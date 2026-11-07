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

ENTRY_HOOK_SYM(memcpy) {
    if (guest_libc()) return 0;
    const uint64_t n = ARG(2);
    if (n) {
        WAIT_WRITEBACKS(GP(ARG(1)), n);   /* (TOTK copies some rendered maps with the CPU) */
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
