/* The game's resolution above 1920x1080: the size it composes its frame in (its output: the canvas, the HUD, the
   frames it presents) and the size of its 3D scene. The game renders at those sizes itself, as with the community
   resolution mods; FSR 2 / DLSS (gpu/render/rx_fsr2.cpp) or the game's own last pass scales the scene to the output,
   and the present scales the output to the window.

   Adapted from totk-pc-recomp's runtime/resolution.c and recomp/patches-v143.txt (MIT, Copyright (c) 2026 Valiantsin
   Dzerakh), whose patch sites are MacNativeGames' games/totk/runtime/game_patches_v143.c (MIT, Copyright (c) 2026
   MacNativeGames contributors). Here the sites are splices into the lifted code (hos.h SPLICE_AFTER) instead of
   instruction patches, and the heap table is made by tools/button_icons.py --heapdef.

   The setting: output_resolution in input.ini (F1 > Graphics, the launcher), at the start: 0 1920x1080 (the game's
   own), 1 the display's (the largest 16:9 that fits the main monitor), 2 2560x1440, 3 3840x2160. HOS_OUTPUT_RES=<w>x<h>
   overrides it. Shapes other than 16:9 wait for the ultrawide work (the camera and the HUD's layouts).

   TOTK 1.4.3:
   - the swapchain size nnMain builds (0x438_00000780 in x8, f_08bdb440) and the display configuration's canvas (+0x10)
     and active output (+0x508, f_083dc9a8) take the output size;
   - the scene: every path that picks the scene's size meets at main+0x585e48 (f_08585da0), which stores it as floats at
     the renderer's +0xcc4 / +0xcc8. The game then scales that by output width / 1920, so its own 5/6 (1600x900 at
     1080p) follows the output without help: the scene splice is on only for Native (render_resolution 5 at the start:
     the scene at the output's size, for DLAA), with the wanted size divided by that factor.
   - the heap: agl's DynamicTextureAllocatorMem (113664 KiB, carved from Graphics, 201728 KiB) in
     System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs holds the output- and scene-sized render targets; at 2560x1440
     the shipped size runs out at the title screen. Both grow by the shipped size times the output's pixel ratio above
     1920x1080 plus the scene's above 1600x900 (totk-pc-recomp's formula, from MacNativeGames' measurement), and the
     game's memory (hos_mem.c hos_total_memory, which its heap is sized from) by the same amount. Without the grown
     table the game keeps its own resolution. */
#include "hos.h"
#include "input_bind.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define BASE_W 1920
#define BASE_H 1080
#define SCENE_W 1600
#define SCENE_H 900
#define DTA_KIB 113664u   /* DynamicTextureAllocatorMem, shipped */
#define SCENE_MAX 4096
#define HEAPDEF "System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs"

unsigned hos_out_w = BASE_W, hos_out_h = BASE_H;   /* the game's output (nn_vi.c, the renderer) */
static struct {
    unsigned sw, sh;    /* the scene */
    int scene_set;      /* the scene splice: Native */
    int on;             /* the splices are on: the heap table was grown (or did not need to be) */
    char heapdef[MAX_PATH];
} R;

extern uint64_t hos_total_memory;   /* hos_mem.c */
extern const char *hos_romfs_dir, *hos_user_dir;
int hos_mods_romfs(const char *rel, char *out, size_t cap);
int hos_run_tool(const char *cwd, const char *args, const char *log_name);   /* buttons.c */

/* The scene's share of the output that the 3D resolution modes are fractions of (totk_drs.c): 5/6, or 1 for Native */
float hos_res_scene_share(void) { return R.on && R.scene_set ? 1.0f : 5.0f / 6.0f; }
int hos_res_native(void) { return R.on && R.scene_set; }

static uint64_t heap_extra_kib(void) {
    const double grow = fmax(0.0, (double)hos_out_w * hos_out_h / (BASE_W * BASE_H) - 1.0) +
                        fmax(0.0, (double)R.sw * R.sh / (SCENE_W * SCENE_H) - 1.0);
    return grow > 0 ? (uint64_t)ceil(DTA_KIB * (1.0 + grow) / 1024.0) * 1024 - DTA_KIB : 0;   /* (whole MiB) */
}

#ifndef __EMSCRIPTEN__
static void decide(void) {
    Bindings b;
    hos_bind_get(&b);
    unsigned w = BASE_W, h = BASE_H;
    const char *e = getenv("HOS_OUTPUT_RES");
    if (e && *e) {
        unsigned ew = 0, eh = 0;
        if (sscanf(e, "%ux%u", &ew, &eh) == 2 && ew >= 1280 && eh >= 720 && ew <= 7680 && eh <= 4320) w = ew, h = eh;
        else hos_log("res: HOS_OUTPUT_RES=%s is not <width>x<height> in 1280x720..7680x4320: 1920x1080", e);
    } else if (b.output_res == 2) {
        w = 2560, h = 1440;
    } else if (b.output_res == 3) {
        w = 3840, h = 2160;
    } else if (b.output_res == 1) {   /* the main display's size, the largest 16:9 inside it */
        DEVMODEA dm = {0};
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth >= 1280 && dm.dmPelsHeight >= 720) {
            w = dm.dmPelsWidth;
            if (w * 9 > dm.dmPelsHeight * 16) w = dm.dmPelsHeight * 16 / 9;
            h = w * 9 / 16;
            hos_log("res: the display is %lux%lu: output %ux%u", dm.dmPelsWidth, dm.dmPelsHeight, w & ~1u, h & ~1u);
        }
    }
    if (w * 9 != h * 16 && abs((int)(w * 9) - (int)(h * 16)) > (int)h / 8)
        hos_log("res: %ux%u is not 16:9: the view is stretched until the ultrawide support (the camera, the HUD)", w, h);
    hos_out_w = w & ~1u, hos_out_h = h & ~1u;
    /* the scene: the game's own 5/6 of the output, or the whole output for Native (DLAA) */
    R.scene_set = b.render_mode == 5;
    const double k = R.scene_set ? 1.0 : 5.0 / 6.0;
    double sw = hos_out_w * k, sh = hos_out_h * k;
    const double fit = fmin(1.0, fmin(SCENE_MAX / sw, SCENE_MAX / sh));
    R.sw = (unsigned)(sw * fit + 0.5), R.sh = (unsigned)(sh * fit + 0.5);
}

/* the grown heap table, made once for each size (and each source file) into <user>\resolution */
static int make_heapdef(uint64_t extra_kib) {
    char src[MAX_PATH], p[MAX_PATH];
    if (!hos_mods_romfs(HEAPDEF, p, sizeof p)) snprintf(p, sizeof p, "%s/%s", hos_romfs_dir, HEAPDEF);
    for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
    if (!GetFullPathNameA(p, sizeof src, src, NULL)) snprintf(src, sizeof src, "%s", p);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &a)) {
        hos_log("res: no %s: the game keeps its own resolution", src);
        return 0;
    }
    char dir[MAX_PATH], rel[MAX_PATH];
    snprintf(rel, sizeof rel, "%s\\resolution", hos_user_dir ? hos_user_dir : "user");
    for (char *c = rel; *c; c++) if (*c == '/') *c = '\\';
    if (!GetFullPathNameA(rel, sizeof dir, dir, NULL)) snprintf(dir, sizeof dir, "%s", rel);
    CreateDirectoryA(dir, NULL);
    snprintf(R.heapdef, sizeof R.heapdef, "%s\\HeapDef_%llu_%lu_%08lx%08lx.byml.zs", dir, (unsigned long long)extra_kib,
             a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
    if (GetFileAttributesA(R.heapdef) != INVALID_FILE_ATTRIBUTES) return 1;
    char args[3 * MAX_PATH];
    snprintf(args, sizeof args, "--heapdef \"%s\" \"%s\" %llu", src, R.heapdef, (unsigned long long)extra_kib);
    if (hos_run_tool(dir, args, "heapdef.log") && GetFileAttributesA(R.heapdef) != INVALID_FILE_ATTRIBUTES) return 1;
    hos_log("res: the heap table could not be grown (%s\\heapdef.log): the game keeps its own resolution", dir);
    R.heapdef[0] = 0;
    return 0;
}
#endif

/* hos_main.c, after the mods are known and before the game starts: the sizes, the heap */
void hos_res_init(void) {
#if GAME_VERSION == 143 && !defined(__EMSCRIPTEN__)
    decide();
    if (hos_out_w == BASE_W && hos_out_h == BASE_H && !R.scene_set) {
        hos_log("res: the game's own (output 1920x1080, scene 1600x900)");
        return;
    }
    const uint64_t extra = heap_extra_kib();
    if (extra && !make_heapdef(extra)) {
        hos_out_w = BASE_W, hos_out_h = BASE_H, R.scene_set = 0;
        return;
    }
    hos_total_memory += ((extra << 10) + 0x1fffff) & ~0x1fffffull;
    R.on = 1;
    hos_log("res: output %ux%u, scene %ux%u%s; texture heap +%llu MB, game memory %llu MB", hos_out_w, hos_out_h, R.sw,
            R.sh, R.scene_set ? " (Native)" : " (the game's 5/6 of the output)", (unsigned long long)(extra >> 10),
            (unsigned long long)(hos_total_memory >> 20));
#else
    hos_log("res: a bigger resolution needs TOTK 1.4.3: the game's own");
#endif
}

/* nn_fs.c: the grown heap table in place of the game's */
int hos_res_romfs(const char *rel, char *out, size_t cap) {
#ifdef __EMSCRIPTEN__
    (void)rel, (void)out, (void)cap;
    return 0;
#endif
    if (!R.on || !R.heapdef[0] || _stricmp(rel, HEAPDEF)) return 0;
    snprintf(out, cap, "%s", R.heapdef);
    static int logged;
    if (!logged++) hos_log("res: %s <- %s", rel, R.heapdef);
    return 1;
}

#if GAME_VERSION == 143
static uint64_t packed(void) { return (uint64_t)hos_out_h << 32 | hos_out_w; }
static void put64(uint64_t a, uint64_t v) {
    memcpy(GP(a), &v, 8);
    HOS_WRITTEN(GP(a), 8);
}
static void putf(uint64_t a, float f) {
    memcpy(GP(a), &f, 4);
    HOS_WRITTEN(GP(a), 4);
}
/* the swapchain size nnMain builds: mov x8, #0x780; movk x8, #0x438, lsl #32 (main+0xbdb578) */
SPLICE_AFTER(0x08bdb440, "c->x[8] = (c->x[8] & ~(0xFFFFULL<<32)) | (0x438ULL<<32);") {
    if (R.on) c->x[8] = packed();
}
/* the display configuration (x19): its canvas (+0x10) and, next, its active output (+0x508, from x3) (main+0x3dcb28) */
SPLICE_AFTER(0x083dc9a8, "L_083dcb28:;") {
    if (!R.on) return;
    c->x[3] = packed();
    put64(c->x[19] + 0x10, packed());
    static int logged;
    if (!logged++) hos_log("res: the game's canvas is %ux%u", hos_out_w, hos_out_h);
}
/* the scene's size, as the renderer (x19) stores it (+0xcc4, +0xcc8; main+0x585e48): Native only; 1.4.3 multiplies it
   by output width / 1920 afterwards */
SPLICE_AFTER(0x08585da0, "recomp_store32(c,(c->x[19]+3272ULL),_v);") {
    if (!R.on || !R.scene_set) return;
    const float k = (float)hos_out_w / BASE_W;
    putf(c->x[19] + 0xcc4, R.sw / k);
    putf(c->x[19] + 0xcc8, R.sh / k);
    static int logged;
    if (!logged++) hos_log("res: the scene is %ux%u (Native)", R.sw, R.sh);
}
#endif
