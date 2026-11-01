/* Keyboard and mouse bindings (input_bind.c), their editor window (input_bind_ui.c, F1 in the game), and what
   nn_hid.c reads from them. A binding is a Windows virtual-key code (keys, and the mouse buttons VK_LBUTTON,
   VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2) or one of the mouse wheel's two directions (BIND_WHEEL_*). */
#ifndef HOS_INPUT_BIND_H
#define HOS_INPUT_BIND_H
#include <stdint.h>

enum {   /* what a binding drives: the Pro Controller's buttons, stick directions, and walk (slower left stick) */
    ACT_A, ACT_B, ACT_X, ACT_Y, ACT_L, ACT_R, ACT_ZL, ACT_ZR, ACT_PLUS, ACT_MINUS, ACT_LS, ACT_RS,
    ACT_DLEFT, ACT_DUP, ACT_DRIGHT, ACT_DDOWN,
    ACT_LUP, ACT_LDOWN, ACT_LLEFT, ACT_LRIGHT, ACT_RUP, ACT_RDOWN, ACT_RLEFT, ACT_RRIGHT,
    ACT_WALK,
    ACT_COUNT
};
#define BIND_SLOTS 2              /* bindings per action */
#define BIND_WHEEL_UP 0x101
#define BIND_WHEEL_DOWN 0x102

typedef struct {
    uint16_t key[ACT_COUNT][BIND_SLOTS];   /* 0 = none */
    int mouse_camera;                      /* the captured mouse turns the camera */
    double mouse_sens;                     /* 1 = the default speed */
    int mouse_invert_y;
    double walk_scale;                     /* the left stick while walk is held, 0.1 .. 1 */
    double mouse_pitch;                    /* the mouse camera's up / down speed over its left / right, 0.25 .. 2.5 */
    double mouse_hold;                     /* the mouse's speed while Ultrahand holds something, 0.25 .. 4 */
} Bindings;

/* A copy of the current bindings (loaded from <user>/input.ini on first use). */
void hos_bind_get(Bindings *out);
/* Replaces the bindings and saves them to <user>/input.ini. */
void hos_bind_set(const Bindings *in);
void hos_bind_defaults(Bindings *out);

/* The action's name ("Jump (X)") and its short button name ("X"). */
const char *hos_bind_action_label(int act);
const char *hos_bind_action_button(int act);
/* A binding's name as input.ini spells it ("E", "Space", "Mouse1", "WheelUp"); "" for none. */
const char *hos_bind_key_name(int code);
int hos_bind_key_from_name(const char *name);   /* 0 if unknown */

/* Read by nn_hid.c each poll: buttons (bit = Npad button), the left and right stick from keys (-32767..32767),
   whether walk is held. mouse_buttons_ok: the mouse is captured (its buttons count only then). */
void hos_bind_poll(uint64_t *buttons, int32_t stick[4], int mouse_buttons_ok);

/* The mouse wheel (display.c): each notch is a short press of what is bound to that direction. */
void hos_bind_wheel(int notches);

/* Nonzero while the bindings window is open: the game gets no keyboard and mouse input then. */
extern volatile int hos_bind_ui_active;

#endif
