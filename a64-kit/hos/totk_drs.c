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

/* The 3D scene's size: the game's own dynamic resolution, or a share of the game's output (FSR 2 / DLSS names: Quality
   1.5x, Balanced 1.7x, Performance 2x, Ultra Performance 3x, Native 1x). The setting is "render_resolution" in
   input.ini (F1 > Graphics, the launcher; input_bind.c); F4 goes to the next one; HOS_RENDER_SCALE (a number, or
   quality / balanced / performance / ultra) overrides it at start.
   The scene's render targets are the game's 5/6 of the output (totk_res.c: 1600x896 at 1080p, 2133x1200 at 1440p),
   or the whole output when Native was chosen at the start (totk_res.c: the scene splice); a mode draws into that share
   of them (hos_render_scale, the view box below). Native without the scene splice is the targets' full 5/6. */
#include "input_bind.h"
static const struct { const char *name; float share; } modes[] = {
    {"Game's own", 0}, {"Quality", 2.0f / 3.0f}, {"Balanced", 1.0f / 1.7f}, {"Performance", 0.5f},
    {"Ultra Performance", 1.0f / 3.0f}, {"Native (DLAA)", 1.0f},
};
enum { NMODES = sizeof modes / sizeof *modes };
extern unsigned hos_out_w, hos_out_h;   /* totk_res.c */
float hos_res_scene_share(void);
int hos_res_native(void);
volatile float hos_render_scale = -1;
int hos_render_mode_count(void) { return NMODES; }
static float mode_scale(int m) {   /* (of the targets) */
    if (m <= 0 || m >= NMODES) return 0;
    const float s = modes[m].share / hos_res_scene_share();
    return s > 1 ? 1 : s;
}
const char *hos_render_mode_name(int m) {
    static char names[NMODES][64];
    if (m < 0 || m >= NMODES) m = 0;
    const float share = hos_res_scene_share();
    if (m == 0)
        snprintf(names[m], 64, "Game's own (up to %ux%u)", (unsigned)(hos_out_w * share), (unsigned)(hos_out_h * share));
    else if (m == NMODES - 1 && !hos_res_native())
        snprintf(names[m], 64, "Native (DLAA) - at the next start");
    else
        snprintf(names[m], 64, "%s (%ux%u)", modes[m].name, (unsigned)(hos_out_w * modes[m].share + 0.5f),
                 (unsigned)(hos_out_h * modes[m].share + 0.5f));
    return names[m];
}
void hos_render_mode_apply(int m) {
    if (m < 0 || m >= NMODES) m = 0;
    const float s = mode_scale(m);
    if (hos_render_scale != s) hos_log("drs: 3D resolution: %s", hos_render_mode_name(m));
    hos_render_scale = s;
}
/* (the modes need FSR 2: the game's own scale-up draws a smaller scene into a corner of the screen, see below) */
static int fsr2_enabled(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RX_FSR2");
        on = e && *e && *e != '0';
        if (!on) hos_log("drs: the 3D resolution setting needs FSR 2 (RX_FSR2=1, play_portable_fsr2.bat): the game's own");
    }
    return on;
}
static float render_scale(void) {
    if (!fsr2_enabled()) return 0;
    if (hos_render_scale < 0) {
        const char *e = getenv("HOS_RENDER_SCALE");
        if (e && *e) {
            static const char *const keys[] = {"", "quality", "balanced", "performance", "ultra"};
            char low[16] = {0};
            for (int i = 0; i < 15 && e[i]; i++) low[i] = (char)(e[i] | 0x20);
            float s = 0;
            for (int i = 1; i < 5; i++)
                if (!strncmp(low, keys[i], strlen(keys[i]))) s = mode_scale(i);
            if (!s) s = (float)atof(e);
            hos_render_scale = s > 0.2f && s <= 1 ? s : 0;
            hos_log("drs: the 3D scene at %.3g of its full size (HOS_RENDER_SCALE)", hos_render_scale);
        } else {
            Bindings b;
            hos_bind_get(&b);   /* (loads input.ini, and applies its render_resolution) */
            hos_render_mode_apply(b.render_mode);
        }
    }
    return hos_render_scale;
}
/* F4: the next mode, saved; its name */
const char *hos_render_mode_cycle(void) {
    Bindings b;
    hos_bind_get(&b);
    b.render_mode = (b.render_mode + 1) % NMODES;
    hos_bind_set(&b);   /* (applies it) */
    static char t[96];
    snprintf(t, sizeof t, "3D resolution: %s", hos_render_mode_name(b.render_mode));
    return t;
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

/* The game's own dynamic resolution levels (game::gfx::DynamicResolutionEachLevelParam, 0x78 bytes): f_09ead7b4 is
   their reflection (x0 the level: StressThresh at +0x3c, ResolutionReduceScale at +0x38, EnableFXAA +0x40,
   EnableSuperResolution +0x41). They keep working under a fixed 3D resolution and multiply it (run_quality.log,
   2026-11-02: Balanced's 1128x632 went to 1008x568 and 896x504 every few frames, Ultra Performance's 640x360 to
   568x320 and 512x288), which made the lower modes smear. Under a fixed mode, every level's scale is made the full
   level's (HOS_DRS_LOCK=0: not), and put back for the game's own.
   That did nothing (run_quality.log, 15:37): nothing but this file reads these objects while playing, the game keeps
   its own copy. Stopping the GPU time it measures within a frame (gpu/render/rx_nvn.cpp, gpu_ticks) did not hold it
   either (run_quality.log 2026-11-03: Quality still went to 1144x640 / 1024x576), so the levels still come and go.
   With HOS_DRS_PROBE the levels are logged (0.58 .. 1).
   Levels with EnableSuperResolution 0 scale the scene up with another shader (fs 7f5070f0c3f67c3e instead of
   7102057103341499), whose viewport follows the view's box: under a fixed mode it drew the scene into the lower left
   0.8 of the screen, black around it (the black box; frame dump 27966). FSR 2 now replaces both (rx_fsr2.cpp). */
static struct { uint64_t at, vtable; float scale, saved; int locked; } levels[32];
static int nlevels;
ENTRY_HOOK(0x09ead7b4) {
    const uint64_t at = c->x[0];
    if (at < 0x8000000) return 0;
    for (int i = 0; i < nlevels; i++)
        if (levels[i].at == at) return 0;
    if (nlevels < 32) levels[nlevels].at = at, levels[nlevels].vtable = G64(at), nlevels++;
    return 0;
}
static void lock_levels(int lock) {
    static int off = -1, logged;
    if (off < 0) off = getenv("HOS_DRS_LOCK") && *getenv("HOS_DRS_LOCK") == '0';
    if (off || !nlevels) return;
    /* (a level still there: its object unchanged, its scale a fraction) */
    float lo = 2, hi = -1;
    int live = 0;
    for (int i = 0; i < nlevels; i++) {
        if (G64(levels[i].at) != levels[i].vtable) continue;
        const float v = gf(levels[i].at + 0x38);
        if (!(v >= 0 && v <= 1)) continue;
        if (!levels[i].locked) lo = fminf(lo, v), hi = fmaxf(hi, v);
        live++;
    }
    if (!logged && live) {
        logged = 1;
        for (int i = 0; i < nlevels; i++)
            hos_log("drs: level %d at %#llx: ResolutionReduceScale %g, StressThresh %g, FXAA %d, super resolution %d", i,
                    (unsigned long long)levels[i].at, gf(levels[i].at + 0x38), gf(levels[i].at + 0x3c),
                    *(uint8_t *)GP(levels[i].at + 0x40), *(uint8_t *)GP(levels[i].at + 0x41));
    }
    /* the full level's value: 1 if the scales are multipliers, 0 if they are how much to take off */
    static float full = -1;
    if (full < 0 && hi >= lo) {
        full = hi == 1 ? 1.0f : lo == 0 ? 0.0f : -2.0f;
        hos_log("drs: the levels' scales go from %g to %g: %s", lo, hi,
                full == 1 ? "multipliers, locked at 1 under a fixed 3D resolution"
                : full == 0 ? "reductions, locked at 0 under a fixed 3D resolution" : "not understood, left alone");
    }
    if (full < 0) return;
    for (int i = 0; i < nlevels; i++) {
        if (G64(levels[i].at) != levels[i].vtable) continue;
        const float v = gf(levels[i].at + 0x38);
        if (lock && !levels[i].locked && v >= 0 && v <= 1) {
            levels[i].saved = v, levels[i].locked = 1;
            sf(levels[i].at + 0x38, full);
        } else if (!lock && levels[i].locked) {
            if (v == full) sf(levels[i].at + 0x38, levels[i].saved);
            levels[i].locked = 0;
        }
    }
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
        /* The view's pixel rect (its +0x2e00: min x, y, max x, y; what the shaders' p_dynamic_resolution_ratio and
           the targets' size come from): the box times the game's own level. The level objects themselves are only
           read when loaded (run_quality.log 2026-11-02: no reads while playing), so who writes this rect is where the
           level's scale is applied. (f_080e1f50's view: x0+0x18 the views' manager, its +0x10 the views, 0x3ce0
           bytes each, w1 the index) */
        const uint64_t mgr = G64(c->x[0] + 0x18), views = mgr >= 0x8000000 ? G64(mgr + 0x10) : 0;
        if (views >= 0x8000000 && scene) {
            const uint64_t rect = views + (uint64_t)(uint32_t)c->x[1] * 0x3ce0 + 0x2e00;
            static float last[4];
            float r[4];
            memcpy(r, GP(rect), 16);
            if (memcmp(r, last, 16) && lines < 800) {
                lines++;
                hos_log("drs: view %d pixel rect (%g,%g)-(%g,%g) at %#llx", (int)c->x[1], r[0], r[1], r[2], r[3],
                        (unsigned long long)rect);
                memcpy(last, r, 16);
            }
#ifndef __EMSCRIPTEN__
            if (!armed) {
                armed = 1;
                void hos_watch_arm(uint64_t guest, int len, const char *why);
                hos_watch_arm(rect, 8, "drs: view pixel rect min");
                hos_watch_arm(rect + 8, 8, "drs: view pixel rect max");
            }
#endif
        }
    }
    /* (the game may write the box only when its level changes: what it last wrote is kept, and put back when the
       mode goes back to the game's own) */
    if (scene && drs_probe()) lock_levels(0);   /* (only logs them now: see above) */
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
