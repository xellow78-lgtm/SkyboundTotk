/* The sun's shadows: their map's size and how far the sharp cascades reach (Shadow resolution / Shadow distance: F1 >
   Graphics, the launcher, input.ini shadow_resolution / shadow_distance; at the start).

   Adapted from totk-pc-recomp's runtime/resolution.c (shadow_init, res_shadow_range) and recomp/patches-v143.txt (the
   shadow and cascade sets) (MIT, Copyright (c) 2026 Valiantsin Dzerakh). Here the sites are splices into the lifted
   code (hos.h SPLICE_AFTER) instead of instruction patches, and the texture heap grows with the resolution's
   (totk_res.c hos_res_init).

   TOTK 1.4.3:
   - the size: each frame the engine sizes its sun shadow cascades (a D16 array of 4 layers, 960 at most in the field)
     to the area they cover, down to about 400 near the ground (foliage shadows turn blocky) and every change of size
     moves the shadow texels (leaf shadows jump instead of sliding). The size is interpolated in f_0868318c and rounded up
     to 8 at main+0x683604 (L_08683604: add w11, w11, #7), then stored as the cascades' width (+0x3d8) and, through a
     packed store at +0xf4, their height (+0x3f8). A fixed size goes into w11 just before that rounding, as the
     community's shadow resolution mods do. The texture heap must hold the bigger maps: (size^2 - 960^2) x 4 layers x
     2 bytes, three maps in flight (the engine makes the array anew each frame), totk-pc-recomp's measurement.
   - the distance: the depth shadow parameter object (f_086f7880) registers depth_shadow_near_0_1..3 = 20, 40, 80 m and
     depth_shadow_far_0 = 160 m as immediates (mov w8, #<float>; str w8, ...: main+0x6fabb8, 6fac40, 6facc8, 6fad4c), and
     the field's .bgsdw files do not override them. The splice multiplies them: the sharp first cascade reaches further
     and the steps where shadows turn softer move away, spreading each cascade's texels over more ground (Shadow
     resolution pays that back). */
#include "hos.h"
#include "input_bind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADOW_BASE 960u

static const unsigned kSizes[4] = {0, 1024, 1536, 2048};          /* shadow_resolution: 0 the game's own */
static const float kRanges[4] = {1.0f, 1.5f, 2.0f, 3.0f};         /* shadow_distance */

static struct {
    unsigned size;    /* the cascades' fixed size, 0: the game's own */
    float range;      /* the cascade distances' factor */
    int on;           /* the splices are on (the heap holds the size) */
} S = {0, 1.0f, 0};

/* the wanted settings (totk_res.c hos_res_init, at the start) */
void hos_shadow_decide(void) {
    Bindings b;
    hos_bind_get(&b);
    S.size = kSizes[b.shadow_res >= 0 && b.shadow_res < 4 ? b.shadow_res : 0];
    S.range = kRanges[b.shadow_dist >= 0 && b.shadow_dist < 4 ? b.shadow_dist : 0];
    /* (HOS_SHADOW_RES=<size> 256..4096, a multiple of 8, or game; HOS_SHADOW_RANGE=<percent> 50..400: overrides) */
    const char *e = getenv("HOS_SHADOW_RES");
    if (e && *e) {
        const int n = atoi(e);
        if (n >= 256 && n <= 4096 && !(n & 7)) S.size = (unsigned)n;
        else if (!_stricmp(e, "game")) S.size = 0;
        else hos_log("shadows: HOS_SHADOW_RES=%s is not game or a size 256..4096 (a multiple of 8)", e);
    }
    e = getenv("HOS_SHADOW_RANGE");
    if (e && *e) {
        const int pct = atoi(e);
        if (pct >= 50 && pct <= 400) S.range = pct / 100.0f;
        else hos_log("shadows: HOS_SHADOW_RANGE=%s is not a percent 50..400", e);
    }
}

/* the texture heap the size needs above the game's own (KiB) */
uint64_t hos_shadow_heap_kib(void) {
    return S.size > SHADOW_BASE ? ((uint64_t)S.size * S.size - (uint64_t)SHADOW_BASE * SHADOW_BASE) * 8 * 3 / 1024 : 0;
}

/* heap_ok: the heap table holds the size (or it needed nothing) */
void hos_shadow_enable(int heap_ok) {
#if GAME_VERSION == 143
    if (S.size > SHADOW_BASE && !heap_ok) {
        hos_log("shadows: the texture heap could not grow: the game sizes its shadow map");
        S.size = 0;
    }
    S.on = 1;
    if (S.size) hos_log("shadows: sun shadow map %ux%u (the game's own: up to %u, smaller near the ground)", S.size,
                        S.size, SHADOW_BASE);
    else hos_log("shadows: sun shadow map sized by the game");
    if (S.range != 1.0f)
        hos_log("shadows: cascades end at %g, %g, %g and %g m (the game's: 20, 40, 80, 160)", 20 * S.range,
                40 * S.range, 80 * S.range, 160 * S.range);
#else
    (void)heap_ok;
    if (S.size || S.range != 1.0f) hos_log("shadows: shadow resolution and distance need TOTK 1.4.3: the game's own");
#endif
}

#if GAME_VERSION == 143
/* the cascades' size, just before it is rounded up to 8 (main+0x683604) */
SPLICE_AFTER(0x0868318c, "L_08683604:;") {
    if (S.on && S.size) c->x[11] = S.size;
}
/* the cascade distances, as the depth shadow parameters register them (main+0x6fabb8 .. 0x6fad4c) */
SPLICE_AFTER_EACH(0x086f7880, "c->x[8] = 0x4") {
    if (!S.on || S.range == 1.0f) return;
    float d;
    switch ((uint32_t)c->x[8]) {
    case 0x41a00000u: d = 20.0f; break;
    case 0x42200000u: d = 40.0f; break;
    case 0x42a00000u: d = 80.0f; break;
    case 0x43200000u: d = 160.0f; break;
    default: return;
    }
    d *= S.range;
    uint32_t bits;
    memcpy(&bits, &d, 4);
    c->x[8] = bits;
}
#endif
