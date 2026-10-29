/* TOTK ultrawide (F6 or RX_ULTRAWIDE=1, gpu/render/rx_present_fx.cpp): the game's cameras take the window's aspect
   instead of 16:9, and the present stretches the (still 16:9) frame over the whole window, so a 21:9 or 32:9 display
   shows more of the world to the sides at the right proportions (each pixel a little wider than it is tall; the HUD
   is stretched with it for now).
   TOTK v1.0.0 computes its gameplay cameras' aspect as width / height (an fdiv s0, s0, s1 stored at camera + 176:
   main+0x1968c2c and main+0x1e5eae8) and keeps one 16:9 constant (1.7777780, main+0x377ac54): the same three places
   the TOTK-Ultrawide tool (github.com/Fruithapje21/TOTK-Ultrawide) patches for 1.0.0. Here the division's result
   becomes hos_view_aspect when that is set (0: the game's own), and the constant follows it.
   v1.4.3: the same division in f_09ff0404 and f_0a53fac0 (the only two places where fdiv s0, s0, s1 is stored to
   [x8, #0xb0] beside the same strb w9, [x8, #8]), and the constant at main+0x2d06910 (of the two 16:9 floats in its
   .rodata, the one loaded once with an add, as v1.0.0's is). */
#include "hos.h"
#include <string.h>

float hos_view_aspect;

static uint32_t aspect_bits(void) {
    uint32_t b;
    memcpy(&b, &hos_view_aspect, 4);
    return b;
}

/* the camera's width / height, just computed into s0 */
static void camera_aspect(Ctx *c) {
    if (hos_view_aspect > 0) c->vreg[0][0] = aspect_bits(), c->vreg[0][1] = 0;
}
#if GAME_VERSION == 100
SPLICE_AFTER(0x09968b68, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
SPLICE_AFTER(0x09e5ea24, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
#define ASPECT_CONSTANT 0x0B77AC54ULL   /* main+0x377ac54 */
#elif GAME_VERSION == 143
SPLICE_AFTER(0x09ff0404, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
SPLICE_AFTER(0x0a53fac0, "a64_vset64(c->vreg[0],_v);}") { camera_aspect(c); }
#define ASPECT_CONSTANT 0x0AD06910ULL   /* main+0x2d06910 */
#endif

/* hos_view_aspect changed (the renderer: the window's size or the setting): the 16:9 constant follows it */
void hos_ultrawide_apply(void) {
#ifdef ASPECT_CONSTANT
    static uint32_t original;
    if (!original) original = G32(ASPECT_CONSTANT);
    if (original != 0x3FE38E3Bu) return;   /* (not 16:9 there: not the build this address is for; left alone) */
    G32(ASPECT_CONSTANT) = hos_view_aspect > 0 ? aspect_bits() : original;
#endif
}

/* The HUD: TOTK's 2D layout loads a pair of sizes / scales (ldp s0, s1) at five places (main+0x12ade68, +0x12ae24c,
   +0x1a8e69c, +0x1a8f18c, +0x1a93954: the places TOTK-Ultrawide hooks); its horizontal one times aspect / (16:9)
   lays the HUD out for the wider view, so after the present's stretch it keeps its proportions.
   v1.4.3: two of the five are found (f_0a136efc: the same ldp s0, s1, [x29, #0x18] as f_09a8f0e0; f_0a13b2a4: the
   same loads as f_09a935ec, the hooked one now off x9 instead of x20), the other three not yet. A HUD scaled in some
   layouts and not others would be worse than one stretched evenly, so on v1.4.3 none are hooked until all are. */
static void ui_scale(Ctx *c) {
    if (hos_view_aspect <= 0) return;
    float f;
    memcpy(&f, &c->vreg[0][0], 4);
    f *= hos_view_aspect / (16.0f / 9.0f);
    uint32_t b;
    memcpy(&b, &f, 4);
    c->vreg[0][0] = b;
}
#if GAME_VERSION == 100
SPLICE_AFTER_EACH(0x092ada34, "_b=c->x[8]; int64_t _o=0; { uint64_t _p0,_p1; recomp_ldp32(c,(_b+_o),&_p0,&_p1); a64_vset64(c->vreg[0],_p0); a64_vset64(c->vreg[1],_p1);") { ui_scale(c); }
SPLICE_AFTER(0x09a8e5d4, "_b=c->x[31]; int64_t _o=8; { uint64_t _p0,_p1; recomp_ldp32(c,(_b+_o),&_p0,&_p1); a64_vset64(c->vreg[0],_p0); a64_vset64(c->vreg[1],_p1);") { ui_scale(c); }
SPLICE_AFTER(0x09a8f0e0, "_b=c->x[29]; int64_t _o=24; { uint64_t _p0,_p1; recomp_ldp32(c,(_b+_o),&_p0,&_p1); a64_vset64(c->vreg[0],_p0); a64_vset64(c->vreg[1],_p1);") { ui_scale(c); }
SPLICE_AFTER(0x09a935ec, "_b=c->x[20]; int64_t _o=32; { uint64_t _p0,_p1; recomp_ldp32(c,(_b+_o),&_p0,&_p1); a64_vset64(c->vreg[0],_p0); a64_vset64(c->vreg[1],_p1);") { ui_scale(c); }
#endif
