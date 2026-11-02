/* TOTK's dynamic resolution (v1.4.3): HOS_DRS_PROBE=1 finds who sets it, HOS_RENDER_SCALE=<s> (experimental) fixes
   the 3D scene's size at s times the full size (1600x896 on the targets; 0.8 = 1280x720, 0.6 = 960x540).

   What is known:
   - The scene targets stay 1600x896; when the GPU is busy the game draws into a smaller part of them (1432x808,
     1280x720, 1200x672, 960x540: HOS_LOG_VIEWPORT). The levels come from the game's parameters
     (game::gfx::DynamicResolutionEachLevelParam: StressThresh, ResolutionReduceScale at +0x38, EnableSuperResolution,
     EnableFXAA, RecoverStress, Priority; registered at 0x09ead7e0).
   - f_08006a44 is sead's Viewport::apply(context, LogicalFrameBuffer): x0 the viewport (box at +8, posture at +0x18),
     x2 the frame buffer (virtual size at +8, physical area at +0x10). The scene's frame buffers are 1280x720 virtual
     on 1600x896 physical and did not change while playing (run_drs_probe.log, 2026-11-02): dynamic resolution shrinks
     the viewport box, not the frame buffer.
   - That box is the scene view's (its +0x3078, an array of 0x3ce0-byte views), set by f_080e1f50 from its x3: a
     viewport built by f_080d5a48 from the box at x2+0x58 (min) / +0x60 (max), x2 being the view (camera) object. The
     same view array's +0x2e00 rect gives the shaders' p_dynamic_resolution_ratio (f_08078840), so a box changed here
     before f_080e1f50 runs reaches what is derived from it in that frame.
   So the probe logs that box at f_080d5a48 and puts hardware write breakpoints (hos_watch.c) on it: whoever writes it
   is the dynamic resolution, with a backtrace. */
#include "hos.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The scene's size as a fraction of the full size (0 = the game's own dynamic resolution), and its modes for a
   1920x1080 output (FSR 2 / DLSS names): HOS_RENDER_SCALE at start (a number, or quality / balanced / performance /
   ultra), F4 to go through them (display.c). The full size is the targets' 1600x896, so nothing above it. */
static const struct { const char *name; float scale; } modes[] = {
    {"the game's own (dynamic, up to 1600x896)", 0},
    {"Quality (1.5x: 1280x720)", 0.8f},
    {"Balanced (1.7x: 1129x635)", 0.70588f},
    {"Performance (2x: 960x540)", 0.6f},
    {"Ultra Performance (3x: 640x360)", 0.4f},
};
volatile float hos_render_scale = -1;
static int mode_now;
static float render_scale(void) {
    if (hos_render_scale < 0) {
        const char *e = getenv("HOS_RENDER_SCALE");
        float s = 0;
        if (e && *e) {
            static const char *const keys[] = {"", "quality", "balanced", "performance", "ultra"};
            char low[16] = {0};
            for (int i = 0; i < 15 && e[i]; i++) low[i] = (char)(e[i] | 0x20);
            for (int i = 1; i < (int)(sizeof keys / sizeof *keys); i++)
                if (!strncmp(low, keys[i], strlen(keys[i]))) s = modes[i].scale, mode_now = i;
            if (!s) s = (float)atof(e);
        }
        hos_render_scale = s > 0.2f && s <= 1 ? s : 0;
        if (hos_render_scale) hos_log("drs: the 3D scene at %.3g of its full size", hos_render_scale);
    }
    return hos_render_scale;
}
/* F4: the next mode; its name */
const char *hos_render_mode_cycle(void) {
    render_scale();
    mode_now = (mode_now + 1) % (int)(sizeof modes / sizeof *modes);
    hos_render_scale = modes[mode_now].scale;
    static char b[96];
    snprintf(b, sizeof b, "3D resolution: %s", modes[mode_now].name);
    hos_log("drs: %s", b);
    return b;
}

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
static void sf(uint64_t a, float f) {
    memcpy(GP(a), &f, 4);
    HOS_WRITTEN(GP(a), 4);
}

/* f_080d5a48(x0, w1 the view's index, x2 the view, x3 ...): the view's box is in the scene frame buffer's virtual
   units, 1280x720 at full size */
ENTRY_HOOK(0x080d5a48) {
    const uint64_t view = c->x[2];
    if (view < 0x8000000) return 0;
    const uint64_t box = view + 0x58;
    const float x0 = gf(box), y0 = gf(box + 4), x1 = gf(box + 8), y1 = gf(box + 12);
    const float s = render_scale();
    /* (the scene's own views: the box from the origin, 16:9, at most the full 1280x720) */
    const int scene = x0 == 0 && y0 == 0 && x1 >= 400 && x1 <= 1280.5f && y1 >= 200 && y1 <= 720.5f &&
                      fabsf(x1 / y1 - 16.0f / 9.0f) < 0.08f;
    if (drs_probe()) {
        static struct { uint64_t view; float b[4]; } seen[32];
        static int nseen, lines, armed;
        int i = 0;
        while (i < nseen && seen[i].view != view) i++;
        const int is_new = i == nseen;
        if (is_new && nseen < 32) seen[nseen++].view = view;
        if (i < 32 && (is_new || seen[i].b[0] != x0 || seen[i].b[1] != y0 || seen[i].b[2] != x1 || seen[i].b[3] != y1)) {
            seen[i].b[0] = x0, seen[i].b[1] = y0, seen[i].b[2] = x1, seen[i].b[3] = y1;
            if (lines < 800) {
                lines++;
                hos_log("drs: view %#llx (index %d) box (%g,%g)-(%g,%g)%s; lr %#llx", (unsigned long long)view,
                        (int)c->x[1], x0, y0, x1, y1, is_new ? " (first)" : " (changed)", (unsigned long long)c->x[30]);
            }
        }
#ifndef __EMSCRIPTEN__
        if (!armed && scene) {
            armed = 1;
            void hos_watch_arm(uint64_t guest, int len, const char *why);
            hos_watch_arm(box, 8, "drs: scene view box min");
            hos_watch_arm(box + 8, 8, "drs: scene view box max");
        }
#endif
    }
    /* (the game may write the box only when its level changes: what it last wrote is kept, and put back when the
       mode goes back to the game's own) */
    static float game_w, game_h, ours_w = -1, ours_h = -1;
    if (scene && (x1 != ours_w || y1 != ours_h)) game_w = x1, game_h = y1;
    if (s && scene) {
        const float w = 1280 * s, h = 720 * s;
        if (x1 != w || y1 != h) sf(box + 8, w), sf(box + 12, h);
        ours_w = w, ours_h = h;
    } else if (!s && scene && ours_w >= 0) {
        if (x1 == ours_w && y1 == ours_h && game_w > 0) sf(box + 8, game_w), sf(box + 12, game_h);
        ours_w = ours_h = -1;
    }
    return 0;
}
#endif
