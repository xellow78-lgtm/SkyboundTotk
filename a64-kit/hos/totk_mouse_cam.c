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
   sensitivity per count, as a 0.022-degree game at 3x), at the frame's step (nn_vi: 1/60 or 1/30 s). A full input
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
int hos_vi_interval(void);                           /* nn_vi.c */
extern volatile LONG64 hos_present_count;            /* nn_vi.c */

static _Thread_local int inside;

ENTRY_HOOK(0x08874c1c) {
    if (inside || !native_on()) return 0;
    const uint64_t state = c->x[0], in = c->x[1], flags = c->x[2];
    inside = 1;
    f_08874c1c(c);   /* (the game's own input first: the stick and keys keep working alongside the mouse) */
    inside = 0;
    const int64_t now = hos_now_ns(), before = native_at;
    native_at = now;

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
        Bindings b;
        hos_bind_get(&b);
        vx = vy = 0;
        if (held && b.mouse_camera && (dx || dy)) {
            const char *e = getenv("HOS_MOUSE_SENS");
            const double sens = e && atof(e) > 0 ? atof(e) : b.mouse_sens;
            const double step = hos_vi_interval() / 60.0;   /* the frame's simulated time */
            const double k = DEG_PER_COUNT * sens / (CAM_DEG_PER_SEC * step);
            vx = (float)(dx * k);
            vy = (float)(-dy * k * b.mouse_pitch * (b.mouse_invert_y ? -1 : 1));
        }
        static int logs = -1;
        if (logs < 0) logs = getenv("HOS_MOUSE_CAM_LOG") ? atoi(getenv("HOS_MOUSE_CAM_LOG")) : 0;
        if (logs > 0 && (dx || dy || getf(in) != 0 || getf(in + 4) != 0)) {
            logs--;
            hos_log("mouse cam: stick %.3f %.3f -> %.3f %.3f, mouse %d %d -> %.3f %.3f, flags %#llx, interval %d",
                    getf(in), getf(in + 4), getf(state + 0x420), getf(state + 0x424), dx, dy, vx, vy,
                    (unsigned long long)flags, hos_vi_interval());
        }
        static int told;
        if (!told++) hos_log("mouse cam: native (the camera's input, f_08874c1c); HOS_MOUSE_NATIVE=0 for the stick");
    }
    const float ax = flags & 2 ? -vx : vx, ay = flags & 4 ? -vy : vy;
    ReleaseSRWLockExclusive(&lock);
    /* (flag bit 0: the input is off; state+0x148 bit 1: a set input instead of the stick's. The game's, then) */
    if ((ax != 0 || ay != 0) && !(flags & 1) && !(*(uint8_t *)GP(state + 0x148) & 2)) {
        setf(state + 0x420, getf(state + 0x420) + ax);
        setf(state + 0x424, getf(state + 0x424) + ay);
    }
    return 1;
}
#endif
