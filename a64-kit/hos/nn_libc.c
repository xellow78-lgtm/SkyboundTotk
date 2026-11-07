#include "hos.h"
#include <stdlib.h>
#include <string.h>

static int guest_libc(void) {
    static int v = -1;
    if (v < 0) v = getenv("HOS_GUEST_LIBC") != NULL;
    return v;
}

#if defined(RX_WEB_MT) && RX_WEB_MT
void rx_wait_writebacks_reading(const void *p, uint64_t n);   /* gpu/render/rx_nvn.h */
#define WAIT_WRITEBACKS(p, n) rx_wait_writebacks_reading(p, n)
#else
#define WAIT_WRITEBACKS(p, n) ((void)0)
#endif

/* HOS_WATCH (debugging, hos_watch.c): a copy into a watched address says where it came from */
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
extern volatile int hos_watch_on;
void hos_watch_copy(uint64_t dst, uint64_t src, uint64_t n, uint64_t lr);
#define WATCH_COPY(d, s, n) do { if (hos_watch_on) hos_watch_copy((d), (s), (n), c->x[30]); } while (0)
#else
#define WATCH_COPY(d, s, n) ((void)0)
#endif

/* Render target memory the CPU reads. A render target's pixels reach guest memory only when rx writes it back, which
   it did only after a one-off draw (a target no draw used in the last 64 frames): TOTK's terrain bakes maps into a
   target and copies them out with the CPU, and a burst of bakes into the same target (streaming after a load) left
   the first bake's pixels in memory for the rest. rx marks the 4 KB pages its NVN targets cover (hos_rt_pages); a
   memcpy / memmove out of one, or an InvalidateMappedRange over one (nvn_native.c), notes when (hos_rt_read_s:
   seconds + 1) and, the first time in a minute, bumps hos_rt_read_gen: rx then writes that target back after each
   submission that draws it (rx_draw.cpp, cpu_read_target). */
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define HOS_RT_SPACE (1ull << 33)   /* (8 GB of guest space, in 4 KB pages) */
volatile uint8_t hos_rt_pages[HOS_RT_SPACE >> 12];
volatile uint32_t hos_rt_read_s[HOS_RT_SPACE >> 12];
volatile long hos_rt_read_gen;
void hos_rt_cpu_read(uint64_t guest, uint64_t n, uint64_t lr, const char *how) {
    if (!n || guest >= HOS_RT_SPACE) return;
    const uint64_t last = (guest + n - 1 < HOS_RT_SPACE ? guest + n - 1 : HOS_RT_SPACE - 1) >> 12;
    const uint32_t now = (uint32_t)(hos_now_ns() / 1000000000) + 1;
    int fresh = 0;
    for (uint64_t p = guest >> 12; p <= last; p++)
        if (hos_rt_pages[p]) {
            if (!hos_rt_read_s[p] || hos_rt_read_s[p] + 60 < now) fresh = 1;   /* (first in a minute: rx looks again) */
            if (hos_rt_read_s[p] != now) hos_rt_read_s[p] = now;
        }
    if (!fresh) return;
    __atomic_add_fetch(&hos_rt_read_gen, 1, __ATOMIC_SEQ_CST);
    static volatile long logged;
    if (__atomic_add_fetch(&logged, 1, __ATOMIC_RELAXED) <= 48)
        hos_log("rx: the CPU reads render target memory: %s %llx+%llx from %llx", how, (unsigned long long)guest,
                (unsigned long long)n, (unsigned long long)lr);
}
#define RT_COPY(s, n) do {                                                                                   \
        const uint64_t s_ = (s), e_ = s_ + (n) - 1;                                                          \
        if (e_ < HOS_RT_SPACE && (hos_rt_pages[s_ >> 12] | hos_rt_pages[e_ >> 12]))                          \
            hos_rt_cpu_read(s_, (n), c->x[30], "memcpy");                                                    \
    } while (0)
#else
#define RT_COPY(s, n) ((void)0)
#endif

ENTRY_HOOK_SYM(memcpy) {
    if (guest_libc()) return 0;
    const uint64_t n = ARG(2);
    if (n) {
        WAIT_WRITEBACKS(GP(ARG(1)), n);   /* (TOTK copies some rendered maps with the CPU) */
        RT_COPY(ARG(1), n);
        memmove(GP(ARG(0)), GP(ARG(1)), (size_t)n);
        HOS_WRITTEN(GP(ARG(0)), (size_t)n);
        WATCH_COPY(ARG(0), ARG(1), n);
    }
    return 1;
}

ENTRY_HOOK_SYM(memmove) {
    if (guest_libc()) return 0;
    const uint64_t n = ARG(2);
    if (n) {
        WAIT_WRITEBACKS(GP(ARG(1)), n);
        RT_COPY(ARG(1), n);
        memmove(GP(ARG(0)), GP(ARG(1)), (size_t)n);
        HOS_WRITTEN(GP(ARG(0)), (size_t)n);
        WATCH_COPY(ARG(0), ARG(1), n);
    }
    return 1;
}

ENTRY_HOOK_SYM(memset) {
    if (guest_libc()) return 0;
    const uint64_t n = ARG(2);
    if (n) {
        memset(GP(ARG(0)), (int)(uint8_t)ARG(1), (size_t)n);
        HOS_WRITTEN(GP(ARG(0)), (size_t)n);
    }
    return 1;
}

ENTRY_HOOK_SYM(memcmp) {
    if (guest_libc()) return 0;
    const uint8_t *a = (const uint8_t *)GP(ARG(0)), *b = (const uint8_t *)GP(ARG(1));
    const size_t n = (size_t)ARG(2);
    int r = 0;
    if (n && memcmp(a, b, n)) {
        size_t i = 0;
        while (a[i] == b[i]) i++;
        r = (int)a[i] - (int)b[i];
    }
    RET((uint64_t)(int64_t)r);
    return 1;
}

ENTRY_HOOK_SYM(strlen) {
    if (guest_libc()) return 0;
    RET(strlen((const char *)GP(ARG(0))));
    return 1;
}

ENTRY_HOOK_SYM(strcmp) {
    if (guest_libc()) return 0;
    const uint8_t *a = (const uint8_t *)GP(ARG(0)), *b = (const uint8_t *)GP(ARG(1));
    int r = 0;
    if (strcmp((const char *)a, (const char *)b)) {
        size_t i = 0;
        while (a[i] && a[i] == b[i]) i++;
        r = (int)a[i] - (int)b[i];
    }
    RET((uint64_t)(int64_t)r);
    return 1;
}
