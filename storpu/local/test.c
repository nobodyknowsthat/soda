#include <stdlib.h>
#include <storpu.h>
#include <storpu/thread.h>
#include <storpu/sched.h>
#include <storpu/file.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

spu_mutex_t mutex;

__thread int i;

unsigned long thread_func(unsigned long arg)
{
    // cpu_set_t cpuset;
    // CPU_ZERO(&cpuset);
    // CPU_SET(1, &cpuset);
    // spu_sched_setaffinity(spu_thread_self(), sizeof(cpuset), &cpuset);

    spu_mutex_lock(&mutex);
    spu_printf("In thread %p\n", &i);
    spu_mutex_unlock(&mutex);

    return 0;
}

unsigned long hello_world(unsigned long arg)
{
    spu_printf("Hello world\n");
    return 0;
}

unsigned long test(unsigned long arg)
{
    void* addr = sbrk(0x4000);

    void* map_addr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

    void* a = malloc(48);

    void* map_flash =
        mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, 0, 0);

    /* void* map_flash = mmap(NULL, 0x8000, PROT_READ | PROT_WRITE, MAP_SHARED,
     */
    /*                        FD_HOST_MEM, arg); */

    spu_printf("Arg %lx\n", arg);
    spu_printf("Hello world %p %p %p %p\n", addr, map_addr, a, &i);

    /* spu_read(FD_HOST_MEM, map_addr, 0x1000, 0x1000); */
    /* int i; */
    /* for (i = 0; i < 0x1000; i += 4) { */
    /*     spu_printf("%x\n", *(unsigned int*)(map_addr + i)); */
    /* } */
    spu_printf("Value %lx\n", *(unsigned long*)(map_flash + 0x0));
    spu_printf("Value %lx\n", *(unsigned long*)(map_flash + 0x1000));
    spu_printf("Value %lx\n", *(unsigned long*)(map_flash + 0x2000));
    /* *(unsigned long*)(map_flash + 0x1000) = 0x1234; */
    /* msync(map_flash, 0x8000, MS_SYNC); */
    /* *(unsigned long*)(map_flash + 0x2000) = 0x5678; */
    /* msync(map_flash, 0x8000, MS_SYNC); */

    spu_mutex_init(&mutex, NULL);

    spu_thread_t thread;
    spu_thread_create(&thread, NULL, thread_func, 0);
    spu_printf("Create thread %d\n", thread);

    spu_thread_join(thread, NULL);
    spu_printf("Thread %d returned\n", thread);

    return 0;
}

unsigned long data_read(unsigned long args_ptr)
{
    // allocate a buffer in ssd
    unsigned char* io_buf =
        mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);

    for (i = 0; i < 0x4000; i++)
        io_buf[i] = 0;
    spu_read(1, io_buf, 0x4000, 0);

    int i, j;
    for (i = 0; i < 0x4000; i += 32) {
        for (j = 0; j < 32; j++) {
            spu_printf("%02x ", io_buf[i + j]);
        }
        spu_printf("\n");
    }

    munmap(io_buf, 0x4000);
    return 0;
}

struct load_data_thread_arg {
    int fd;
    unsigned long host_addr;
    unsigned long file_addr;
    size_t length;
};

#define FLASH_PG_SIZE 16384

static unsigned long load_data_thread(unsigned long arg)
{
    struct load_data_thread_arg* lda = (void*)arg;
    void* buf =
        mmap(NULL, FLASH_PG_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);
    size_t offset = 0;

    spu_printf("Load %lx -> %lx, length %lx buf %p\n", lda->host_addr,
               lda->file_addr, lda->length, buf);

    while (offset < lda->length) {
        size_t chunk = MIN(FLASH_PG_SIZE, lda->length - offset);

        spu_read(FD_HOST_MEM, buf, chunk, lda->host_addr + offset);
        spu_write(lda->fd, buf, chunk, lda->file_addr + offset);

        offset += chunk;
    }

    munmap(buf, 0x4000);
    return 0;
}

#define NR_LOAD_DATA_THREADS 8

unsigned long load_data(unsigned long arg)
{
    spu_thread_t threads[NR_LOAD_DATA_THREADS];
    struct load_data_thread_arg thread_args[NR_LOAD_DATA_THREADS];
    struct {
        unsigned long fd;
        unsigned long host_addr;
        unsigned long flash_addr;
        unsigned long length;
    } lda;
    size_t thread_len;

    spu_read(FD_SCRATCHPAD, &lda, sizeof(lda), arg);

    thread_len = lda.length / NR_LOAD_DATA_THREADS;
    if (thread_len % FLASH_PG_SIZE)
        thread_len += FLASH_PG_SIZE - (thread_len % FLASH_PG_SIZE);

    for (i = 0; i < NR_LOAD_DATA_THREADS; i++) {
        struct load_data_thread_arg* ldta = &thread_args[i];
        size_t chunk = MIN(lda.length, thread_len);

        ldta->fd = lda.fd;
        ldta->host_addr = lda.host_addr;
        ldta->file_addr = lda.flash_addr;
        ldta->length = chunk;

        spu_thread_create(&threads[i], NULL, load_data_thread,
                          (unsigned long)ldta);

        lda.host_addr += chunk;
        lda.flash_addr += chunk;
        lda.length -= chunk;
    }

    for (i = 0; i < NR_LOAD_DATA_THREADS; i++) {
        spu_thread_join(threads[i], NULL);
    }

    sync();

    return 0;
}
