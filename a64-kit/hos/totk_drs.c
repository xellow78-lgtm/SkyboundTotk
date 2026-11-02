/* TOTK's dynamic resolution (v1.4.3), a probe: HOS_DRS_PROBE=1.

   What is known: the scene targets stay 1600x896 and the game draws into a smaller part of them when the GPU is busy
   (1432x808, 1280x720, 1200x672, 960x540: HOS_LOG_VIEWPORT). The levels come from the game's parameters
   (game::gfx::DynamicResolutionEachLevelParam: StressThresh, ResolutionReduceScale at +0x38, EnableSuperResolution,
   EnableFXAA, RecoverStress, Priority; their registration is at 0x09ead7e0).

   f_08006a44 is sead's Viewport::apply(context, LogicalFrameBuffer): x0 the viewport (its box at +8, posture at
   +0x18), x2 the frame buffer (virtual size at +8, physical area min at +0x10 and max at +0x18), and it maps the
   viewport from the virtual size into the physical area. So dynamic resolution shrinks the frame buffers' physical
   areas. The probe logs each frame buffer the first time it is used and each time its physical area changes, and
   puts hardware write breakpoints (hos_watch.c) on the first shrunk one's area: whoever writes it is the dynamic
   resolution, with a backtrace. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if GAME_VERSION == 143
static int drs_probe(void) {
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("HOS_DRS_PROBE");
        m = e && *e && *e != '0';
    }
    return m;
}

static float gf(uint64_t a) {
    float f;
    memcpy(&f, GP(a), 4);
    return f;
}

ENTRY_HOOK(0x08006a44) {
    if (!drs_probe()) return 0;
    static struct { uint64_t fb; float area[4]; } seen[96];
    static int nseen, lines, armed;
    const uint64_t vp = c->x[0], fb = c->x[2];
    if (fb < 0x8000000) return 0;
    const float vw = gf(fb + 8), vh = gf(fb + 12);
    const float a[4] = {gf(fb + 0x10), gf(fb + 0x14), gf(fb + 0x18), gf(fb + 0x1c)};
    int i = 0;
    while (i < nseen && seen[i].fb != fb) i++;
    const int is_new = i == nseen;
    if (is_new) {
        if (nseen == 96) return 0;
        seen[nseen++].fb = fb;
    }
    if (!is_new && !memcmp(seen[i].area, a, sizeof a)) return 0;
    memcpy(seen[i].area, a, sizeof a);
    if (lines < 600) {
        lines++;
        hos_log("drs: fb %#llx (vtable %#llx) virtual %gx%g physical (%g,%g)-(%g,%g)%s; viewport (%g,%g)-(%g,%g) "
                "posture %u; lr %#llx",
                (unsigned long long)fb, (unsigned long long)G64(fb), vw, vh, a[0], a[1], a[2], a[3],
                is_new ? " (first use)" : " (changed)", gf(vp + 8), gf(vp + 12), gf(vp + 16), gf(vp + 20),
                G32(vp + 0x18), (unsigned long long)c->x[30]);
    }
#ifndef __EMSCRIPTEN__
    /* the first frame buffer seen shrunk (its physical area smaller than at its first use, or than its virtual size) */
    if (!armed && !is_new && a[2] - a[0] > 100 && a[3] - a[1] > 100) {
        armed = 1;
        void hos_watch_arm(uint64_t guest, int len, const char *why);
        hos_watch_arm(fb + 0x10, 8, "drs: frame buffer physical min");
        hos_watch_arm(fb + 0x18, 8, "drs: frame buffer physical max");
        hos_log("drs: watching frame buffer %#llx's physical area", (unsigned long long)fb);
    }
#endif
    return 0;
}
#endif
