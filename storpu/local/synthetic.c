#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <storpu.h>
#include <storpu/thread.h>
#include <storpu/sched.h>
#include <storpu/file.h>

#include <arm_neon.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define BLOCK_SIZE (64 << 10UL)

#define FD_STATS 13
#define FD_KNN   14
#define FD_GREP  15

static void do_stats(int bits, const uint8_t* buf, size_t count, uint64_t* sump,
                     uint64_t* maxp, uint64_t* minp)
{
    int stride = bits >> 3;
    const uint8_t* end = buf + count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;

    sum = *sump;
    min = *minp;
    max = *maxp;

    while (buf < end) {
        uint64_t val;

        if (bits == 32) {
            val = *(const uint32_t*)buf;
        } else {
            val = *(const uint64_t*)buf;
        }

        sum += val;
        max = MAX(max, val);
        min = MIN(min, val);

        buf += stride;
    }

    *sump = sum;
    *maxp = max;
    *minp = min;
}

unsigned long stats_workload(unsigned long arg)
{
    size_t pos;
    unsigned char* io_buf;
    uint64_t sum;
    uint64_t max;
    uint64_t min;
    cpu_set_t cpuset;

    struct {
        unsigned long start_offset;
        unsigned long end_offset;
        int bits;
        int tid;
    } stats_arg;

    spu_read(FD_SCRATCHPAD, &stats_arg, sizeof(stats_arg), arg);

    CPU_ZERO(&cpuset);
    CPU_SET(stats_arg.tid % 3, &cpuset);
    spu_sched_setaffinity(spu_thread_self(), sizeof(cpuset), &cpuset);

    spu_printf("Starting thread %d\n", stats_arg.tid);
    io_buf =
        mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);

    sum = 0;
    max = 0;
    min = UINT64_MAX;

    for (pos = stats_arg.start_offset; pos < stats_arg.end_offset;
         pos += BLOCK_SIZE) {
        size_t chunk = MIN(BLOCK_SIZE, stats_arg.end_offset - pos);

        if (pos > stats_arg.start_offset && pos % 0x40000000 == 0)
            spu_printf("Pos@%d: %lx\n", stats_arg.tid, pos);

        spu_read(FD_STATS, io_buf, chunk, pos);

        do_stats(stats_arg.bits, io_buf, chunk, &sum, &max, &min);
    }

    spu_printf("%lx %lx %lx\n", sum, min, max);

    munmap(io_buf, BLOCK_SIZE);

    return 0;
}

static double do_knn(int bits, const uint8_t* buf, size_t count)
{
    int stride = bits >> 3;
    const uint8_t* end = buf + count;
    double sum = 0.0;

    while (buf < end) {
        if (bits == 32) {
            float val = *(const float*)buf;
            sum += val * val;
        } else {
            double val = *(const double*)buf;
            sum += val * val;
        }

        buf += stride;
    }

    return sum;
}

unsigned long knn_workload(unsigned long arg)
{
    size_t pos;
    unsigned char* io_buf;
    double sum;
    cpu_set_t cpuset;

    struct {
        unsigned long start_offset;
        unsigned long end_offset;
        int bits;
        int tid;
    } stats_arg;

    spu_read(FD_SCRATCHPAD, &stats_arg, sizeof(stats_arg), arg);

    CPU_ZERO(&cpuset);
    CPU_SET(stats_arg.tid % 3, &cpuset);
    spu_sched_setaffinity(spu_thread_self(), sizeof(cpuset), &cpuset);

    spu_printf("Starting thread %d\n", stats_arg.tid);
    io_buf =
        mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);

    sum = 0.0;

    for (pos = stats_arg.start_offset; pos < stats_arg.end_offset;
         pos += BLOCK_SIZE) {
        size_t chunk = MIN(BLOCK_SIZE, stats_arg.end_offset - pos);

        if (pos > stats_arg.start_offset && pos % 0x40000000 == 0)
            spu_printf("Pos@%d: %lx\n", stats_arg.tid, pos);

        spu_read(FD_KNN, io_buf, chunk, pos);

        sum += do_knn(stats_arg.bits, io_buf, chunk);
    }

    munmap(io_buf, BLOCK_SIZE);

    return 0;
}

void computeLPSArray(const char* pat, size_t M, int* lps)
{
    size_t len = 0;

    lps[0] = 0;

    size_t i = 1;
    while (i < M) {
        if (pat[i] == pat[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
}

bool KMPSearch(const char* txt, size_t N, const char* pat, size_t M, int* lps)
{
    size_t i = 0;
    size_t j = 0;

    while ((N - i) >= (M - j)) {
        if (pat[j] == txt[i]) {
            j++;
            i++;
        }

        if (j == M) {
            return true;
        }

        else if (i < N && pat[j] != txt[i]) {
            if (j != 0)
                j = lps[j - 1];
            else
                i = i + 1;
        }
    }

    return false;
}

bool strstr_simd(const char* s, size_t n, const char* needle, size_t k)
{
    const uint8x16_t first = vdupq_n_u8(needle[0]);
    const uint8x16_t last = vdupq_n_u8(needle[k - 1]);

    const uint8_t* ptr = (const uint8_t*)(s);

    for (size_t i = 0; i < n; i += 16) {

        const uint8x16_t block_first = vld1q_u8(ptr + i);
        const uint8x16_t block_last = vld1q_u8(ptr + i + k - 1);

        const uint8x16_t eq_first = vceqq_u8(first, block_first);
        const uint8x16_t eq_last = vceqq_u8(last, block_last);
        const uint8x16_t pred_16 = vandq_u8(eq_first, eq_last);

        uint64_t mask;

        mask = vgetq_lane_u64(vreinterpretq_u64_u8(pred_16), 0);
        if (mask) {
            // 0
            if ((mask & 0xff) &&
                (memcmp(s + i + 0 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 1
            if ((mask & 0xff) &&
                (memcmp(s + i + 1 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 2
            if ((mask & 0xff) &&
                (memcmp(s + i + 2 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 3
            if ((mask & 0xff) &&
                (memcmp(s + i + 3 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 4
            if ((mask & 0xff) &&
                (memcmp(s + i + 4 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 5
            if ((mask & 0xff) &&
                (memcmp(s + i + 5 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 6
            if ((mask & 0xff) &&
                (memcmp(s + i + 6 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 7
            if ((mask & 0xff) &&
                (memcmp(s + i + 7 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;
        }

        mask = vgetq_lane_u64(vreinterpretq_u64_u8(pred_16), 1);
        if (mask) {
            // 0
            if ((mask & 0xff) &&
                (memcmp(s + i + 0 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 1
            if ((mask & 0xff) &&
                (memcmp(s + i + 1 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 2
            if ((mask & 0xff) &&
                (memcmp(s + i + 2 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 3
            if ((mask & 0xff) &&
                (memcmp(s + i + 3 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 4
            if ((mask & 0xff) &&
                (memcmp(s + i + 4 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 5
            if ((mask & 0xff) &&
                (memcmp(s + i + 5 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 6
            if ((mask & 0xff) &&
                (memcmp(s + i + 6 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;

            // 7
            if ((mask & 0xff) &&
                (memcmp(s + i + 7 + 8 + 1, needle + 1, k - 2) == 0)) {
                return true;
            }

            mask >>= 8;
        }
    }

    return false;
}

unsigned long grep_workload(unsigned long arg)
{
    size_t pos;
    unsigned char* io_buf;
    int matches = 0;
    cpu_set_t cpuset;

    struct {
        unsigned long start_offset;
        unsigned long end_offset;
        int bits;
        int tid;
    } stats_arg;

    const char pat[] = "TUKOZYPDOBRKBLBUTYAIBHSBWROFWGNR";
    int M = 32;
    int lps[32];

    computeLPSArray(pat, M, lps);

    spu_read(FD_SCRATCHPAD, &stats_arg, sizeof(stats_arg), arg);

    CPU_ZERO(&cpuset);
    CPU_SET(stats_arg.tid % 3, &cpuset);
    spu_sched_setaffinity(spu_thread_self(), sizeof(cpuset), &cpuset);

    spu_printf("Starting thread %d\n", stats_arg.tid);
    io_buf =
        mmap(NULL, BLOCK_SIZE * 2, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_CONTIG, -1, 0);
    *((char*)io_buf + BLOCK_SIZE) = '\0';

    for (pos = stats_arg.start_offset; pos < stats_arg.end_offset;
         pos += BLOCK_SIZE) {
        size_t chunk = MIN(BLOCK_SIZE, stats_arg.end_offset - pos);

        if (pos > stats_arg.start_offset && pos % 0x40000000 == 0)
            spu_printf("Pos@%d: %lx\n", stats_arg.tid, pos);

        spu_read(FD_GREP, io_buf, chunk, pos);

        /* if (KMPSearch((char*)io_buf, chunk, pat, M, lps)) matches++; */
        if (strstr_simd((char*)io_buf, chunk, pat, (size_t)M)) matches++;
    }

    munmap(io_buf, BLOCK_SIZE * 2);

    return 0;
}
