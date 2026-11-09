/* Profile-guided builds, the training half (a64-kit/build.py, RX_PGO=gen): an instrumented build counts how often each
   branch of the lifted game code and the renderer runs, and compiler-rt's profile runtime (third_party/llvm-profile)
   writes the counts to <exe folder>/pgo/skybound-<build signature>.profraw. %m in the name merges: each write adds to
   the file, so the counters are written and zeroed every minute (a session that ends badly keeps all but its last
   minute) and once more when the window closes. The game ends with TerminateProcess (display.c, hos_svc.c), which
   skips the runtime's own write at exit. (The same scheme as valentderah/totk-pc-recomp's runtime/platform/win32.c,
   MIT.) Without RX_PGO_GEN both calls do nothing. */
#include "hos.h"
#include <stdio.h>
#include <string.h>
#if defined(RX_PGO_GEN) && !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int __llvm_profile_write_file(void);
int __llvm_profile_dump(void);
void __llvm_profile_reset_counters(void);
void __llvm_profile_set_filename(const char *name);

static SRWLOCK pgo_lock = SRWLOCK_INIT;
static int pgo_done;

static DWORD WINAPI pgo_flush(void *arg) {
    (void)arg;
    for (int n = 1;; n++) {
        Sleep(60 * 1000);
        AcquireSRWLockExclusive(&pgo_lock);
        if (pgo_done) {
            ReleaseSRWLockExclusive(&pgo_lock);
            return 0;
        }
        const int64_t t0 = hos_now_ns();
        const int r = __llvm_profile_write_file();
        if (r == 0) __llvm_profile_reset_counters();   /* (counts taken during the write are lost: a few, not a minute) */
        ReleaseSRWLockExclusive(&pgo_lock);
        if (n == 1 || r)
            hos_log("pgo: counters %s (%.1f s)", r ? "NOT written" : "written", (hos_now_ns() - t0) / 1e9);
    }
}

void hos_pgo_start(void) {
    static char path[MAX_PATH + 64];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *cut = NULL;
    for (DWORD i = 0; i < n; i++)
        if (path[i] == '\\' || path[i] == '/') cut = path + i;
    if (!cut) {
        hos_log("pgo: no exe folder: the counters stay in the working folder (default_%%m.profraw)");
        return;
    }
    const char sep = *cut;
    snprintf(cut + 1, sizeof path - (size_t)(cut + 1 - path), "pgo");
    CreateDirectoryA(path, NULL);
    const size_t len = strlen(path);
    snprintf(path + len, sizeof path - len, "%cskybound-%%m.profraw", sep);
    __llvm_profile_set_filename(path);
    hos_log("pgo: instrumented build (RX_PGO=gen): counters go to %s every minute and when the window closes", path);
    HANDLE h = CreateThread(NULL, 0, pgo_flush, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

void hos_pgo_dump(void) {
    AcquireSRWLockExclusive(&pgo_lock);
    if (!pgo_done) {
        pgo_done = 1;
        const int64_t t0 = hos_now_ns();
        const int r = __llvm_profile_dump();
        hos_log("pgo: last counters %s (%.1f s)", r ? "NOT written" : "written", (hos_now_ns() - t0) / 1e9);
        hos_log_flush();
    }
    ReleaseSRWLockExclusive(&pgo_lock);
}
#else
void hos_pgo_start(void) {}
void hos_pgo_dump(void) {}
#endif
