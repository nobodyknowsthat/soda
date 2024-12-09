#include <config.h>
#include <const.h>
#include <libcoro.h>
#include <stdlib.h>
#include "../proto.h"
#include <errno.h>
#include <utils.h>
#include <page.h>
#include <memalloc.h>
#include <slab.h>
#include <string.h>

#include "coro_internal.h"

coro_thread_t current_thread;
coro_queue_t free_threads;
coro_queue_t run_queue;
coro_tcb_t main_thread;
coro_tcb_t** threads;
int nr_threads;

#define CORO_STACK_MIN  0x1000
#define STACKGUARD_SIZE 0x1000

#define NR_INITIAL_THREADS 4

static coro_attr_t __default_thread_attr = {
    .stacksize = CORO_STACK_MIN,
    .stackaddr = NULL,
};

static void coro_thread_init(coro_thread_t thread, coro_attr_t* attr,
                             void* (*proc)(void*), void* arg);
static void coro_thread_stop(coro_thread_t thread);
static void coro_trampoline(void);
static int realloc_thread_pool(void);

void coro_init(void)
{
    static int initialized = 0;

    if (initialized) {
        return;
    }

    coro_thread_reset(MAIN_THREAD);
    if (getcontext(&main_thread.context) == -1) {
        panic("cannot get context for main thread");
    }

    nr_threads = 0;
    current_thread = MAIN_THREAD;
    coro_queue_init(&free_threads);

    coro_scheduler_init();

    initialized = 1;
}

coro_tcb_t* coro_find_tcb(coro_thread_t thread)
{
    if (!is_valid_id(thread)) {
        panic("invalid coroutine id %d", thread);
    }

    if (thread == MAIN_THREAD) {
        return &main_thread;
    } else {
        return threads[thread];
    }
}

int coro_thread_create(coro_thread_t* tid, coro_attr_t* attr,
                       void* (*proc)(void*), void* arg)
{
    coro_thread_t thread;

    if (proc == NULL) {
        return EINVAL;
    }

    if (!coro_queue_empty(&free_threads)) {
        thread = coro_queue_dequeue(&free_threads);
        coro_thread_init(thread, attr, proc, arg);

        if (tid != NULL) {
            *tid = thread;
        }
    } else {
        if (realloc_thread_pool() == -1) {
            return EAGAIN;
        }

        return coro_thread_create(tid, attr, proc, arg);
    }

    return 0;
}

static void coro_thread_init(coro_thread_t thread, coro_attr_t* attr,
                             void* (*proc)(void*), void* arg)
{
    coro_tcb_t* tcb = coro_find_tcb(thread);
    size_t stacksize;
    void* stackaddr;

    tcb->next = NULL;
    tcb->state = CR_DEAD;
    tcb->proc = proc;
    tcb->arg = arg;

    if (attr != NULL) {
        tcb->attr = *attr;
    } else {
        tcb->attr = __default_thread_attr;
    }

    if (coro_cond_init(&tcb->exited, NULL) != 0) {
        panic("cannot initialize condition variable");
    }

    if (coro_mutex_init(&tcb->exitm, NULL) != 0) {
        panic("cannot initialize mutex");
    }

    tcb->context.uc_link = NULL;

    if (getcontext(&tcb->context) == -1)
        panic("failed to initialize context state");

    stacksize = tcb->attr.stacksize;
    stackaddr = tcb->attr.stackaddr;

    if (stacksize < CORO_STACK_MIN) {
        stacksize = CORO_STACK_MIN;
        stackaddr = tcb->attr.stackaddr = NULL;
    }

    if (stackaddr == NULL) {
        stacksize = roundup(stacksize, ARCH_PG_SIZE);
        stackaddr = alloc_vmpages(stacksize >> ARCH_PG_SHIFT, ZONE_PS_DDR_LOW);

        if (stackaddr == NULL) {
            panic("failed to allocate stack for thread");
        }

        tcb->context.uc_stack.ss_sp = stackaddr;
    } else {
        tcb->context.uc_stack.ss_sp = stackaddr;
    }

    tcb->context.uc_stack.ss_size = stacksize;
    makecontext(&tcb->context, coro_trampoline, 0);

    coro_unsuspend(thread);
}

void coro_thread_reset(coro_thread_t thread)
{
    coro_tcb_t* tcb = coro_find_tcb(thread);

    tcb->id = thread;
    tcb->next = NULL;
    tcb->state = CR_DEAD;
    tcb->proc = NULL;
    tcb->arg = NULL;
    tcb->result = NULL;

    tcb->context.uc_link = NULL;

    if (tcb->attr.stackaddr == NULL) {
        if (tcb->context.uc_stack.ss_sp != NULL) {
            free_mem(__pa(tcb->context.uc_stack.ss_sp),
                     tcb->context.uc_stack.ss_size);
            tcb->context.uc_stack.ss_sp = NULL;
        }
    }

    tcb->context.uc_stack.ss_size = 0;
}

static void coro_thread_stop(coro_thread_t thread)
{
    coro_tcb_t* tcb = coro_find_tcb(thread);

    if (tcb->state == CR_DEAD) {
        return;
    }

    if (thread != current_thread) {
        coro_thread_reset(thread);
        coro_queue_enqueue(&free_threads, thread);
    }
}

void coro_exit(void* result)
{
    coro_tcb_t* tcb = coro_find_tcb(current_thread);

    if (tcb->state == CR_EXITING) {
        return;
    }

    tcb->result = result;
    tcb->state = CR_EXITING;

    if (coro_cond_signal(&tcb->exited) != 0) {
        panic("cannot signal exit");
    }

    coro_schedule();
}

int coro_join(coro_thread_t thread, void** value)
{
    coro_tcb_t* tcb;
    int retval;

    if (!is_valid_id(thread)) {
        return EINVAL;
    } else if (thread == current_thread) {
        return EDEADLK;
    }

    tcb = coro_find_tcb(thread);
    if (tcb->state == CR_DEAD) {
        return ESRCH;
    }

    if (tcb->state != CR_EXITING) {
        if ((retval = coro_mutex_lock(&tcb->exitm)) != 0) {
            return retval;
        }

        if ((retval = coro_cond_wait(&tcb->exited, &tcb->exitm)) != 0) {
            return retval;
        }

        if ((retval = coro_mutex_unlock(&tcb->exitm)) != 0) {
            return retval;
        }
    }

    if (value != NULL) {
        *value = tcb->result;
    }

    coro_thread_stop(thread);
    return 0;
}

coro_thread_t coro_self(void) { return current_thread; }

static void coro_trampoline(void)
{
    void* result;
    coro_tcb_t* tcb = coro_find_tcb(current_thread);

    result = (tcb->proc)(tcb->arg);
    coro_exit(result);
}

static int realloc_thread_pool(void)
{
    int old_nr_threads, new_nr_threads;
    coro_tcb_t** new_threads;
    int i;

    old_nr_threads = nr_threads;

    if (old_nr_threads == 0) {
        new_nr_threads = NR_INITIAL_THREADS;
    } else {
        new_nr_threads = old_nr_threads * 2;
    }

    if (new_nr_threads >= MAX_THREADS) {
        return -1;
    }

    if (old_nr_threads == 0) {
        new_threads = calloc(new_nr_threads, sizeof(coro_tcb_t*));
    } else {
        new_threads = realloc(threads, sizeof(coro_tcb_t*) * new_nr_threads);
    }

    for (i = old_nr_threads; i < new_nr_threads; i++) {
        SLABALLOC(new_threads[i]);

        if (new_threads[i] == NULL) {
            while (i-- > old_nr_threads) {
                free(new_threads[i]);
            }

            if (old_nr_threads == 0) {
                free(new_threads);
            } else {
                threads =
                    realloc(new_threads, sizeof(coro_tcb_t) * old_nr_threads);
                if (threads == NULL) {
                    panic("failed to shrink thread pool");
                }
            }

            return -1;
        }

        memset(new_threads[i], 0, sizeof(coro_tcb_t));
    }

    threads = new_threads;
    nr_threads = new_nr_threads;

    for (i = old_nr_threads; i < nr_threads; i++) {
        coro_queue_enqueue(&free_threads, i);
        coro_thread_reset(i);
    }

    return 0;
}

void coro_stacktrace(coro_thread_t thread)
{
#ifdef __aarch64__
    coro_tcb_t* tcb;
    ucontext_t* ctx;
    unsigned long fp, hfp, pc;

    xil_printf("Stacktrace (thread %d) ", thread);

    tcb = coro_find_tcb(thread);
    ctx = &tcb->context;

    pc = _UC_MACHINE_LR(ctx);
    xil_printf("0x%lx ", (unsigned long)pc);

    fp = _UC_MACHINE_FP(ctx);
    while (fp) {
        pc = ((unsigned long*)fp)[1];
        hfp = ((unsigned long*)fp)[0];

        xil_printf("0x%lx ", (unsigned long)pc);

        if (hfp != 0 && hfp <= fp) {
            pc = -1;
            xil_printf("0x%lx ", (unsigned long)pc);
            break;
        }

        fp = hfp;
    }

    xil_printf("\n");
#endif
}
