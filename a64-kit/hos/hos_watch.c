/* Debugging: who reads or writes a guest address. x64 hardware data breakpoints (DR0-DR3) on every guest thread;
   each hit is counted by the host code address that made it (a lifted function shows as f_XXXXXXXX, the guest
   code at XXXXXXXX), the first hits of each with a backtrace, and a summary every 5 s while there are new hits.

   HOS_WATCH=<guest address>[:<1|2|4|8 bytes>][,...] (up to 4) arms at start, after HOS_WATCH_AFTER=<s> (default 30);
   code that finds an address at run time arms it with hos_watch_arm (e.g. totk_watch.c). A breakpoint goes on the
   threads that exist when it is armed, and on the game's threads started after that (hos_watch_thread, from
   hos_thread_start), so HOS_WATCH_AFTER=0 sees the loading too. A memcpy / memmove of the game's into a watched
   address logs where the bytes came from; HOS_WATCH_FOLLOW=1 then watches those source bytes too (free slots
   only). Needs the PDB next to the exe for names.

   hos_guard_arm(guest, len, why): every access to a whole range (up to 4), by any thread, through guard pages, each
   logged with who made it, and the offset in the range (RX_GUARD_GLOOM: the Depths gloom maps). hos_scan_pieces:
   where else in guest memory some bytes are (copies of data the game may keep), the first place found guarded. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

int hos_thread_handles(HANDLE *out, const char **names, int max);   /* hos_thread.c */
uint8_t *hos_guest_base(void);                                      /* hos_main.c */
uint64_t hos_guest_size(void);
void hos_backtrace(void);                                           /* hos_prof.c */

static struct { uint64_t guest; int len; char why[48]; } slot[4];
static volatile LONG nslots;
static SRWLOCK lock = SRWLOCK_INIT;

typedef struct { DWORD64 rip; int slot; volatile LONG hits, reported; } Site;
static Site sites[256];
static volatile LONG nsites, total_hits, new_hits;

/* The registers every thread should have now: also put back into the context of each hit (handler), since a
   thread stopped inside the handler while apply_all set its registers would otherwise get the old ones back from the
   exception's context when it continues (the second run: a breakpoint added while the main thread kept hitting the
   first one never fired on it). */
static volatile DWORD64 want_dr[4], want_dr7;
static void apply_all(void) {
    HANDLE th[256];
    const char *names[256];
    const int n = hos_thread_handles(th, names, 256);
    DWORD64 dr7 = 0;
    DWORD64 dr[4] = {0};
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < nslots && i < 4; i++) {
        dr[i] = (DWORD64)(uintptr_t)(hos_guest_base() + slot[i].guest);
        const int lenbits = slot[i].len == 8 ? 2 : slot[i].len == 4 ? 3 : slot[i].len == 2 ? 1 : 0;
        dr7 |= 1ull << (i * 2);                       /* local enable */
        dr7 |= 3ull << (16 + i * 4);                  /* read or write */
        dr7 |= (DWORD64)lenbits << (18 + i * 4);
    }
    ReleaseSRWLockShared(&lock);
    for (int i = 0; i < 4; i++) want_dr[i] = dr[i];
    want_dr7 = dr7;
    int done = 0;
    for (int i = 0; i < n; i++) {
        if (GetThreadId(th[i]) == GetCurrentThreadId()) continue;
        if (SuspendThread(th[i]) == (DWORD)-1) continue;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th[i], &c)) {
            c.Dr0 = dr[0], c.Dr1 = dr[1], c.Dr2 = dr[2], c.Dr3 = dr[3];
            c.Dr7 = dr7;
            c.Dr6 = 0;
            done += SetThreadContext(th[i], &c) != 0;
        }
        ResumeThread(th[i]);
    }
    hos_log("watch: %ld breakpoint%s on %d of %d game threads", (long)nslots, nslots == 1 ? "" : "s", done, n);
}

static const char *symbolize(DWORD64 pc, char *out, size_t cap) {
    static int ready;
    HANDLE proc = GetCurrentProcess();
    if (!ready) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        char path[MAX_PATH + 8] = ".;";
        GetModuleFileNameA(NULL, path + 2, MAX_PATH);
        char *s = strrchr(path, '\\');
        if (s) *s = 0;
        SymInitialize(proc, path, TRUE);
        ready = 1;
    }
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    memset(buf, 0, sizeof buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    if (SymFromAddr(proc, pc, &disp, sym)) snprintf(out, cap, "%s+%#llx", sym->Name, (unsigned long long)disp);
    else snprintf(out, cap, "%#llx", (unsigned long long)pc);
    return out;
}

/* Guard pages (hos_guard_arm): every access to a whole range, by any thread, through PAGE_GUARD. A hit is recorded
   (by host code address, as breakpoints are: sites[].slot 4 + the range), the thread single-steps the access, and
   the page is guarded again on the step. The first 400 hits are logged with the offset in the range, read or write,
   the thread and hos_guard_context's note (the renderer's camera), up to 150 a range. A range stops being guarded
   after 20000 hits. A range armed once (hos_guard_arm_once) is not guarded again: each of its pages tells once. */
typedef struct { uint64_t guest, len; DWORD protect; int once; char why[48]; volatile LONG hits, lines; } Guard;
static Guard guards[4];
static volatile LONG nguards;
static __declspec(thread) uintptr_t rearm_page;
static __declspec(thread) int rearm_guard;
void (*hos_guard_context)(char *out, size_t cap);
static int guard_of(uintptr_t a, uint64_t *off) {
    for (LONG i = 0; i < nguards && i < 4; i++) {
        const uintptr_t h = (uintptr_t)(hos_guest_base() + guards[i].guest);
        if (a >= h && a < h + guards[i].len) {
            *off = a - h;
            return (int)i;
        }
    }
    return -1;
}

static LONG CALLBACK handler(EXCEPTION_POINTERS *e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        const uintptr_t a = (uintptr_t)e->ExceptionRecord->ExceptionInformation[1];
        uint64_t off = 0;
        const int g = guard_of(a, &off);
        if (g < 0) return EXCEPTION_CONTINUE_SEARCH;
        CONTEXT *c = e->ContextRecord;
        const DWORD64 rip = c->Rip;
        HosThread *t = hos_self();
        /* (the game's threads' hits count toward the limit; host threads', the renderer reading what it decodes,
           are logged 16 times in all and never stop the guard) */
        static volatile LONG host_lines;
        const LONG n = t ? InterlockedIncrement(&guards[g].hits) : 0;
        const int log_it = t ? 1 : InterlockedIncrement(&host_lines) <= 16;
        InterlockedIncrement(&total_hits);
        InterlockedIncrement(&new_hits);
        Site *site = NULL;
        AcquireSRWLockExclusive(&lock);
        for (LONG i = 0; i < nsites && !site; i++)
            if (sites[i].rip == rip && sites[i].slot == 4 + g) site = &sites[i];
        if (!site && nsites < 256) {
            site = &sites[nsites];
            site->rip = rip;
            site->slot = 4 + g;
            InterlockedIncrement(&nsites);
        }
        ReleaseSRWLockExclusive(&lock);
        if (site) InterlockedIncrement(&site->hits);
        static volatile LONG lines;
        static SRWLOCK sym_lock = SRWLOCK_INIT;   /* (dbghelp is not thread safe) */
        if (log_it && InterlockedIncrement(&lines) <= 600 && InterlockedIncrement(&guards[g].lines) <= 150) {
            AcquireSRWLockExclusive(&sym_lock);
            char name[300], note[160] = "";
            if (hos_guard_context) hos_guard_context(note, sizeof note);
            const ULONG_PTR rw = e->ExceptionRecord->ExceptionInformation[0];
            hos_log("guard: %s +%#llx %s by %s on thread %s%s%s", guards[g].why, (unsigned long long)off,
                    rw == 1 ? "written" : rw == 8 ? "executed" : "read", symbolize(rip, name, sizeof name),
                    t ? hos_thread_name(t) : "(host)", *note ? ", " : "", note);
            if (site && site->reported++ < 1) {
                hos_log_flush();
                hos_backtrace();
            }
            ReleaseSRWLockExclusive(&sym_lock);
        }
        if (guards[g].once) {
        } else if (n < 20000) {   /* (guarded again after this one instruction) */
            rearm_page = a & ~(uintptr_t)0xFFF;
            rearm_guard = g;
            c->EFlags |= 0x100;
        } else if (n == 20000) {
            hos_log("guard: %s: 20000 hits, no longer guarded", guards[g].why);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    if (rearm_page) {   /* (the step after a guard hit) */
        DWORD old;
        VirtualProtect((void *)rearm_page, 0x1000, guards[rearm_guard].protect | PAGE_GUARD, &old);
        rearm_page = 0;
        c->EFlags &= ~(DWORD)0x100;
        if (!(c->Dr6 & 0xF)) return EXCEPTION_CONTINUE_EXECUTION;
    }
    const DWORD64 hit = c->Dr6 & 0xF;
    if (!hit) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    c->Dr0 = want_dr[0], c->Dr1 = want_dr[1], c->Dr2 = want_dr[2], c->Dr3 = want_dr[3];
    c->Dr7 = want_dr7;
    int s = 0;
    while (s < 3 && !(hit >> s & 1)) s++;
    const DWORD64 rip = c->Rip;
    InterlockedIncrement(&total_hits);
    InterlockedIncrement(&new_hits);
    Site *site = NULL;
    for (LONG i = 0; i < nsites && !site; i++)
        if (sites[i].rip == rip && sites[i].slot == s) site = &sites[i];
    if (!site && nsites < 256) {
        AcquireSRWLockExclusive(&lock);
        for (LONG i = 0; i < nsites && !site; i++)
            if (sites[i].rip == rip && sites[i].slot == s) site = &sites[i];
        if (!site && nsites < 256) {
            site = &sites[nsites];
            site->rip = rip;
            site->slot = s;
            InterlockedIncrement(&nsites);
        }
        ReleaseSRWLockExclusive(&lock);
    }
    if (!site) return EXCEPTION_CONTINUE_EXECUTION;
    InterlockedIncrement(&site->hits);
    if (InterlockedIncrement(&site->reported) <= 2) {
        char name[300];
        HosThread *t = hos_self();
        /* (no read of the watched bytes here: that is a hit of its own, in this handler) */
        hos_log("watch: %s (%#llx) touched by %s on thread %s", slot[s].why, (unsigned long long)slot[s].guest,
                symbolize(rip, name, sizeof name), t ? hos_thread_name(t) : "?");
        hos_log_flush();
        hos_backtrace();
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI summary_main(void *p) {
    (void)p;
    for (;;) {
        Sleep(5000);
        if (!InterlockedExchange(&new_hits, 0)) continue;
        char line[4096];
        int len = snprintf(line, sizeof line, "watch: %ld hits so far;", (long)total_hits);
        for (LONG i = 0; i < nsites && len < (int)sizeof line - 200; i++) {
            char name[300];
            len += snprintf(line + len, sizeof line - len, " [%d] %s x%ld;", sites[i].slot,
                            symbolize(sites[i].rip, name, sizeof name), (long)sites[i].hits);
        }
        hos_log("%s", line);
    }
    return 0;
}

static DWORD WINAPI arm_main(void *p) {
    (void)p;
    apply_all();
    return 0;
}

volatile int hos_watch_on;   /* a breakpoint is armed: the game's memcpy / memmove report to hos_watch_copy */

void hos_watch_arm(uint64_t guest, int len, const char *why) {
    static int started;
    if (len != 1 && len != 2 && len != 4 && len != 8) len = 8;
    guest &= ~(uint64_t)(len - 1);   /* (a breakpoint covers an aligned range) */
    AcquireSRWLockExclusive(&lock);
    for (LONG i = 0; i < nslots && i < 4; i++)
        if (slot[i].guest == guest && slot[i].len == len) {   /* (watched already) */
            ReleaseSRWLockExclusive(&lock);
            return;
        }
    if (nslots >= 4) {
        ReleaseSRWLockExclusive(&lock);
        hos_log("watch: all 4 breakpoints in use; %s not watched", why);
        return;
    }
    slot[nslots].guest = guest;
    slot[nslots].len = len;
    snprintf(slot[nslots].why, sizeof slot[nslots].why, "%s", why);
    InterlockedIncrement(&nslots);
    ReleaseSRWLockExclusive(&lock);
    if (!started) {
        started = 1;
        AddVectoredExceptionHandler(1, handler);
        CreateThread(NULL, 0, summary_main, NULL, 0, NULL);
    }
    hos_watch_on = 1;
    hos_log("watch: %s at guest %#llx (%d bytes)", why, (unsigned long long)guest, len);
    /* (from another thread: a thread cannot set its own context while it runs) */
    HANDLE h = CreateThread(NULL, 0, arm_main, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

/* Every access to [guest, guest + len) from now on, by any thread (guard pages: the handler above). Up to 4 ranges;
   the same range again is ignored. */
static void guard_arm(uint64_t guest, uint64_t len, const char *why, int once) {
    static int started;
    const uint64_t lo = guest & ~0xFFFull, hi = (guest + len + 0xFFF) & ~0xFFFull;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(hos_guest_base() + lo, &mi, sizeof mi) || mi.State != MEM_COMMIT) {
        hos_log("guard: %s at guest %#llx: not committed memory, not guarded", why, (unsigned long long)guest);
        return;
    }
    AcquireSRWLockExclusive(&lock);
    for (LONG i = 0; i < nguards && i < 4; i++)
        if (guards[i].guest == lo && guards[i].len == hi - lo) {
            ReleaseSRWLockExclusive(&lock);
            return;
        }
    if (nguards >= 4) {
        ReleaseSRWLockExclusive(&lock);
        hos_log("guard: all 4 ranges in use; %s not guarded", why);
        return;
    }
    Guard *g = &guards[nguards];
    g->guest = lo, g->len = hi - lo, g->protect = mi.Protect & 0xFF, g->hits = 0, g->lines = 0, g->once = once;
    snprintf(g->why, sizeof g->why, "%s", why);
    InterlockedIncrement(&nguards);
    ReleaseSRWLockExclusive(&lock);
    if (!started) {
        started = 1;
        AddVectoredExceptionHandler(1, handler);
        CreateThread(NULL, 0, summary_main, NULL, 0, NULL);
    }
    DWORD old;
    const BOOL ok = VirtualProtect(hos_guest_base() + lo, (SIZE_T)(hi - lo), g->protect | PAGE_GUARD, &old);
    hos_log("guard: %s at guest %#llx..%#llx%s%s", why, (unsigned long long)lo, (unsigned long long)hi,
            once ? " (each page once)" : "", ok ? "" : " (VirtualProtect failed: not guarded)");
}
void hos_guard_arm(uint64_t guest, uint64_t len, const char *why) { guard_arm(guest, len, why, 0); }
void hos_guard_arm_once(uint64_t guest, uint64_t len, const char *why) { guard_arm(guest, len, why, 1); }
/* RX_GUARD_GLOOM's control (set by the renderer when it guards the gloom maps): the next InvalidateMappedRange over
   render target memory (the game about to read what the GPU drew there: the terrain's tiles) is guarded once, from
   the game's own thread right before its reads (nvn_native.c), so the log shows the guard catching the game's code */
volatile int hos_guard_control_pending;

/* Where else in guest memory n pieces of `len` bytes are (copies of some data the game may keep for itself), after
   delay_s seconds, on a thread of its own: each piece is found by an 8-byte anchor at a 16-byte aligned offset in it
   (the first one whose bytes are not all alike), every 16-byte aligned place of committed, readable memory compared
   (read with ReadProcessMemory 4 MB at a time, so memory freed meanwhile is no fault), [skip, skip + skip_len) left out.
   Each place found is logged; the first one is guarded (2 MB from its 64 KB, hos_guard_arm) as guard_why. */
typedef struct { uint8_t *bytes; uint32_t len, anchor; uint64_t anchor_value; char name[64]; } Piece;
typedef struct { Piece *p; int n; uint64_t skip, skip_len; int delay_s; char why[48]; } Scan;
static DWORD WINAPI scan_main(void *arg) {
    Scan *s = (Scan *)arg;
    Sleep((DWORD)s->delay_s * 1000);
    const int64_t t0 = hos_now_ns();
    uint8_t *base = hos_guest_base();
    uint64_t space = hos_guest_size();
    if (space > (1ull << 33)) space = 1ull << 33;
    enum { CHUNK = 4 << 20, OVERLAP = 4096 };
    uint8_t *buf = (uint8_t *)malloc(CHUNK + OVERLAP);
    int found = 0;
    uint64_t guarded = 0, scanned = 0;
    for (uint64_t at = 0x10000; buf && at < space;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(base + at, &mi, sizeof mi)) break;
        const uint64_t lo = (uint64_t)((uint8_t *)mi.BaseAddress - base), hi = lo + mi.RegionSize;
        at = hi;
        const DWORD p = mi.Protect;
        if (mi.State != MEM_COMMIT || (p & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(p & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
            continue;
        for (uint64_t c = lo; c < hi; c += CHUNK) {
            const uint64_t want = hi - c < CHUNK + OVERLAP ? hi - c : CHUNK + OVERLAP;
            if (c < s->skip + s->skip_len && s->skip < c + want) continue;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), base + c, buf, (SIZE_T)want, &got) || got < 16) continue;
            scanned += got;
            for (uint64_t o = 0; o + 8 <= got && o < CHUNK; o += 16) {
                uint64_t v;
                memcpy(&v, buf + o, 8);
                for (int k = 0; k < s->n; k++) {
                    const Piece *pc = &s->p[k];
                    if (v != pc->anchor_value || o < pc->anchor) continue;
                    const uint64_t start = o - pc->anchor;
                    if (start + pc->len > got || memcmp(buf + start, pc->bytes, pc->len)) continue;
                    const uint64_t g = c + start;
                    if (found++ < 64)
                        hos_log("scan: %s found at guest %#llx (region %#llx..%#llx, protect %#lx)", pc->name,
                                (unsigned long long)g, (unsigned long long)lo, (unsigned long long)hi, (unsigned long)p);
                    if (!guarded) {
                        guarded = g & ~0xFFFFull;
                        hos_guard_arm(guarded, 2u << 20, s->why);
                    }
                }
            }
        }
    }
    hos_log("scan: %d places hold the %d pieces looked for; %llu MB scanned in %.1f s", found, s->n,
            (unsigned long long)(scanned >> 20), (double)(hos_now_ns() - t0) / 1e9);
    free(buf);
    for (int k = 0; k < s->n; k++) free(s->p[k].bytes);
    free(s->p);
    free(s);
    return 0;
}
void hos_scan_pieces(const uint8_t *const *pieces, const char *const *names, int n, uint32_t len, uint64_t skip,
                     uint64_t skip_len, int delay_s, const char *guard_why) {
    Scan *s = (Scan *)calloc(1, sizeof *s);
    s->p = (Piece *)calloc((size_t)(n > 0 ? n : 1), sizeof *s->p);
    for (int i = 0; i < n; i++) {
        Piece *pc = &s->p[s->n];
        int ok = 0;
        for (uint32_t a = 0; a + 8 <= len && !ok; a += 16) {
            int varied = 0;
            for (int j = 1; j < 8 && !varied; j++) varied = pieces[i][a + j] != pieces[i][a];
            if (varied) pc->anchor = a, memcpy(&pc->anchor_value, pieces[i] + a, 8), ok = 1;
        }
        if (!ok) continue;   /* (all alike: it would be found everywhere) */
        pc->bytes = (uint8_t *)malloc(len);
        memcpy(pc->bytes, pieces[i], len);
        pc->len = len;
        snprintf(pc->name, sizeof pc->name, "%s", names[i]);
        s->n++;
    }
    s->skip = skip, s->skip_len = skip_len, s->delay_s = delay_s;
    snprintf(s->why, sizeof s->why, "%s", guard_why);
    hos_log("scan: looking for %d pieces of %u bytes in %d s", s->n, len, delay_s);
    HANDLE h = CreateThread(NULL, 0, scan_main, s, 0, NULL);
    if (h) CloseHandle(h);
}

/* The game's memcpy / memmove (nn_libc.c), after the copy: one that filled a watched address says where from (the
   data the game copied there, e.g. a texture from the buffer it was loaded into); with HOS_WATCH_FOLLOW=1 the
   source's matching bytes are watched from then on too (a copy the game may keep and read itself) */
void hos_watch_copy(uint64_t dst, uint64_t src, uint64_t n, uint64_t lr) {
    static int follow = -1;
    if (follow < 0) follow = getenv("HOS_WATCH_FOLLOW") && *getenv("HOS_WATCH_FOLLOW") != '0';
    for (LONG i = 0; i < nslots && i < 4; i++) {
        const uint64_t g = slot[i].guest;
        if (g < dst || g >= dst + n) continue;
        const uint64_t s = src + (g - dst);
        static volatile LONG lines;
        if (InterlockedIncrement(&lines) <= 40) {
            HosThread *t = hos_self();
            hos_log("watch: %s (%#llx) filled by a copy of %#llx bytes from %#llx (to %#llx); its bytes come from %#llx; "
                    "thread %s, lr %#llx", slot[i].why, (unsigned long long)g, (unsigned long long)n,
                    (unsigned long long)src, (unsigned long long)dst, (unsigned long long)s,
                    t ? hos_thread_name(t) : "?", (unsigned long long)lr);
        }
        if (follow && strncmp(slot[i].why, "copy source", 11)) {
            char why[48];
            snprintf(why, sizeof why, "copy source of %llx", (unsigned long long)g);
            hos_watch_arm(s, slot[i].len, why);
        }
    }
    /* (and copied out of a watched address: the data moving elsewhere, e.g. a texture the texture manager moves) */
    for (LONG i = 0; i < nslots && i < 4; i++) {
        const uint64_t g = slot[i].guest;
        if (g < src || g >= src + n) continue;
        static volatile LONG lines;
        if (InterlockedIncrement(&lines) <= 40) {
            HosThread *t = hos_self();
            hos_log("watch: %s (%#llx) copied out: %#llx bytes from %#llx to %#llx, its bytes to %#llx; thread %s, lr %#llx",
                    slot[i].why, (unsigned long long)g, (unsigned long long)n, (unsigned long long)src,
                    (unsigned long long)dst, (unsigned long long)(dst + (g - src)), t ? hos_thread_name(t) : "?",
                    (unsigned long long)lr);
        }
    }
}

static DWORD WINAPI env_main(void *p) {
    (void)p;
    const char *after = getenv("HOS_WATCH_AFTER");
    Sleep((DWORD)((after ? atof(after) : 30.0) * 1000));
    char spec[512];
    snprintf(spec, sizeof spec, "%s", getenv("HOS_WATCH"));
    for (char *tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
        char *end;
        const uint64_t a = strtoull(tok, &end, 16);
        const int len = *end == ':' ? atoi(end + 1) : 8;
        if (a) hos_watch_arm(a, len, "HOS_WATCH");
    }
    return 0;
}

/* (called at start, hos_main.c) */
/* A watched address moved (the object it is in was made again, e.g. on loading a save): the breakpoint follows */
void hos_watch_move(const char *why, uint64_t guest) {
    AcquireSRWLockExclusive(&lock);
    int found = 0;
    for (int i = 0; i < nslots && i < 4; i++)
        if (!strcmp(slot[i].why, why)) {
            slot[i].guest = guest & ~(uint64_t)(slot[i].len - 1);
            found = 1;
        }
    ReleaseSRWLockExclusive(&lock);
    if (!found) return;
    static uint64_t last;
    if (guest != last) hos_log("watch: %s moved to guest %#llx", why, (unsigned long long)guest);
    last = guest;
    HANDLE h = CreateThread(NULL, 0, arm_main, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

/* A game thread made suspended, before it runs (hos_thread_start): the breakpoints armed so far go on it too */
void hos_watch_thread(void *thread) {
    if (!want_dr7) return;
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext((HANDLE)thread, &c)) return;
    c.Dr0 = want_dr[0], c.Dr1 = want_dr[1], c.Dr2 = want_dr[2], c.Dr3 = want_dr[3];
    c.Dr7 = want_dr7;
    c.Dr6 = 0;
    SetThreadContext((HANDLE)thread, &c);
}

void hos_watch_start(void) {
    if (!getenv("HOS_WATCH")) return;
    HANDLE h = CreateThread(NULL, 0, env_main, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
