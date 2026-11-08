/* Reflection quality (F1 > Graphics, the launcher, input.ini reflection_quality; at the start): the size of the
   game's reflection buffer, which its water and shiny surfaces show.

   TOTK 1.4.3 (found here, 2026-11-08): the reflection pass (f_0a850e9c, x20 its renderer, x23 the scene viewport) asks
   the dynamic texture allocator for "reflection buffer" at the viewport's size times two factors of the renderer
   (+0x308 width, +0x30c height), rounded down to 8 (the allocator call at main+0xa8510b4), then for
   "reflection_reduce" at that size times two more (+0x328, +0x32c) when their product is below 1, and blurs from there
   (reflection_color_blur).
   The splice multiplies the first two factors by the setting (x1.5, x2), at most the viewport's own size, right after
   the game computes the size (the shaders get it from the same descriptors, and the reduce follows). The buffers come
   from the texture heap, which grows to fit (totk_res.c hos_res_init). The first call logs what the game asked for. */
#include "hos.h"
#include "input_bind.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float kScales[3] = {1.0f, 1.5f, 2.0f};   /* reflection_quality */

static struct {
    float k;   /* the factors' multiplier */
    int on;
} F = {1.0f, 0};

void hos_reflection_decide(void) {
    Bindings b;
    hos_bind_get(&b);
    F.k = kScales[b.reflection_quality >= 0 && b.reflection_quality < 3 ? b.reflection_quality : 0];
    const char *e = getenv("HOS_REFLECTION_SCALE");   /* (1..4: overrides) */
    if (e && *e) {
        const double k = atof(e);
        if (k >= 1 && k <= 4) F.k = (float)k;
        else hos_log("reflections: HOS_REFLECTION_SCALE=%s is not 1..4", e);
    }
}

/* the texture heap the bigger buffers need (KiB): for a scene of sw x sh, the game's factors taken as 1/2 (the
   buffer, its depth and the reduced and blurred copies: ~24 bytes a texel) */
uint64_t hos_reflection_heap_kib(unsigned sw, unsigned sh) {
    if (F.k <= 1.0f) return 0;
    const double f = fmin(0.5 * F.k, 1.0);
    return (uint64_t)((f * f - 0.25) * sw * sh * 24 / 1024);
}

void hos_reflection_enable(int heap_ok) {
#if GAME_VERSION == 143
    if (F.k > 1.0f && !heap_ok) {
        hos_log("reflections: the texture heap could not grow: the game's own reflection size");
        F.k = 1.0f;
    }
    F.on = 1;
    if (F.k > 1.0f) hos_log("reflections: the reflection buffer x%g on each side (at most the scene's size)", F.k);
#else
    (void)heap_ok;
    if (F.k > 1.0f) hos_log("reflections: reflection quality needs TOTK 1.4.3: the game's own");
#endif
}

#if GAME_VERSION == 143
static float rdf(uint64_t a) {
    float f;
    memcpy(&f, GP(a), 4);
    return f;
}
/* the reflection buffer's size, as the game computed it (x8 width, x9 height), before the rounding to 8 */
SPLICE_AFTER(0x0a850e9c, "c->x[9]=recomp_fp_to_int(c->vreg[3][0]&UINT64_C(0xffffffff),32,32,0,3,c->fpcr,&c->fpsr);") {
    if (!F.on) return;
    const float vw = rdf(c->x[23] + 24) - rdf(c->x[23] + 16), vh = rdf(c->x[23] + 28) - rdf(c->x[23] + 20);
    const float fw = rdf(c->x[20] + 0x308), fh = rdf(c->x[20] + 0x30c);
    static int logged;
    if (logged < 4) {   /* (what the game asks for: the first few, and after a change of viewport) */
        static float lw, lh;
        if (vw != lw || vh != lh) {
            logged++, lw = vw, lh = vh;
            hos_log("reflections: viewport %.0fx%.0f, factors %g x %g (reduce %g x %g): the game's buffer %ux%u",
                    vw, vh, fw, fh, rdf(c->x[20] + 0x328), rdf(c->x[20] + 0x32c), (unsigned)c->x[8],
                    (unsigned)c->x[9]);
        }
    }
    if (F.k <= 1.0f || !(vw > 0) || !(vh > 0) || !(fw > 0) || !(fh > 0)) return;
    const float w = fminf(fw * F.k, 1.0f) * vw, h = fminf(fh * F.k, 1.0f) * vh;
    c->x[8] = (uint32_t)w;
    c->x[9] = (uint32_t)h;
}
#endif
