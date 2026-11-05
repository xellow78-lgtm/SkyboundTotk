/* TOTK 1.4.3 above 30 fps: what the real-time frame step (nn_vi.c: the game steps by the time its frames really
   take) leaves out. The game was written for whole 1/30 s frames, and a few things count frames, not time:

   - fades: the distance / LOD mesh fades and the pre-actor fades add trunc(2.1 * ratio) to a counter each frame (2 at
     30 fps, 1 at 60, 0 above ~63.5 fps), so above that a fade stops and its object stays see-through or invisible
     until a slower frame. The increment is floored at 1 (the four places it is computed).
   - hair and cloth: the actor animation update makes a second clock {seconds = factor / 30, ratio = factor} for each
     rendered frame, so at 60 fps they moved twice as fast. Two actor paths build it inline (the factor in s10 / s0,
     the ratio slot on the stack): they get the measured step times the factor; two others go through the out-of-line
     update f_082a0e40 with seconds and ratio in s2 / s3, which are scaled by the measured ratio (only from those two:
     its third caller already passes measured time).
   - the equipment-switch cooldown (player state +0x109c) is stepped only while above FLT_EPSILON, but weapons, shields
     and bows stay locked while it is above 0: whole 30 Hz steps end at exactly 0, a measured step can leave a
     remainder in (0, FLT_EPSILON] that is never stepped again, locking equipment until a load. Such a remainder is
     stepped like any last step (found by JieML1100, totk-pc-recomp issue #18).
   - trampolines: the contact code scales the bounce by 0.1 when the time object's +0x28 (30 / (ratio * time scale))
     is over 100, which depends on the frame rate; it compares +0x28 * +0x38 instead, as MacNativeGames does.

   The sites and what each does are from totk-pc-recomp's recomp/patches-v143.txt (MIT, Copyright (c) 2026 Valiantsin
   Dzerakh), ported there from MacNativeGames games/totk/runtime/game_patches_v143.c (MIT, Copyright (c) 2026
   MacNativeGames contributors). Here they are splices and an entry hook on the lifted code. All need the real-time
   step (HOS_REALTIME, on by default); HOS_HIGHFPS_FIX=0 leaves the fades, the cooldown and trampolines as shipped,
   HOS_CLOTH_REALTIME=0 the hair and cloth.

   Still not fixed above ~63 fps, in any port as far as is known (MacNativeGames #1348): the camera turns faster and
   Link may not climb steps. */
#include "hos.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

int hos_realtime_step(void);        /* nn_vi.c */
uint64_t hos_frame_step_bits(void);

static int flag(const char *name) {   /* (on unless name=0) */
    const char *e = getenv(name);
    return !(e && *e == '0');
}
static int highfps(void) {
    static int on = -1;
    if (on < 0) on = flag("HOS_HIGHFPS_FIX") && hos_realtime_step();
    return on;
}
static int cloth(void) {
    static int on = -1;
    if (on < 0) on = flag("HOS_CLOTH_REALTIME") && hos_realtime_step();
    return on;
}
static float getf(uint64_t a) {
    float f;
    memcpy(&f, GP(a), 4);
    return f;
}
static void putf(uint64_t a, float f) {
    memcpy(GP(a), &f, 4);
    HOS_WRITTEN(GP(a), 4);
}
static float vf(Ctx *c, int v) {
    float f;
    memcpy(&f, &c->vreg[v][0], 4);
    return f;
}
static void setvf(Ctx *c, int v, float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    c->vreg[v][0] = b, c->vreg[v][1] = 0;
}
static void step(float *seconds, float *ratio) {
    const uint64_t v = hos_frame_step_bits();
    const uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    memcpy(ratio, &lo, 4);
    memcpy(seconds, &hi, 4);
}

#if GAME_VERSION == 143
/* ---- fades: fcvtzs w8 / w10, s0 (the increment); +1 when it came out 0 */
SPLICE_AFTER(0x080a7900, "c->x[8]=recomp_fp_to_int(c->vreg[0][0]&UINT64_C(0xffffffff),32,32,1,3,c->fpcr,&c->fpsr);") {
    if (highfps() && !(uint32_t)c->x[8]) c->x[8] = 1;   /* (main+0x0a7ac4: a mesh fade) */
}
SPLICE_AFTER(0x0895dbb0, "c->x[8]=recomp_fp_to_int(c->vreg[0][0]&UINT64_C(0xffffffff),32,32,1,3,c->fpcr,&c->fpsr);") {
    if (highfps() && !(uint32_t)c->x[8]) c->x[8] = 1;   /* (main+0x95dd24: the second mesh fade) */
}
SPLICE_AFTER_EACH(0x08615540, "c->x[10]=recomp_fp_to_int(c->vreg[0][0]&UINT64_C(0xffffffff),32,32,1,3,c->fpcr,&c->fpsr);") {
    if (highfps() && !(uint32_t)c->x[10]) c->x[10] = 1;   /* (main+0x6155e4, +0x615660: the pre-actor fades) */
}

/* ---- the equipment cooldown: the value just loaded (s0) is compared with FLT_EPSILON next; a remainder at or below
   it (and above 0) is shown as 1 to that test, so the step (which reads the field itself) takes it below 0 */
SPLICE_AFTER(0x082adef0, "recomp_load32(c,(c->x[19]+4252ULL))") {
    if (!highfps()) return;
    const float v = vf(c, 0);
    if (v > 0 && v <= FLT_EPSILON) setvf(c, 0, 1.0f);
}

/* ---- trampolines: ldr s0, [x9, #0x28] (main+0x562a64) times [x9, #0x38] */
SPLICE_AFTER(0x085628cc, "recomp_load32(c,(c->x[9]+40ULL))") {
    if (highfps()) setvf(c, 0, vf(c, 0) * getf(c->x[9] + 0x38));
}

/* ---- hair and cloth, inline: main+0x246eac (factor s10; seconds to sp+0x360, ratio to sp+0x364) */
SPLICE_AFTER(0x08245030, "recomp_store32_sp(c,(c->x[31]+864ULL),_v);") {
    if (!cloth()) return;
    float seconds, ratio;
    step(&seconds, &ratio);
    const float k = vf(c, 10);
    putf(c->x[31] + 0x360, seconds * k);
    putf(c->x[31] + 0x364, ratio * k);
    setvf(c, 0, seconds * k);
}
/* main+0x345904 (factor s0; seconds to sp+0x110, ratio to sp+0x114) */
SPLICE_AFTER(0x0834526c, "recomp_store32_sp(c,(c->x[31]+272ULL),_v);") {
    if (!cloth()) return;
    float seconds, ratio;
    step(&seconds, &ratio);
    const float k = vf(c, 0);
    putf(c->x[31] + 0x110, seconds * k);
    putf(c->x[31] + 0x114, ratio * k);
    setvf(c, 1, seconds * k);
}
/* the out-of-line update, from main+0x24944c and +0x28bc98 (their return addresses): s2 seconds, s3 ratio */
ENTRY_HOOK(0x082a0e40) {
    if (!cloth() || (c->x[30] != 0x08249450ULL && c->x[30] != 0x0828bc9cULL)) return 0;
    float seconds, ratio;
    step(&seconds, &ratio);
    setvf(c, 2, vf(c, 2) * ratio);
    setvf(c, 3, vf(c, 3) * ratio);
    return 0;
}
#endif
