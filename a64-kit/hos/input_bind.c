/* Keyboard and mouse bindings: which keys and mouse buttons press which Pro Controller button or push which stick
   direction (input_bind.h). Kept in <user>/input.ini, one line per action, e.g. "Y = F, Mouse1"; F1 in the game
   opens an editor (input_bind_ui.c). A line left out keeps the default; "X =" unbinds. Unknown names are skipped
   (logged). Also there: mouse_camera, mouse_sensitivity, mouse_invert_y, mouse_vertical_speed, walk_speed. An older <user>/mouse_sens.txt
   is read once as the sensitivity when there is no input.ini yet. */
#include "hos.h"
#include "input_bind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef __EMSCRIPTEN__   /* (the web build's windows.h defines a few virtual keys; these are winuser.h's values) */
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F24 0x87
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_COMMA 0xBC
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#endif

extern const char *hos_user_dir;
volatile int hos_bind_ui_active;

static const struct { const char *ini, *button, *label; int npad_bit; } actions[ACT_COUNT] = {
    [ACT_A] = {"A", "A", "Action", 0},
    [ACT_B] = {"B", "B", "Put away / cancel (hold: sprint)", 1},
    [ACT_X] = {"X", "X", "Jump", 2},
    [ACT_Y] = {"Y", "Y", "Attack", 3},
    [ACT_L] = {"L", "L", "Ability", 6},
    [ACT_R] = {"R", "R", "Throw weapon", 7},
    [ACT_ZL] = {"ZL", "ZL", "Focus / shield", 8},
    [ACT_ZR] = {"ZR", "ZR", "Use bow", 9},
    [ACT_PLUS] = {"Plus", "+", "Menu", 10},
    [ACT_MINUS] = {"Minus", "-", "Purah Pad", 11},
    [ACT_LS] = {"LeftStickClick", "LS", "Crouch", 4},
    [ACT_RS] = {"RightStickClick", "RS", "Right stick click", 5},
    [ACT_DLEFT] = {"DpadLeft", "D-pad Left", "Change equipment", 12},
    [ACT_DUP] = {"DpadUp", "D-pad Up", "D-pad Up", 13},
    [ACT_DRIGHT] = {"DpadRight", "D-pad Right", "Change equipment", 14},
    [ACT_DDOWN] = {"DpadDown", "D-pad Down", "D-pad Down", 15},
    [ACT_LUP] = {"MoveUp", "Left stick up", "Move forward", -1},
    [ACT_LDOWN] = {"MoveDown", "Left stick down", "Move back", -1},
    [ACT_LLEFT] = {"MoveLeft", "Left stick left", "Move left", -1},
    [ACT_LRIGHT] = {"MoveRight", "Left stick right", "Move right", -1},
    [ACT_RUP] = {"CameraUp", "Right stick up", "Camera up", -1},
    [ACT_RDOWN] = {"CameraDown", "Right stick down", "Camera down", -1},
    [ACT_RLEFT] = {"CameraLeft", "Right stick left", "Camera left", -1},
    [ACT_RRIGHT] = {"CameraRight", "Right stick right", "Camera right", -1},
    [ACT_WALK] = {"Walk", "Walk", "Walk (slower left stick)", -1},
};

const char *hos_bind_action_label(int act) { return act >= 0 && act < ACT_COUNT ? actions[act].label : ""; }
const char *hos_bind_action_button(int act) { return act >= 0 && act < ACT_COUNT ? actions[act].button : ""; }

/* ---- key names ------------------------------------------------------------------------------------------------ */
static const struct { int vk; const char *name; } named[] = {
    {VK_LBUTTON, "Mouse1"}, {VK_RBUTTON, "Mouse2"}, {VK_MBUTTON, "Mouse3"}, {VK_XBUTTON1, "Mouse4"},
    {VK_XBUTTON2, "Mouse5"}, {BIND_WHEEL_UP, "WheelUp"}, {BIND_WHEEL_DOWN, "WheelDown"},
    {VK_BACK, "Backspace"}, {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"},
    {VK_MENU, "Alt"}, {VK_PAUSE, "Pause"}, {VK_CAPITAL, "CapsLock"}, {VK_ESCAPE, "Esc"}, {VK_SPACE, "Space"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_END, "End"}, {VK_HOME, "Home"}, {VK_LEFT, "Left"},
    {VK_UP, "Up"}, {VK_RIGHT, "Right"}, {VK_DOWN, "Down"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"},
    {VK_MULTIPLY, "NumMultiply"}, {VK_ADD, "NumPlus"}, {VK_SUBTRACT, "NumMinus"}, {VK_DECIMAL, "NumDecimal"},
    {VK_DIVIDE, "NumDivide"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_COMMA, "Comma"},
    {VK_OEM_MINUS, "Minus"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"}, {VK_OEM_3, "Backquote"},
    {VK_OEM_4, "LeftBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_6, "RightBracket"}, {VK_OEM_7, "Quote"},
    {VK_LSHIFT, "LeftShift"}, {VK_RSHIFT, "RightShift"}, {VK_LCONTROL, "LeftCtrl"}, {VK_RCONTROL, "RightCtrl"},
    {VK_LMENU, "LeftAlt"}, {VK_RMENU, "RightAlt"},
};

const char *hos_bind_key_name(int code) {
    static char buf[8][16];
    static volatile LONG next;
    if (!code) return "";
    for (unsigned i = 0; i < sizeof named / sizeof *named; i++)
        if (named[i].vk == code) return named[i].name;
    char *b = buf[InterlockedIncrement(&next) & 7];
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) snprintf(b, 16, "%c", code);
    else if (code >= VK_NUMPAD0 && code <= VK_NUMPAD9) snprintf(b, 16, "Num%d", code - VK_NUMPAD0);
    else if (code >= VK_F1 && code <= VK_F24) snprintf(b, 16, "F%d", code - VK_F1 + 1);
    else snprintf(b, 16, "VK%d", code);
    return b;
}

int hos_bind_key_from_name(const char *s) {
    if (!s || !*s) return 0;
    for (unsigned i = 0; i < sizeof named / sizeof *named; i++)
        if (!_stricmp(named[i].name, s)) return named[i].vk;
    if (!s[1] && ((s[0] >= '0' && s[0] <= '9') || ((s[0] | 0x20) >= 'a' && (s[0] | 0x20) <= 'z')))
        return s[0] >= 'a' ? s[0] - 0x20 : s[0];
    if (!_strnicmp(s, "Num", 3) && s[3] >= '0' && s[3] <= '9' && !s[4]) return VK_NUMPAD0 + (s[3] - '0');
    if ((s[0] | 0x20) == 'f' && atoi(s + 1) >= 1 && atoi(s + 1) <= 24) return VK_F1 + atoi(s + 1) - 1;
    if (!_strnicmp(s, "VK", 2) && atoi(s + 2) > 0 && atoi(s + 2) < 256) return atoi(s + 2);
    return 0;
}

/* ---- the bindings ---------------------------------------------------------------------------------------------- */
void hos_bind_defaults(Bindings *b) {
    memset(b, 0, sizeof *b);
    static const struct { int act; uint16_t k0, k1; } d[] = {
        {ACT_A, 'E'}, {ACT_B, VK_SHIFT, 'Q'}, {ACT_X, VK_SPACE}, {ACT_Y, VK_LBUTTON, 'F'}, {ACT_L, 'Z'},
        {ACT_R, 'R'}, {ACT_ZL, VK_RBUTTON, 'C'}, {ACT_ZR, 'V'}, {ACT_PLUS, VK_RETURN}, {ACT_MINUS, VK_TAB},
        {ACT_LS, VK_CONTROL}, {ACT_RS, VK_MBUTTON}, {ACT_DLEFT, VK_LEFT}, {ACT_DUP, VK_UP}, {ACT_DRIGHT, VK_RIGHT},
        {ACT_DDOWN, VK_DOWN}, {ACT_LUP, 'W'}, {ACT_LDOWN, 'S'}, {ACT_LLEFT, 'A'}, {ACT_LRIGHT, 'D'}, {ACT_RUP, 'I'},
        {ACT_RDOWN, 'K'}, {ACT_RLEFT, 'J'}, {ACT_RRIGHT, 'L'}, {ACT_WALK, VK_MENU}};
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++) {
        b->key[d[i].act][0] = d[i].k0;
        b->key[d[i].act][1] = d[i].k1;
    }
    b->mouse_camera = 1;
    b->mouse_sens = 1.0;
    b->walk_scale = 0.4;
    b->mouse_pitch = 1.0;
    b->mouse_hold = 1.0;
    b->render_mode = 0;
    b->aniso = 0;
    b->reflex = 0;
    b->button_icons = 0;
    b->output_res = 0;
    b->fps_limit = 60;
    b->fps_counter = 0;
    b->aspect = 0;
    b->frame_timing = 0;
    b->dlss_preset = 0;
    b->shadow_res = 0;
    b->shadow_dist = 0;
}

volatile int hos_aniso_level, hos_reflex_mode, hos_button_icons_setting, hos_fps_limit = 60, hos_fps_counter, hos_frame_timing,
             hos_dlss_preset;
static void apply_graphics(const Bindings *b) {
    hos_aniso_level = b->aniso;
    hos_reflex_mode = b->reflex;
    hos_button_icons_setting = b->button_icons;
    hos_fps_limit = b->fps_limit;
    hos_fps_counter = b->fps_counter;
    hos_frame_timing = b->frame_timing;
    hos_dlss_preset = b->dlss_preset;
}

static SRWLOCK lock = SRWLOCK_INIT;
static Bindings cur;
static int loaded;

static void ini_path(char *p, size_t n) { snprintf(p, n, "%s/input.ini", hos_user_dir ? hos_user_dir : "user"); }

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void load(Bindings *b) {
    hos_bind_defaults(b);
    char path[512];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(path, sizeof path, "%s/mouse_sens.txt", hos_user_dir ? hos_user_dir : "user");
        FILE *old = fopen(path, "r");
        double v;
        if (old && fscanf(old, "%lf", &v) == 1 && v > 0) b->mouse_sens = v;
        if (old) fclose(old);
        hos_log("input: default keyboard and mouse bindings (F1 in the game changes them)");
        return;
    }
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');   /* (a comment, also after a value: save() writes one on each line) */
        if (hash) *hash = 0;
        char *s = trim(line), *eq = strchr(s, '=');
        if (!*s || *s == ';' || !eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (!_stricmp(k, "mouse_camera")) { b->mouse_camera = atoi(v) != 0; continue; }
        if (!_stricmp(k, "mouse_sensitivity")) { if (atof(v) > 0) b->mouse_sens = atof(v); continue; }
        if (!_stricmp(k, "mouse_invert_y")) { b->mouse_invert_y = atoi(v) != 0; continue; }
        if (!_stricmp(k, "walk_speed")) { b->walk_scale = atof(v); continue; }
        if (!_stricmp(k, "mouse_vertical_speed")) { b->mouse_pitch = atof(v); continue; }
        if (!_stricmp(k, "mouse_ultrahand_holding_speed")) { b->mouse_hold = atof(v); continue; }
        if (!_stricmp(k, "render_resolution")) { b->render_mode = atoi(v); continue; }
        if (!_stricmp(k, "anisotropic_filtering")) { b->aniso = atoi(v); continue; }
        if (!_stricmp(k, "nvidia_reflex")) { b->reflex = atoi(v); continue; }
        if (!_stricmp(k, "button_icons")) { b->button_icons = atoi(v); continue; }
        if (!_stricmp(k, "output_resolution")) { b->output_res = atoi(v); continue; }
        if (!_stricmp(k, "frame_rate_limit")) { b->fps_limit = atoi(v); continue; }
        if (!_stricmp(k, "fps_counter")) { b->fps_counter = atoi(v); continue; }
        if (!_stricmp(k, "aspect_ratio")) { b->aspect = atoi(v); continue; }
        if (!_stricmp(k, "frame_timing")) { b->frame_timing = atoi(v); continue; }
        if (!_stricmp(k, "dlss_preset")) { b->dlss_preset = atoi(v); continue; }
        if (!_stricmp(k, "shadow_resolution")) { b->shadow_res = atoi(v); continue; }
        if (!_stricmp(k, "shadow_distance")) { b->shadow_dist = atoi(v); continue; }
        int act = -1;
        for (int a = 0; a < ACT_COUNT && act < 0; a++)
            if (!_stricmp(k, actions[a].ini)) act = a;
        if (act < 0) { hos_log("input: input.ini: unknown setting \"%s\"", k); continue; }
        b->key[act][0] = b->key[act][1] = 0;
        int slot = 0;
        for (char *tok = strtok(v, ","); tok && slot < BIND_SLOTS; tok = strtok(NULL, ",")) {
            const int code = hos_bind_key_from_name(trim(tok));
            if (code) b->key[act][slot++] = (uint16_t)code;
            else if (*trim(tok)) hos_log("input: input.ini: %s: unknown key \"%s\"", k, trim(tok));
        }
        n++;
    }
    fclose(f);
    if (b->mouse_sens < 0.05) b->mouse_sens = 0.05;
    if (b->mouse_sens > 20) b->mouse_sens = 20;
    if (b->walk_scale < 0.1 || b->walk_scale > 1) b->walk_scale = 0.4;
    if (!(b->mouse_pitch >= 0.25 && b->mouse_pitch <= 2.5)) b->mouse_pitch = 1.0;
    if (!(b->mouse_hold >= 0.25 && b->mouse_hold <= 4)) b->mouse_hold = 1.0;
    if (b->render_mode < 0 || b->render_mode >= hos_render_mode_count()) b->render_mode = 0;
    if (b->aniso < 0 || b->aniso > 4) b->aniso = 0;
    if (b->reflex < 0 || b->reflex > 2) b->reflex = 0;
    if (b->button_icons < 0 || b->button_icons > 4) b->button_icons = 0;
    if (b->output_res < 0 || b->output_res > 3) b->output_res = 0;
    if (b->fps_limit > 200) b->fps_limit = 200;   /* (240 was a choice before: the game's sound fades out above 200) */
    if (b->fps_limit != 0 && b->fps_limit < 30) b->fps_limit = 60;
    if (b->fps_counter < 0 || b->fps_counter > 2) b->fps_counter = 0;
    if (b->aspect < 0 || b->aspect > 4) b->aspect = 0;
    if (b->frame_timing < 0 || b->frame_timing > 1) b->frame_timing = 0;
    if (b->dlss_preset < 0 || b->dlss_preset > 4) b->dlss_preset = 0;
    if (b->shadow_res < 0 || b->shadow_res > 3) b->shadow_res = 0;
    if (b->shadow_dist < 0 || b->shadow_dist > 3) b->shadow_dist = 0;
    hos_log("input: bindings from %s (%d actions set)", path, n);
}

static void save(const Bindings *b) {
    char path[512];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) { hos_log("input: cannot write %s", path); return; }
    fprintf(f, "# SkyboundTOTK keyboard and mouse bindings. F1 in the game opens the editor; or edit this file (game\n"
               "# closed). Up to %d keys an action, separated by commas; nothing after = unbinds. Mouse buttons are\n"
               "# Mouse1 (left) .. Mouse5, the wheel WheelUp / WheelDown.\n\n", BIND_SLOTS);
    fprintf(f, "mouse_camera = %d\nmouse_sensitivity = %.3f\nmouse_vertical_speed = %.2f\n"
               "mouse_ultrahand_holding_speed = %.2f\nmouse_invert_y = %d\nwalk_speed = %.2f\n\n", b->mouse_camera,
               b->mouse_sens, b->mouse_pitch, b->mouse_hold, b->mouse_invert_y, b->walk_scale);
    fprintf(f, "# 3D resolution (of the output): 0 the game's own (dynamic, up to 5/6), 1 Quality (2/3), 2 Balanced\n"
               "# (1/1.7), 3 Performance (1/2), 4 Ultra Performance (1/3), 5 Native (DLAA; chosen at the start: the full\n"
               "# output); FSR 2 or DLSS scales it up\nrender_resolution = %d\n", b->render_mode);
    fprintf(f, "# anisotropic filtering: 0 the game's own, 1 2x, 2 4x, 3 8x, 4 16x\nanisotropic_filtering = %d\n"
               "# NVIDIA Reflex: 0 off, 1 on, 2 on + boost (NVIDIA GPUs, Vulkan)\nnvidia_reflex = %d\n"
               "# button prompts: 0 the device played with, 1 keyboard and mouse, 2 Xbox, 3 PlayStation, 4 Switch\n"
               "button_icons = %d\n"
               "# the game's output, at the next start: 0 1920x1080 (its own), 1 the display's (the largest 16:9 that fits),\n"
               "# 2 2560x1440, 3 3840x2160\noutput_resolution = %d\n"
               "# frames a second at most: 30 .. 200, or 0 for the display's refresh rate (at most 200; above 60: beta)\n"
               "frame_rate_limit = %d\n"
               "# the frame counter in the top left corner: 0 off, 1 frames a second, 2 and the frame time\n"
               "fps_counter = %d\n"
               "# the picture's shape, at the next start: 0 16:9 (the game's own), 1 the display's, 2 16:10, 3 21:9, 4 32:9\n"
               "aspect_ratio = %d\n"
               "# the game's step per frame: 0 smooth (the average of the last 5 frame times), 1 responsive (the last\n"
               "# frame's time: on a G-Sync / FreeSync display, no speed-up after a slow frame)\n"
               "frame_timing = %d\n"
               "# DLSS's model: 0 NVIDIA's default for the mode, 1 K, 2 M, 3 L (transformer models), 4 J\n"
               "dlss_preset = %d\n"
               "# the sun's shadow map, at the next start: 0 the game's own (up to 960, smaller near the ground), 1 1024,\n"
               "# 2 1536, 3 2048 (sharper, steadier shadows; more video memory)\nshadow_resolution = %d\n"
               "# how far the sharp shadows reach, at the next start: 0 the game's own (20 / 40 / 80 / 160 m), 1 x1.5,\n"
               "# 2 x2, 3 x3 (each cascade spread over more ground: raise shadow_resolution with it)\nshadow_distance = %d\n\n",
               b->aniso, b->reflex, b->button_icons, b->output_res, b->fps_limit, b->fps_counter, b->aspect,
               b->frame_timing, b->dlss_preset, b->shadow_res, b->shadow_dist);
    for (int a = 0; a < ACT_COUNT; a++) {
        fprintf(f, "%s = ", actions[a].ini);
        for (int s = 0, first = 1; s < BIND_SLOTS; s++)
            if (b->key[a][s]) { fprintf(f, "%s%s", first ? "" : ", ", hos_bind_key_name(b->key[a][s])); first = 0; }
        fprintf(f, "    # %s: %s\n", actions[a].button, actions[a].label);
    }
    fclose(f);
}

static void ensure_loaded(void) {
    if (loaded) return;
    AcquireSRWLockExclusive(&lock);
    if (!loaded) { load(&cur); apply_graphics(&cur); loaded = 1; }
    ReleaseSRWLockExclusive(&lock);
}

void hos_bind_get(Bindings *out) {
    ensure_loaded();
    AcquireSRWLockShared(&lock);
    *out = cur;
    ReleaseSRWLockShared(&lock);
}

void hos_bind_set(const Bindings *in) {
    ensure_loaded();
    AcquireSRWLockExclusive(&lock);
    cur = *in;
    ReleaseSRWLockExclusive(&lock);
    hos_render_mode_apply(in->render_mode);
    apply_graphics(in);
    save(in);
    hos_log("input: bindings saved");
}

/* ---- polling ------------------------------------------------------------------------------------------------- */
static volatile LONG wheel_up, wheel_down;   /* notches not yet turned into presses */
void hos_bind_wheel(int notches) {
    if (notches > 0) InterlockedExchangeAdd(&wheel_up, notches);
    if (notches < 0) InterlockedExchangeAdd(&wheel_down, -notches);
}
/* A notch is a press of 60 ms then 40 ms released (the game sees it across frames even at 20 fps), one after another */
static int wheel_pressed(volatile LONG *pending, int64_t *until, int64_t now) {
    if (*until && now < *until) return 1;
    if (*until && now < *until + 40000000) return 0;
    *until = 0;
    if (*pending > 0) {
        InterlockedDecrement(pending);
        *until = now + 60000000;
        return 1;
    }
    return 0;
}

static int is_mouse_button(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

void hos_bind_poll(uint64_t *buttons, int32_t stick[4], int mouse_buttons_ok) {
    Bindings b;
    hos_bind_get(&b);
    const int64_t now = hos_now_ns();
    static int64_t up_until, down_until;
    const int wu = wheel_pressed(&wheel_up, &up_until, now), wd = wheel_pressed(&wheel_down, &down_until, now);
    /* (a click that captured the mouse is not also a press: mouse buttons count once all are up after capture) */
    static int armed;
    int any_mouse = 0;
    for (int vk = VK_LBUTTON; vk <= VK_XBUTTON2; vk++)
        if (is_mouse_button(vk) && (GetAsyncKeyState(vk) & 0x8000)) any_mouse = 1;
    if (!mouse_buttons_ok) armed = 0;
    else if (!armed && !any_mouse) armed = 1;
    int held[ACT_COUNT] = {0};
    for (int a = 0; a < ACT_COUNT; a++)
        for (int s = 0; s < BIND_SLOTS; s++) {
            const int k = b.key[a][s];
            if (!k) continue;
            if (k == BIND_WHEEL_UP) held[a] |= wu;
            else if (k == BIND_WHEEL_DOWN) held[a] |= wd;
            else if (is_mouse_button(k)) held[a] |= armed && (GetAsyncKeyState(k) & 0x8000) != 0;
            else held[a] |= (GetAsyncKeyState(k) & 0x8000) != 0;
        }
    for (int a = 0; a < ACT_COUNT; a++)
        if (held[a] && actions[a].npad_bit >= 0) *buttons |= 1ULL << actions[a].npad_bit;
    const int32_t full = held[ACT_WALK] ? (int32_t)(32767 * b.walk_scale) : 32767;
    stick[0] = (held[ACT_LRIGHT] - held[ACT_LLEFT]) * full;
    stick[1] = (held[ACT_LUP] - held[ACT_LDOWN]) * full;
    stick[2] = (held[ACT_RRIGHT] - held[ACT_RLEFT]) * 32767;
    stick[3] = (held[ACT_RUP] - held[ACT_RDOWN]) * 32767;
}
