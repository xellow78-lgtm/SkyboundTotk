/* Debugging: who reads or writes a guest address. x64 hardware data breakpoints (DR0-DR3) on every guest thread;
   each hit is counted by the host code address that made it (a lifted function shows as f_XXXXXXXX, the guest
   code at XXXXXXXX), the first hits of each with a backtrace, and a summary every 5 s while there are new hits.

   HOS_WATCH=<guest address>[:<1|2|4|8 bytes>][,...] (up to 4) arms at start, after HOS_WATCH_AFTER=<s> (default 30);
   code that finds an address at run time arms it with hos_watch_arm (e.g. totk_watch.c). A breakpoint goes on the
   threads that exist when it is armed, and on the game's threads started after that (hos_watch_thread, from
   hos_thread_start), so HOS_WATCH_AFTER=0 sees the loading too. A memcpy / memmove of the game's into a watched
   address logs where the bytes came from; HOS_WATCH_FOLLOW=1 then watches those source bytes too (free slots
   only). Needs the PDB next to the exe for names. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

int hos_thread_handles(HANDLE *out, const char **names, int max);   /* hos_thread.c */
uint8_t *hos_guest_base(void);                                      /* hos_main.c */
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

static LONG CALLBACK handler(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
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
