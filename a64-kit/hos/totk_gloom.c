/* HOS_GLOOM_TRACE=1 (diagnostic): where the game decides an actor stands in gloom ("miasma").

   The Depths' gloom damage did not follow the gloom drawn (on the Mac port, the same game, mods and save, it does). The
   drawn gloom is the gloom pass's two 1000x800 maps and noise; the game's own code never reads those maps, nor keeps a
   copy of them (RX_GUARD_GLOOM: every level guarded, guest memory searched). What the game's code does read is an
   actor's gloom level: main+0x1c3060c returns it from the actor's world info (actor+0x1e8; the level at +0xc, bit 31
   set: not known), unless flag bit 21 at actor+0x30 is set. The BSA condition IsInMiasma (the evaluator main+0x488580,
   case 0x67397ca6) and the AI query QueryWorldInfoIsMiasmaLevel (main+0x2734cf8: level > 1) take it from there; the
   gloom splashes at each of Link's steps where no gloom is drawn come from such conditions.

   So this hook, at that function's entry: the first 8 actors asked about are logged with their world info, every
   change of their level after (300 lines, with the renderer's camera: hos_guard_context), and the level of the first 3
   is watched by a breakpoint (hos_watch.c: read or write), so the code that writes it, the code that decides gloom, is
   logged with its stack. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#if GAME_VERSION == 143
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
void hos_watch_arm(uint64_t guest, int len, const char *why);
extern void (*hos_guard_context)(char *out, size_t cap);   /* (hos_watch.c; the renderer sets it) */
#endif

static inline int trace_on(void) {
    static int v = -1;
    if (v < 0) v = getenv("HOS_GLOOM_TRACE") && atoi(getenv("HOS_GLOOM_TRACE")) > 0;
    return v;
}

ENTRY_HOOK(0x09c3060c) {
#if !defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)c;
    return 0;
#else
    if (!trace_on()) return 0;
    const uint64_t actor = c->x[0];
    if (!actor) return 0;
    const uint64_t info = G64(actor + 0x1e8);
    if ((G32(actor + 0x30) >> 21 & 1) || !info) return 0;
    const int32_t level = (int32_t)G32(info + 0xc);
    static SRWLOCK lock = SRWLOCK_INIT;
    static uint64_t actors[8];
    static int32_t last[8];
    static int n, lines;
    AcquireSRWLockExclusive(&lock);
    int i = 0;
    while (i < n && actors[i] != actor) i++;
    int arm = 0, changed = 0;
    int32_t was = 0;
    if (i == n && n < 8) {
        actors[n] = actor, last[n] = level, n++;
        arm = i < 3;
        hos_log("gloom: actor %d at guest %#llx: world info %#llx, level %d (asked by %#llx)", i,
                (unsigned long long)actor, (unsigned long long)info, level, (unsigned long long)c->x[30]);
    } else if (i < n && last[i] != level) {
        was = last[i], last[i] = level;
        changed = lines++ < 300;
    }
    ReleaseSRWLockExclusive(&lock);
    if (changed) {
        char note[160] = "";
        if (hos_guard_context) hos_guard_context(note, sizeof note);
        hos_log("gloom: actor %d level %d -> %d%s%s", i, was, level, *note ? ", " : "", note);
    }
    if (arm) {
        char why[48];
        snprintf(why, sizeof why, "gloom level of actor %d", i);
        hos_watch_arm(info + 0xc, 4, why);
    }
    return 0;
#endif
}
#endif
