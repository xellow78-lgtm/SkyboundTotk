/* TOTK at other aspect ratios: aspect_ratio in input.ini (F1 > Graphics, the launcher; at the start): 0 16:9 (the
   game's own), 1 the display's shape, 2 16:10, 3 21:9, 4 32:9. HOS_ASPECT=<w>:<h>, <w>x<h> or a ratio overrides it.
   The game's cameras take that aspect instead of 16:9, and the present (gpu/render/rx_present_fx.cpp) shows the
   (still 16:9) frame at that shape: wider than 16:9 more of the world is seen at the sides; narrower (16:10) the
   vertical view stays and a little less is seen at the sides. Each pixel of the frame is shown a little wider (or
   taller) than it is high. The HUD's layouts are laid out for the shape (totk_ultrawide_ui.c), so it keeps its
   proportions. Fixed for the session: the layouts are read at the start.

   The cameras: each place that hands an aspect ratio to the perspective projection (sead's +0xb0) gets the ratio
   where the value is produced.
   - v1.0.0: the two gameplay cameras' width / height (fdiv s0, s0, s1 stored at camera + 176: main+0x1968c2c and
     main+0x1e5eae8) and the one 16:9 constant (1.7777780, main+0x377ac54): the places the TOTK-Ultrawide tool
     (github.com/Fruithapje21/TOTK-Ultrawide) patches for 1.0.0.
   - v1.4.3: four sites, from MacNativeGames' games/totk/runtime/game_patches_v143.c and aspect_patch.h (MIT,
     Copyright (c) 2026 MacNativeGames contributors), found there with a watchpoint on +0xb0's copies: the camera
     update (ldr s0, [x19, #0x140] at main+0x628c74) and setup (ldr s0, [x22, #0x68] at main+0xb43460) and the two
     viewport divisions (fdiv s0, s0, s1 at main+0x1ff04f4 and +0x253fbb0); here splices on the lifted code after
     each. And the 16:9 constant at main+0x2d06910 (of the two 16:9 floats in its .rodata, the one loaded once with
     an add, as v1.0.0's is).
   Not adjusted (as in MacNativeGames): HUD markers that follow objects in the world sit a little off. */
#include "hos.h"
#include "input_bind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

float hos_view_aspect;   /* the cameras' aspect: 0 the game's own 16:9 */

static uint32_t aspect_bits(void) {
    uint32_t b;
    memcpy(&b, &hos_view_aspect, 4);
    return b;
}

/* the value just put in s0: the aspect ratio */
static void camera_aspect(Ctx *c) {
    if (hos_view_aspect > 0) c->vreg[0][0] = aspect_bits(), c->vreg[0][1] = 0;
}
#if GAME_VERSION == 100
SPLICE_AFTER(0x09968b68, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
SPLICE_AFTER(0x09e5ea24, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
#define ASPECT_CONSTANT 0x0B77AC54ULL   /* main+0x377ac54 */
#elif GAME_VERSION == 143
SPLICE_AFTER(0x08628460, "recomp_load32(c,(c->x[19]+320ULL))") { camera_aspect(c); }   /* main+0x628c74: update */
SPLICE_AFTER(0x08b432bc, "recomp_load32(c,(c->x[22]+104ULL))") { camera_aspect(c); }   /* main+0xb43460: setup */
SPLICE_AFTER(0x09ff0404, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }          /* main+0x1ff04f4 */
SPLICE_AFTER(0x0a53fac0, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }          /* main+0x253fbb0 */
#define ASPECT_CONSTANT 0x0AD06910ULL   /* main+0x2d06910 */
#endif

/* the 16:9 constant follows hos_view_aspect */
void hos_ultrawide_apply(void) {
#ifdef ASPECT_CONSTANT
    static uint32_t original;
    if (!original) original = G32(ASPECT_CONSTANT);
    if (original != 0x3FE38E3Bu) return;   /* (not 16:9 there: not the build this address is for; left alone) */
    G32(ASPECT_CONSTANT) = hos_view_aspect > 0 ? aspect_bits() : original;
#endif
}

/* "<w>:<h>", "<w>x<h>" or a ratio; 0 if none or out of 0.5 .. 8 */
static double parse_ratio(const char *e) {
    double w = 0, h = 0;
    char rest;
    if ((sscanf(e, "%lf:%lf%c", &w, &h, &rest) == 2 || sscanf(e, "%lfx%lf%c", &w, &h, &rest) == 2) && w > 0 && h > 0)
        w /= h;
    else if (sscanf(e, "%lf%c", &w, &rest) != 1)
        return 0;
    return w >= 0.5 && w <= 8 ? w : 0;
}

/* hos_main.c, before the game starts (after the bindings and mods are known): the aspect for the session */
void hos_aspect_init(void) {
    Bindings b;
    hos_bind_get(&b);
    double a = 0;
    const char *what = "aspect_ratio";
    const char *e = getenv("HOS_ASPECT");
    if (e && *e) {
        a = parse_ratio(e), what = "HOS_ASPECT";
        if (!a && strcmp(e, "0")) hos_log("aspect: HOS_ASPECT=%s is not <w>:<h>, <w>x<h> or a ratio in 0.5..8: 16:9", e);
    } else if (b.aspect == 1) {
#ifdef _WIN32
        DEVMODEA dm = {0};
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight) {
            a = (double)dm.dmPelsWidth / dm.dmPelsHeight;
            hos_log("aspect: the display is %lux%lu", dm.dmPelsWidth, dm.dmPelsHeight);
        }
#endif
    } else if (b.aspect == 2) {
        a = 16.0 / 10.0;
    } else if (b.aspect == 3) {
        a = 21.0 / 9.0;
    } else if (b.aspect == 4) {
        a = 32.0 / 9.0;
    }
    const double game = 16.0 / 9.0;
    if (a > 0 && a / game > 0.99 && a / game < 1.01) a = 0;   /* (within 1% of 16:9: the game's own) */
#if GAME_VERSION != 143 && GAME_VERSION != 100
    a = 0;
#endif
    hos_view_aspect = (float)a;
    hos_ultrawide_apply();
#ifdef _WIN32
    void hos_ultrawide_ui_prepare(void);   /* totk_ultrawide_ui.c: the HUD's layouts for the shape */
    hos_ultrawide_ui_prepare();
#endif
    if (a > 0)
        hos_log("aspect: %.4f (%s): the cameras see %s; the HUD's layouts follow", a, what,
                a > game ? "more at the sides" : "the same height, a little less at the sides");
    else
        hos_log("aspect: 16:9 (the game's own)");
}
