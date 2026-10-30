/* Guest memory map: the system region (our own objects), the heap, address regions.
   The whole 4 GB window is committed up front (hos_main.c), so "mapping" here is only bookkeeping and zeroing. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK lock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&lock)
#define UNLOCK() ReleaseSRWLockExclusive(&lock)
#else
#include <pthread.h>
#include <sys/mman.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&lock)
#define UNLOCK() pthread_mutex_unlock(&lock)
#endif

uint64_t hos_heap_size, hos_heap_used, hos_total_memory, hos_program_size;
static uint64_t sys_next = HOS_SYSTEM_BASE;

/* Address regions are handed out top-down from HOS_SPACE_END; the heap grows up from HOS_HEAP_BASE. */
typedef struct { uint64_t addr, size; } Range;
static Range *regions;
static int nregions, capregions;
static uint64_t region_floor = ~0ULL;   /* the lowest region (none yet: the window's end) */

/* Free address space below the heap that regions may also use (web: the window is tight): above the null guard
   to the system region, and from the image's end to the heap. */
static uint64_t low_hole[2][2];

void hos_mem_init(uint64_t image_end) {
    hos_program_size = image_end - 0x08000000ULL;
    low_hole[0][0] = 0x00100000ULL, low_hole[0][1] = HOS_SYSTEM_BASE;
    low_hole[1][0] = (image_end + 0xFFFFFULL) & ~0xFFFFFULL, low_hole[1][1] = HOS_HEAP_BASE;
    /* Retail Switch gives an application 3285 MB. HOS_MEMORY_MB overrides it: TOTK sizes its heap from this
       (heap = total - used - 1080 MB), which is the lever for fitting the web build into wasm32. */
    const char *mb = getenv("HOS_MEMORY_MB");
#ifdef A64_MEM_ZERO
    /* (web: TOTK keeps 1080 MB of the total for address regions (its resource memory grows into them) and its heap
       must stay 2104 MB (a 1902 MB heap: an indirect call into nothing 4 s in), so the window must hold heap +
       regions; the holes below the heap help) */
    hos_total_memory = (mb ? strtoull(mb, NULL, 0) : 3285ULL) << 20;
    hos_log("memory: %llu MB for the game (window %llu MB)", (unsigned long long)(hos_total_memory >> 20),
            (unsigned long long)(HOS_SPACE_END >> 20));
#else
    hos_total_memory = (mb ? strtoull(mb, NULL, 0) : 3285ULL) << 20;
#endif
}

uint64_t hos_region_floor(void) { return region_floor; }

uint64_t hos_sys_alloc(uint64_t size, uint64_t align) {
    LOCK();
    uint64_t a = (sys_next + align - 1) & ~(align - 1);
    if (a + size > HOS_SYSTEM_END) {
        UNLOCK();
        hos_fatal(NULL, "system region exhausted (%#llx bytes asked)", (unsigned long long)size);
    }
    sys_next = a + size;
    UNLOCK();
    memset(GP(a), 0, size);
    HOS_WRITTEN(GP(a), size);
    return a;
}

/* what the game backs with pages (AllocateMemoryPages less FreeMemoryPages), and the region calls: logged with the
   address regions below, to tell sparse reservations from memory really in use */
volatile long long hos_pages_bytes, hos_meminfo_queries;   /* (hos.h) */
static volatile LONG64 region_allocs, region_frees;
#define pages_bytes hos_pages_bytes

/* The game's regions go above 4 GB first (HOS_HIGH_BASE..HOS_HIGH_END, natively: rt/a64.h A64_SPACE_BITS), each
   committed as it is made: the low 4 GB keep only what does not fit there. HOS_HIGH_REGIONS=0: the low 4 GB only, as
   before. The system's own regions (high = 0) stay low. */
static uint64_t region_alloc(uint64_t size, uint64_t align, int high) {
    if (align < 0x1000) align = 0x1000;
    size = (size + 0xFFF) & ~0xFFFULL;
    uint64_t found = 0;
    LOCK();
    int at = nregions;
#ifdef HOS_HIGH_BASE
    static int high_on = -1;
    if (high_on < 0) high_on = !getenv("HOS_HIGH_REGIONS") || atoi(getenv("HOS_HIGH_REGIONS")) != 0;
    if (high && high_on) {
        uint64_t top = HOS_HIGH_END;
        for (int i = 0;; i++) {
            const int in_high = i < nregions && regions[i].addr >= HOS_HIGH_BASE;
            const uint64_t bottom = in_high ? regions[i].addr + regions[i].size : HOS_HIGH_BASE;
            if (top >= bottom + size) {
                const uint64_t a = (top - size) & ~(align - 1);
                if (a >= bottom) { found = a; at = i; break; }
            }
            if (!in_high) break;
            top = regions[i].addr;
        }
#if defined(_WIN32)
        if (found && !VirtualAlloc(GP(found), size, MEM_COMMIT, PAGE_READWRITE)) {
            static int told;
            if (!told++)
                hos_log("mem: %llu MB above 4 GB not committed (error %lu: Windows' commit limit?): the low 4 GB instead",
                        (unsigned long long)(size >> 20), GetLastError());
            found = 0;
            at = nregions;
        }
#endif
    }
#endif
    /* first fit, top-down, between existing regions (kept sorted by address, descending), below HOS_SPACE_END */
    uint64_t top = HOS_SPACE_END;
    int i0 = 0;
    while (i0 < nregions && regions[i0].addr >= HOS_SPACE_END) i0++;   /* (past the regions above 4 GB) */
    for (int i = i0; i <= nregions && !found; i++) {
        uint64_t bottom = i < nregions ? regions[i].addr + regions[i].size : HOS_HEAP_BASE + hos_heap_size;
        if (top >= size) {
            uint64_t a = (top - size) & ~(align - 1);
            if (a >= bottom && a >= HOS_HEAP_BASE + hos_heap_size) { found = a; at = i; break; }
        }
        if (i < nregions) top = regions[i].addr;
    }
    if (!found)   /* the holes below the heap, top-down, between the regions there (the list is descending) */
        for (int h = 1; h >= 0 && !found; h--) {
            uint64_t hi = low_hole[h][1];
            int i = 0;
            while (i < nregions && regions[i].addr >= hi) i++;
            for (;; i++) {
                const uint64_t lo = i < nregions && regions[i].addr + regions[i].size > low_hole[h][0]
                                        ? regions[i].addr + regions[i].size : low_hole[h][0];
                if (hi >= size) {
                    const uint64_t a = (hi - size) & ~(align - 1);
                    if (a >= lo) { found = a; at = i; break; }
                }
                if (i >= nregions || regions[i].addr < low_hole[h][0]) break;
                hi = regions[i].addr;
            }
        }
    if (found) {
        if (nregions == capregions) {
            capregions = capregions ? capregions * 2 : 64;
            regions = (Range *)realloc(regions, capregions * sizeof(Range));
        }
        memmove(&regions[at + 1], &regions[at], (nregions - at) * sizeof(Range));
        regions[at].addr = found;
        regions[at].size = size;
        nregions++;
        if (found < region_floor && found >= HOS_HEAP_BASE) region_floor = found;   /* (not the holes below) */
    }
    /* how much the game holds in regions, logged at each new 128 MB step (on the Switch regions take addresses from a
       64 GB alias range of their own; here 4 GB above the low 4 GB, and the low 4 GB's ~1.7 GB above the heap) */
    uint64_t held = 0, largest = 0;
    for (int i = 0; i < nregions; i++) {
        held += regions[i].size;
        if (regions[i].size > largest) largest = regions[i].size;
    }
    static uint64_t logged_step;
    const uint64_t step = held >> 27;
    const int n = nregions;
    UNLOCK();
    if (found && step > logged_step) {
        logged_step = step;
        hos_log("mem: address regions: %d holding %llu MB (largest %llu MB; %lld made, %lld freed), pages %lld MB, heap "
                "%llu MB; the game asked how much memory is used %lld times", n, (unsigned long long)(held >> 20),
                (unsigned long long)(largest >> 20), (long long)region_allocs, (long long)region_frees,
                (long long)(pages_bytes >> 20), (unsigned long long)(hos_heap_size >> 20),
                (long long)hos_meminfo_queries);
    }
    if (!found) hos_fatal(NULL, "address space exhausted (%#llx bytes asked, heap %#llx; %d regions holding %llu MB, "
                          "largest %llu MB; %lld made, %lld freed; pages %lld MB)", (unsigned long long)size,
                          (unsigned long long)hos_heap_size, n, (unsigned long long)(held >> 20),
                          (unsigned long long)(largest >> 20), (long long)region_allocs, (long long)region_frees,
                          (long long)(pages_bytes >> 20));
    return found;
}

uint64_t hos_region_alloc(uint64_t size, uint64_t align) { return region_alloc(size, align, 0); }

static uint64_t pages_used[(1ULL << A64_SPACE_BITS) / 0x1000 / 64];

/* The pages the game has backed (AllocateMemoryPages, until FreeMemoryPages or the region's FreeAddressRegion), one
   bit a page: hos_pages_bytes counts them. (Adding and subtracting the calls' sizes counted pages backed twice and
   never subtracted the ones a freed region still held: 2,071 MB of "pages" in 1,664 MB of regions.) */
static uint64_t pages_backed[(1ULL << A64_SPACE_BITS) / 0x1000 / 64];
static void backed_set_locked(uint64_t addr, uint64_t size, int on) {
    const uint64_t p0 = addr >> 12, p1 = (addr + size + 0xFFF) >> 12;
    /* (pages in the heap are not counted: the heap is counted whole; the game backs pages there too, 1,255 MB of
       "pages" in 1,024 MB of regions) */
    const uint64_t h0 = HOS_HEAP_BASE >> 12, h1 = (HOS_HEAP_BASE + hos_heap_size) >> 12;
    int64_t changed = 0;
    for (uint64_t p = p0; p < p1 && p < A64_PAGES; p++) {
        if (p >= h0 && p < h1) continue;
        uint64_t *w = &pages_backed[p >> 6];
        const uint64_t bit = 1ULL << (p & 63);
        if (!!(*w & bit) != on) {
            *w ^= bit;
            changed++;
        }
    }
    hos_pages_bytes += (on ? changed : -changed) << 12;
}

static void pages_mark_used_locked(uint64_t addr, uint64_t size) {
    for (uint64_t p = addr >> 12; p < (addr + size + 0xFFF) >> 12 && p < A64_PAGES; p++) pages_used[p >> 6] |= 1ULL << (p & 63);
}

static void discard_pages(uint64_t addr, uint64_t size) {
    const uint64_t lo = (addr + 0xFFF) & ~0xFFFULL, hi = (addr + size) & ~0xFFFULL;
    static int keep = -1;
    if (keep < 0) keep = getenv("HOS_KEEP_FREED_PAGES") != NULL;
    if (hi <= lo || keep) return;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    extern int hos_large_pages;   /* (hos_main.c: large pages cannot be discarded) */
    if (hos_large_pages) return;
    typedef DWORD(WINAPI * DiscardFn)(PVOID, SIZE_T);
    static DiscardFn discard;
    static int looked;
    if (!looked) {
        looked = 1;
        discard = (DiscardFn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "DiscardVirtualMemory");
    }
    if (!discard || discard(GP(lo), (SIZE_T)(hi - lo)) != ERROR_SUCCESS)
        VirtualAlloc(GP(lo), (SIZE_T)(hi - lo), MEM_RESET, PAGE_READWRITE);
#elif !defined(__EMSCRIPTEN__)
    madvise(GP(lo), (size_t)(hi - lo), MADV_DONTNEED);
#endif
}

void hos_region_free(uint64_t addr) {
    uint64_t freed = 0, freed_size = 0;
    LOCK();
    for (int i = 0; i < nregions; i++) {
        if (regions[i].addr == addr) {
            pages_mark_used_locked(regions[i].addr, regions[i].size);
            backed_set_locked(regions[i].addr, regions[i].size, 0);
            freed = regions[i].addr;
            freed_size = regions[i].size;
            memmove(&regions[i], &regions[i + 1], (nregions - i - 1) * sizeof(Range));
            nregions--;
            break;
        }
    }
    UNLOCK();
#if defined(HOS_HIGH_BASE) && defined(_WIN32)
    if (freed_size && freed >= HOS_HIGH_BASE) {   /* (committed when made: back to Windows) */
        VirtualFree(GP(freed), freed_size, MEM_DECOMMIT);
        return;
    }
#endif
    if (freed_size) discard_pages(freed, freed_size);
}

/* ---- nn::os memory -------------------------------------------------------------------------------------- */

#if defined(A64_WRITE_TRACK) && !defined(__EMSCRIPTEN__)
/* HOS_WRITTEN natively (the soft write watch, rt/a64.h A64_MARK): host code wrote [p, p + n) of guest memory */
void hos_mark_written(const void *p, size_t n) {
    const uintptr_t a = (uintptr_t)p - (uintptr_t)A64_MEM;
    if (!n || (uintptr_t)p < (uintptr_t)A64_MEM || a >= (1ull << A64_SPACE_BITS)) return;
    for (uintptr_t q = a >> 12; q <= (a + n - 1) >> 12 && q < A64_PAGES; q++)
        __atomic_store_n(&a64_dirty[q], 1, __ATOMIC_RELEASE);
}
#endif

/* nn::os::SetMemoryHeapSize(unsigned long) -> Result */
HLE(_ZN2nn2os17SetMemoryHeapSizeEm) {
    uint64_t size = ARG(0);
    HOS_TRACE_CALL("SetMemoryHeapSize");
    if (HOS_HEAP_BASE + size > (region_floor < HOS_SPACE_END ? region_floor : HOS_SPACE_END)) {
        hos_log("SetMemoryHeapSize(%#llx): collides with address regions at %#llx", (unsigned long long)size,
                (unsigned long long)region_floor);
        RET(0xCA01);   /* svc::ResultOutOfMemory */
        return;
    }
    if (size < hos_heap_size) {
        memset(GP(HOS_HEAP_BASE + size), 0, hos_heap_size - size);
        HOS_WRITTEN(GP(HOS_HEAP_BASE + size), hos_heap_size - size);
    }
    /* HOS_PREFAULT=1 (experiment): the new heap pages touched now (a zero written to fresh, zero memory), so that the
       game's first touches while streaming are not page faults (which may hold up the renderer's write-watch scans) */
    if (size > hos_heap_size && getenv("HOS_PREFAULT")) {
        const int64_t t0 = hos_now_ns();
        for (uint64_t a = HOS_HEAP_BASE + hos_heap_size; a < HOS_HEAP_BASE + size; a += 4096) *(volatile uint8_t *)GP(a) = 0;
        hos_log("heap: %llu MB prefaulted in %.0f ms", (unsigned long long)((size - hos_heap_size) >> 20), (hos_now_ns() - t0) / 1e6);
    }
    hos_heap_size = size;
    hos_log("heap: %llu MB at %#llx", (unsigned long long)(size >> 20), (unsigned long long)HOS_HEAP_BASE);
    RET(0);
}

/* nn::os::AllocateMemoryBlock(unsigned long*, unsigned long) -> Result: 2 MB-aligned blocks from the heap */
HLE(_ZN2nn2os19AllocateMemoryBlockEPmm) {
    uint64_t out = ARG(0), size = (ARG(1) + 0x1FFFFF) & ~0x1FFFFFULL;
    HOS_TRACE_CALL("AllocateMemoryBlock");
    LOCK();
    uint64_t a = HOS_HEAP_BASE + hos_heap_used;
    int ok = hos_heap_used + size <= hos_heap_size;
    if (ok) hos_heap_used += size;
    UNLOCK();
    if (!ok) {
        hos_log("AllocateMemoryBlock(%#llx): heap exhausted", (unsigned long long)size);
        RET(0x1A01);   /* os::ResultOutOfMemory */
        return;
    }
    G64(out) = a;
    RET(0);
}

/* nn::os::AllocateAddressRegion(unsigned long*, unsigned long) -> Result */
HLE(_ZN2nn2os21AllocateAddressRegionEPmm) {
    HOS_TRACE_CALL("AllocateAddressRegion");
    InterlockedIncrement64(&region_allocs);
    G64(ARG(0)) = region_alloc(ARG(1), 0x10000, 1);
    RET(0);
}

/* nn::os::FreeAddressRegion(unsigned long) -> Result */
HLE(_ZN2nn2os17FreeAddressRegionEm) {
    HOS_TRACE_CALL("FreeAddressRegion");
    InterlockedIncrement64(&region_frees);
    hos_region_free(ARG(0));
    RET(0);
}

/* nn::os::AllocateMemoryPages(unsigned long, unsigned long) -> Result: new pages read as zero */
/* what the game backs with pages (AllocateMemoryPages less FreeMemoryPages), and the calls: logged with the address
   regions (hos_region_alloc), to tell sparse reservations from memory really in use */
HLE(_ZN2nn2os19AllocateMemoryPagesEmm) {
    HOS_TRACE_CALL("AllocateMemoryPages");
    const uint64_t a = ARG(0), n = ARG(1), end = a + n;
    LOCK();
    backed_set_locked(a, n, 1);
    UNLOCK();

    static int zero_all = -1;
    if (zero_all < 0) zero_all = getenv("HOS_ZERO_ALL_PAGES") != NULL;
    if (zero_all || end > (1ULL << A64_SPACE_BITS) || end < a) {
        memset(GP(a), 0, n);
    } else {
        uint64_t runs[64][2];
        int nruns = 0, overflow = 0, in_run = 0;
        LOCK();
        for (uint64_t p = a & ~0xFFFULL; p < end; p += 0x1000) {
            const uint64_t pg = p >> 12, bit = 1ULL << (pg & 63);
            const int used = (pages_used[pg >> 6] & bit) != 0;
            pages_used[pg >> 6] |= bit;
            if (used && !in_run) {
                if (nruns == 64) overflow = 1;
                else runs[nruns][0] = p < a ? a : p;
                in_run = 1;
            } else if (!used && in_run) {
                if (nruns < 64) runs[nruns++][1] = p;
                in_run = 0;
            }
        }
        if (in_run && nruns < 64) runs[nruns++][1] = end;
        UNLOCK();
        if (overflow) memset(GP(a), 0, n);
        else
            for (int i = 0; i < nruns; i++) memset(GP(runs[i][0]), 0, runs[i][1] - runs[i][0]);
    }
    HOS_WRITTEN(GP(a), n);
    RET(0);
}

/* nn::os::FreeMemoryPages(unsigned long, unsigned long) -> Result */
HLE(_ZN2nn2os15FreeMemoryPagesEmm) {
    HOS_TRACE_CALL("FreeMemoryPages");
    LOCK();
    backed_set_locked(ARG(0), ARG(1), 0);
    UNLOCK();
    discard_pages(ARG(0), ARG(1));
    RET(0);
}

/* Memory in use as the Switch counts it: the program and the system's share, the heap once it is set, and the pages
   the game backed in its address regions. The pages were left out (and the heap from QueryMemoryInfo): TOTK then saw
   its ~1 GB for regions as never filling, kept every place it had loaded (pages 2.2 GB after a few teleports, past the
   1080 MB it sets aside for them) and the process ran Windows out of commit. At the start (no heap, no pages) this is
   the program + 16 MB the game sizes its heap from. HOS_PAGES_UNCOUNTED=1: the pages left out, as before. */
uint64_t hos_used_memory(void) {
    static int uncounted = -1;
    if (uncounted < 0) uncounted = getenv("HOS_PAGES_UNCOUNTED") != NULL;
    const int64_t pages = uncounted ? 0 : hos_pages_bytes;
    const uint64_t used = hos_program_size + (16ULL << 20) + hos_heap_size + (uint64_t)(pages > 0 ? pages : 0);
    return used < hos_total_memory ? used : hos_total_memory;   /* (never past the total: free memory would wrap) */
}

/* nn::os::QueryMemoryInfo(nn::os::MemoryInfo*). TOTK reads +0 (total available) and +8 (total used):
   heap = align_down(total - used, 2 MB) - 0x43800000 (main+0x1131004). */
HLE(_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE) {
    uint64_t info = ARG(0);
    HOS_TRACE_CALL("QueryMemoryInfo");
    InterlockedIncrement64(&hos_meminfo_queries);
    G64(info + 0x00) = hos_total_memory;                       /* total available */
    G64(info + 0x08) = hos_used_memory();                      /* total used (hos_used_memory) */
    G64(info + 0x10) = hos_heap_size;                          /* total heap */
    G64(info + 0x18) = hos_heap_used;                          /* allocated heap */
    G64(info + 0x20) = hos_program_size;                       /* program */
    G64(info + 0x28) = 0;                                      /* thread stacks */
    G32(info + 0x30) = 1;                                      /* threads */
}

/* nn::os::SetMemoryAllocatorForThreadLocal runs as the SDK's own code: it stores the game's allocator where
   nnosAllocateMemoryForThreadLocal finds it, which nnmusl uses for each thread's TLS (hos_thread.c thread_main). */

/* nn::os::SetDyingMessageRegion(unsigned long, unsigned long) -> Result */
HLE(_ZN2nn2os21SetDyingMessageRegionEmm) { RET(0); }
