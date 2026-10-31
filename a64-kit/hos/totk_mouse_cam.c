/* TOTK native mouse camera (v1.4.3): the mouse turns the camera through the game's own camera input, instead of
   moving the right stick. As a stick it turned the camera at most as fast as a full tilt (the camera's top speed:
   "dragging the mouse through mud", and more sensitivity changed nothing past that), along the stick's curve.

   Where (found with HOS_WATCH_STICK, totk_watch.c): the player camera's parameters hold StickExponent (+0x180, 2.0)
   and StickSensitivityLat2LngRatio (+0x184, 1.78). Every frame f_088749b0 reads StickExponent and tail-calls
   f_08874c1c(x0 = input state, x1 = the raw sticks: right stick at +0 / +4, left at +8, s0 = the exponent,
   w2 = flags): the right stick past its dead zone, scaled, clamped to the unit circle and curved (|v| ^ exponent),
   is stored at state +0x420 / +0x424 (flag bit 1 / 2: the game's Camera X / Y-axis Inverted options turn x / y
   around), and that is what the camera turns by. f_08191b70 gives the yaw speed: the camera's speed times the 1.78.

   Here f_08874c1c runs as it is, then the mouse's motion in this frame is added to what it stored, linear and not
   clamped: no top speed and no curve, so a frame's turn is the mouse's motion in it (in degrees: 0.066 x the
   sensitivity per count, as a 0.022-degree game at 3x), over the frame's step (nn_vi: 1/60 or 1/30 s, or the real
   frame time with real-time steps). A full input
   (1) turns the camera CAM_DEG_PER_SEC a second horizontally: Savage Wind's measurement of BotW's CameraChase
   (2.848 degrees a 30th of a second; github.com/Tempus3214/SavageWindRecomp, branch mouse-camera, MIT), which TOTK's
   camera, with the same 1.78, is taken to share until measured. The vertical part: at first 1.78 times the mouse's
   motion (Savage Wind's way for BotW, whose cameras turn the vertical part 1.78 times slower), but in TOTK that made
   up / down clearly faster than left / right (a player's test): the same motion now, times mouse_vertical_speed (F1,
   25 .. 250%, default 100%) to even them out by feel. The Inverted options still apply; so does mouse_invert_y.
   While the game reads its camera input this way (the last 250 ms), the mouse stops moving the right stick
   (nn_hid.c, hos_mouse_cam_native); elsewhere (menus, the map) it moves the stick as before.
   HOS_MOUSE_NATIVE=0: the stick as before. HOS_MOUSE_CAM_LOG=<n>: n frames of input and mouse values logged. */
#include "hos.h"
#include "input_bind.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define CAM_DEG_PER_SEC (2.848 * 30.0)   /* a full input's turn a second, horizontally */
#define DEG_PER_COUNT 0.066              /* at sensitivity 1 */

static volatile int64_t native_at;   /* when the camera last took its input this way */

static int native_on(void) {
    static int on = -1;
    if (on < 0) on = !getenv("HOS_MOUSE_NATIVE") || atoi(getenv("HOS_MOUSE_NATIVE")) != 0;
    return on;
}

/* nn_hid.c: the camera takes the mouse natively now, so the mouse leaves the right stick alone */
int hos_mouse_cam_native(void) {
    const int64_t t = native_at;
    return native_on() && t && hos_now_ns() - t < 250000000;
}

#if GAME_VERSION == 143
static float getf(uint64_t a) { float f; memcpy(&f, GP(a), 4); return f; }
static void setf(uint64_t a, float f) { memcpy(GP(a), &f, 4); }

void f_08874c1c(Ctx *);
int hos_mouse_take_cam(int32_t *dx, int32_t *dy);   /* display.c */
double hos_vi_frame_seconds(void);                  /* nn_vi.c */
extern volatile LONG64 hos_present_count;            /* nn_vi.c */

static _Thread_local int inside;
static volatile float boost_x, boost_y;   /* the mouse's turn this frame, as the native camera adds it */
static volatile int64_t boost_frame = -1;
volatile uint64_t hos_game_controller;     /* totk_watch.c: the game controller the camera's stick node reads */

ENTRY_HOOK(0x08874c1c) {
    if (inside || !native_on()) return 0;
    const uint64_t state = c->x[0], in = c->x[1], flags = c->x[2], lr = c->x[30];
    /* The mouse also moves the right stick now (nn_hid.c), for the cameras that read the stick themselves; this one
       takes it natively below, so its read of the stick gets the pads' and keys' alone (put back after) */
    extern volatile float hos_stick_nomouse[2];
    extern volatile int hos_stick_moused;
    const int pre_takes = !(flags & 1) && !(*(uint8_t *)GP(state + 0x148) & 2);
    const int swap = pre_takes && hos_stick_moused;
    float saved[2] = {0, 0};
    if (swap) {
        saved[0] = getf(in), saved[1] = getf(in + 4);
        setf(in, hos_stick_nomouse[0]);
        setf(in + 4, hos_stick_nomouse[1]);
    }
    inside = 1;
    f_08874c1c(c);   /* (the game's own input first: the stick and keys keep working alongside the mouse) */
    inside = 0;
    if (swap) setf(in, saved[0]), setf(in + 4, saved[1]);
    {   /* HOS_WATCH_STICK=cam (debugging): who else reads the camera's raw stick and what it made of it (hos_watch.c):
           the cameras that turn by the stick themselves (Ultrahand holding something) */
        static int armed;
        const char *w = getenv("HOS_WATCH_STICK");
        if (!armed && w && !strcmp(w, "cam")) {
            armed = 1;
            void hos_watch_arm(uint64_t guest, int len, const char *why);
            hos_log("watch: the camera's raw stick at %#llx, its input state at %#llx", (unsigned long long)in,
                    (unsigned long long)state);
            hos_watch_arm(in, 8, "camera raw right stick");
            hos_watch_arm(state + 0x420, 8, "camera input (processed stick)");
        }
    }
    const int64_t now = hos_now_ns(), before = native_at;
    /* (flag bit 0: the input is off; state+0x148 bit 1: a set input instead of the stick's. The mouse is added only
       when neither: otherwise it goes on moving the right stick (nn_hid.c), which is what Ultrahand's and Recall's
       aiming read: the mouse did nothing there, the stick-only camera keys still worked) */
    const int takes = !(flags & 1) && !(*(uint8_t *)GP(state + 0x148) & 2);
    if (takes) native_at = now;
    {
        static int last_takes = -1, told;
        if (takes != last_takes && told < 40) {
            told++;
            hos_log("mouse cam: the camera input %s (flags %#llx, state+0x148 %#x, caller %#llx)",
                    takes ? "takes the mouse" : "is set or off: the mouse moves the stick", (unsigned long long)flags,
                    *(uint8_t *)GP(state + 0x148), (unsigned long long)lr);
        }
        last_takes = takes;
    }

    /* the mouse's motion since the last frame, once a frame (a second call in the same frame gets the same) */
    static SRWLOCK lock = SRWLOCK_INIT;
    static int64_t frame = -1;
    static float vx, vy;
    AcquireSRWLockExclusive(&lock);
    if (hos_present_count != frame) {
        frame = hos_present_count;
        int32_t dx, dy;
        const int held = hos_mouse_take_cam(&dx, &dy);
        if (!before || now - before > 250000000) dx = dy = 0;   /* (motion from a menu or the map: not the camera's) */
        extern volatile int hos_mouse_wheel;   /* nn_hid.c: the ability wheel has the mouse */
        if (hos_mouse_wheel) dx = dy = 0;
        Bindings b;
        hos_bind_get(&b);
        vx = vy = 0;
        if (held && b.mouse_camera && (dx || dy)) {
            const char *e = getenv("HOS_MOUSE_SENS");
            const double sens = e && atof(e) > 0 ? atof(e) : b.mouse_sens;
            const double step = hos_vi_frame_seconds();   /* the frame's simulated time (real-time steps: varies) */
            const double k = DEG_PER_COUNT * sens / (CAM_DEG_PER_SEC * step);
            vx = (float)(dx * k);
            vy = (float)(-dy * k * b.mouse_pitch * (b.mouse_invert_y ? -1 : 1));
        }
        boost_x = vx, boost_y = vy, boost_frame = hos_present_count;
        static int logs = -1;
        if (logs < 0) logs = getenv("HOS_MOUSE_CAM_LOG") ? atoi(getenv("HOS_MOUSE_CAM_LOG")) : 0;
        if (logs > 0 && (dx || dy || getf(in) != 0 || getf(in + 4) != 0)) {
            logs--;
            hos_log("mouse cam: stick %.3f %.3f -> %.3f %.3f, mouse %d %d -> %.3f %.3f, flags %#llx, +0x148 %#x, "
                    "caller %#llx, step %d ms", getf(in), getf(in + 4), getf(state + 0x420), getf(state + 0x424), dx,
                    dy, vx, vy, (unsigned long long)flags, *(uint8_t *)GP(state + 0x148),
                    (unsigned long long)lr, (int)(hos_vi_frame_seconds() * 1000 + 0.5));
        }
        static int told;
        if (!told++) hos_log("mouse cam: native (the camera's input, f_08874c1c); HOS_MOUSE_NATIVE=0 for the stick");
    }
    const float ax = flags & 2 ? -vx : vx, ay = flags & 4 ? -vy : vy;
    ReleaseSRWLockExclusive(&lock);
    if ((ax != 0 || ay != 0) && takes) {
        setf(state + 0x420, getf(state + 0x420) + ax);
        setf(state + 0x424, getf(state + 0x424) + ay);
    }
    return 1;
}
/* HOS_MOUSE_CAM_LOG (debugging): f_0a3d2d40, the other reader of the camera input's turn (state+0x420, found with
   HOS_WATCH_STICK=cam: it runs while Ultrahand holds something). What it read and what it made of it, per frame:
   this+472 / +476 are what it accumulates from the turn (times a speed and the step, clamped). */
void f_0a3d2d40(Ctx *);
static _Thread_local int inside_hold;
ENTRY_HOOK(0x0a3d2d40) {
    static int logs = -1;
    if (logs < 0) logs = getenv("HOS_MOUSE_CAM_LOG") ? atoi(getenv("HOS_MOUSE_CAM_LOG")) : 0;
    if (inside_hold || logs <= 0) return 0;
    const uint64_t self = c->x[0], st = G64(self + 32), step = c->x[1];
    const float in_x = getf(st + 0x420), in_y = getf(st + 0x424);
    const float a0 = getf(self + 472), b0 = getf(self + 476);
    inside_hold = 1;
    f_0a3d2d40(c);
    inside_hold = 0;
    const float a1 = getf(self + 472), b1 = getf(self + 476);
    if ((in_x != 0 || in_y != 0 || a0 != a1 || b0 != b1) && logs > 0) {
        logs--;
        hos_log("hold cam: turn %.3f %.3f (state %#llx +348 %#x) -> +472/476 %.3f %.3f -> %.3f %.3f, +480 %d, step %.4f",
                in_x, in_y, (unsigned long long)st, G32(st + 348), a0, b0, a1, b1, *(uint8_t *)GP(self + 480),
                getf(step + 8));
    }
    return 1;
}
/* HOS_MOUSE_CAM_LOG (debugging): f_0818e414, the player camera's update (the main reader of the camera input's
   turn, state+0x420): its mode (this+384, +388 the last, +392) and the floats it turns by, before and after, per
   frame while there is a turn or the mode changes: to find the mode Ultrahand's holding uses and its turn ramp. */
void f_0818e414(Ctx *);
static _Thread_local int inside_player;
ENTRY_HOOK(0x0818e414) {
    static int logs = -1;
    if (logs < 0) logs = getenv("HOS_MOUSE_CAM_LOG") ? atoi(getenv("HOS_MOUSE_CAM_LOG")) : 0;
    if (inside_player || logs <= 0) return 0;
    const uint64_t self = c->x[0], st = G64(self + 32), step = c->x[1];
    static const int offs[] = {88, 92, 96, 100, 120, 124, 128, 132, 136, 216, 220, 224, 228, 332, 340};
    enum { N = sizeof offs / sizeof *offs };
    float before[N], after[N];
    for (int i = 0; i < N; i++) before[i] = getf(self + offs[i]);
    const float tx = getf(st + 0x420), ty = getf(st + 0x424);
    const uint32_t mode0 = G32(self + 384);
    inside_player = 1;
    f_0818e414(c);
    inside_player = 0;
    for (int i = 0; i < N; i++) after[i] = getf(self + offs[i]);
    const uint32_t mode1 = G32(self + 384);
    static uint32_t last_mode = ~0u;
    if ((tx != 0 || ty != 0 || mode1 != last_mode) && logs > 0) {
        logs--;
        char b[900];
        int n = snprintf(b, sizeof b, "player cam: turn %.3f %.3f mode %u->%u (+388 %u +392 %u +380 %#x) step %.4f |",
                         tx, ty, mode0, mode1, G32(self + 388), G32(self + 392), *(uint8_t *)GP(self + 380),
                         getf(step + 8));
        for (int i = 0; i < N && n < (int)sizeof b - 40; i++)
            n += snprintf(b + n, sizeof b - n, " %d:%.3f>%.3f", offs[i], before[i], after[i]);
        hos_log("%s", b);
    }
    last_mode = mode1;
    return 1;
}
/* HOS_STICK_BOOST=<scale> (experiment): the cameras that turn by the stick themselves (Ultrahand holding something:
   neither the camera input above nor the player camera runs then) get the mouse unclamped: after the controller
   manager's update (f_08479c20) the game controller's right stick (+296 / +300, what the stick nodes read) is the
   pads' and keys' plus the mouse's turn this frame times <scale>, not the stick the mouse moved (at most a full tilt,
   with a floor and a decay: "dragging the mouse through mud"). The camera input above still leaves the mouse's
   stick out and adds it its own way. Only while the camera takes the mouse natively and the wheel is shut. */
void f_08479c20(Ctx *);
int hos_watch_manager_update(Ctx *c);   /* totk_watch.c */
static _Thread_local int inside_mgr;
ENTRY_HOOK(0x08479c20) {
    hos_watch_manager_update(c);
    static float scale = -1;
    if (scale < 0) scale = getenv("HOS_STICK_BOOST") ? (float)atof(getenv("HOS_STICK_BOOST")) : 0;
    if (inside_mgr || scale <= 0) return 0;
    inside_mgr = 1;
    f_08479c20(c);
    inside_mgr = 0;
    extern volatile float hos_stick_nomouse[2];
    extern volatile int hos_mouse_wheel;
    const uint64_t ctl = hos_game_controller;
    if (ctl && hos_mouse_cam_native() && !hos_mouse_wheel && hos_present_count - boost_frame <= 1) {
        /* The stick cameras curve what they read (|v| past a dead zone, raised to an exponent: 2 for the player
           camera's StickExponent), which made the mouse's linear turn a "very fast stick": a few centimetres a
           360, small motions lost in the dead zone. The mouse's part goes in uncurved: the magnitude m becomes
           dz + (1 - dz) * m^(1/exp), so after their curve the turn is again in step with the mouse.
           HOS_STICK_BOOST_EXP=<exp> (default 2; 1: none), HOS_STICK_BOOST_DZ=<dz> (default the camera input's own
           dead zone, read from the game). */
        static float expo = -1, dz = -1;
        if (expo < 0) {
            expo = getenv("HOS_STICK_BOOST_EXP") ? (float)atof(getenv("HOS_STICK_BOOST_EXP")) : 2.0f;
            if (expo < 0.1f) expo = 1;
            const float game_dz = getf(0x0ad026a8);   /* (f_08874c1c's dead zone: a constant of the image) */
            dz = getenv("HOS_STICK_BOOST_DZ") ? (float)atof(getenv("HOS_STICK_BOOST_DZ"))
                 : game_dz > 0 && game_dz < 0.5f ? game_dz : 0.0f;
            hos_log("mouse cam: stick boost on (x%.2f, curve exponent %.2f, dead zone %.3f; the game's %.3f)", scale,
                    expo, dz, game_dz);
        }
        float bx = boost_x * scale, by = boost_y * scale;
        const float m = sqrtf(bx * bx + by * by);
        if (m > 0) {
            const float out = dz + (1 - dz) * powf(m, 1 / expo);
            bx *= out / m, by *= out / m;
        }
        setf(ctl + 296, hos_stick_nomouse[0] + bx);
        setf(ctl + 300, hos_stick_nomouse[1] + by);
    }
    return 1;
}
#endif
