/* The bindings window (F1 in the game, display.c): every action with two binding buttons, and the mouse settings.
   Click a binding, then press the key or mouse button (or turn the wheel) for it; Esc cancels, Delete or Backspace
   clears it. Each change applies at once and is saved to <user>/input.ini (input_bind.c). While the window is open
   the game gets no keyboard and mouse input (controllers still work).

   Plain Win32 controls (user32 / gdi32 only), made on the display thread, owned by the game's window. A key is
   caught by polling GetAsyncKeyState while waiting (the same state the game reads, mouse buttons included), after
   the click that started the wait has been let go. */
#include "hos.h"
#include "input_bind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    ID_BIND = 1000,          /* + act * BIND_SLOTS + slot */
    ID_MOUSE_CAMERA = 2000, ID_INVERT, ID_SENS, ID_SENS_LABEL, ID_WALK, ID_WALK_LABEL, ID_PITCH, ID_PITCH_LABEL, ID_HOLD, ID_HOLD_LABEL, ID_RES, ID_RES_LABEL, ID_DEFAULTS, ID_CLOSE,
    ID_STATUS,
    TIMER_LISTEN = 1
};

static HWND win, owner, status;
static HWND bind_btn[ACT_COUNT][BIND_SLOTS];
static HFONT font, bold;
static int dpi = 96;
static int listen_act = -1, listen_slot, listen_armed;
static uint8_t listen_prev[256];

/* the status line and the buttons: under the mouse and graphics settings (right column: 9 rows, then 346 px of
   them) */
enum { BOTTOM_Y = 36 + 9 * 28 + 14 + 346 + 12 };

static int S(int v) { return MulDiv(v, dpi, 96); }

static void set_status(const char *s) { SetWindowTextA(status, s); }

static void refresh(void) {
    Bindings b;
    hos_bind_get(&b);
    for (int a = 0; a < ACT_COUNT; a++)
        for (int s = 0; s < BIND_SLOTS; s++) {
            const int k = b.key[a][s];
            const int waiting = a == listen_act && s == listen_slot;
            SetWindowTextA(bind_btn[a][s], waiting ? "press a key..." : k ? hos_bind_key_name(k) : "-");
        }
    CheckDlgButton(win, ID_MOUSE_CAMERA, b.mouse_camera ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(win, ID_INVERT, b.mouse_invert_y ? BST_CHECKED : BST_UNCHECKED);
    char t[64];
    const int sens = (int)(b.mouse_sens * 100 + 0.5);
    SetScrollPos(GetDlgItem(win, ID_SENS), SB_CTL, sens < 5 ? 5 : sens > 500 ? 500 : sens, TRUE);
    snprintf(t, sizeof t, "Mouse sensitivity: %.2f", b.mouse_sens);
    SetDlgItemTextA(win, ID_SENS_LABEL, t);
    const int walk = (int)(b.walk_scale * 100 + 0.5);
    SetScrollPos(GetDlgItem(win, ID_WALK), SB_CTL, walk, TRUE);
    snprintf(t, sizeof t, "Walk speed (left stick while Walk is held): %d%%", walk);
    SetDlgItemTextA(win, ID_WALK_LABEL, t);
    const int pitch = (int)(b.mouse_pitch * 100 + 0.5);
    SetScrollPos(GetDlgItem(win, ID_PITCH), SB_CTL, pitch, TRUE);
    snprintf(t, sizeof t, "Mouse camera up / down speed (of left / right): %d%%", pitch);
    SetDlgItemTextA(win, ID_PITCH_LABEL, t);
    const int hold = (int)(b.mouse_hold * 100 + 0.5);
    SetScrollPos(GetDlgItem(win, ID_HOLD), SB_CTL, hold, TRUE);
    snprintf(t, sizeof t, "Ultrahand holding: mouse speed: %d%% (100 = camera)", hold);
    SetDlgItemTextA(win, ID_HOLD_LABEL, t);
    SetScrollPos(GetDlgItem(win, ID_RES), SB_CTL, b.render_mode, TRUE);
    char r[128];
    snprintf(r, sizeof r, "3D resolution: %s", hos_render_mode_name(b.render_mode));
    SetDlgItemTextA(win, ID_RES_LABEL, r);
}

static void stop_listening(void) {
    if (listen_act < 0) return;
    KillTimer(win, TIMER_LISTEN);
    listen_act = -1;
    refresh();
}

/* Binds code to the action being waited for (0 clears), and says where else that key is bound. */
static void bind_to(int code) {
    if (listen_act < 0) return;
    Bindings b;
    hos_bind_get(&b);
    const int act = listen_act, slot = listen_slot;
    b.key[act][slot] = (uint16_t)code;
    if (code && b.key[act][slot ^ 1] == code) b.key[act][slot ^ 1] = 0;   /* (the same key twice on one action) */
    hos_bind_set(&b);
    char msg[256];
    if (!code) snprintf(msg, sizeof msg, "%s (%s): binding cleared.", hos_bind_action_label(act), hos_bind_action_button(act));
    else {
        snprintf(msg, sizeof msg, "%s now on %s (%s).", hos_bind_key_name(code), hos_bind_action_label(act),
                 hos_bind_action_button(act));
        for (int a = 0; a < ACT_COUNT; a++)
            for (int s = 0; s < BIND_SLOTS; s++)
                if (a != act && b.key[a][s] == code) {
                    char more[128];
                    snprintf(more, sizeof more, " Also bound to %s (%s): both happen.", hos_bind_action_label(a),
                             hos_bind_action_button(a));
                    strncat(msg, more, sizeof msg - strlen(msg) - 1);
                    a = ACT_COUNT;
                    break;
                }
    }
    set_status(msg);
    stop_listening();
}

/* Keys the game window uses itself: not bindable (Esc is cancel here; F1 this window; F6-F11 display settings) */
static int reserved(int vk) { return vk == VK_ESCAPE || vk == VK_F1 || vk == VK_F4 || (vk >= VK_F6 && vk <= VK_F11); }

static void listen_tick(void) {
    uint8_t now[256];
    int any_mouse = 0;
    for (int vk = 1; vk < 256; vk++) now[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    for (int vk = VK_LBUTTON; vk <= VK_XBUTTON2; vk++) if (vk != VK_CANCEL && now[vk]) any_mouse = 1;
    if (!listen_armed) {   /* (the click on the binding button: wait until it is let go) */
        if (!any_mouse) listen_armed = 1;
        memcpy(listen_prev, now, sizeof now);
        return;
    }
    for (int vk = 1; vk < 256; vk++) {
        if (!now[vk] || listen_prev[vk] || vk == VK_CANCEL) continue;
        if (vk >= VK_LSHIFT && vk <= VK_RMENU) continue;   /* (the generic Shift / Ctrl / Alt come too) */
        if (vk == VK_ESCAPE) { set_status("Cancelled."); stop_listening(); return; }
        if (vk == VK_DELETE || vk == VK_BACK) { bind_to(0); return; }
        if (reserved(vk)) { set_status("That key is the game window's own (Esc, F1, F6-F11)."); continue; }
        bind_to(vk);
        return;
    }
    memcpy(listen_prev, now, sizeof now);
}

static void start_listening(int act, int slot) {
    stop_listening();
    listen_act = act;
    listen_slot = slot;
    listen_armed = 0;
    memset(listen_prev, 1, sizeof listen_prev);
    SetFocus(win);   /* (keys go to this window, which ignores them, not to the button: Space would click it) */
    char msg[192];
    snprintf(msg, sizeof msg, "%s (%s): press a key or mouse button, or turn the wheel. Esc cancels, Delete clears.",
             hos_bind_action_label(act), hos_bind_action_button(act));
    set_status(msg);
    refresh();
    SetTimer(win, TIMER_LISTEN, 10, NULL);
}

static HWND make(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), win,
                             (HMENU)(intptr_t)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static void scroll(HWND bar, int code, int pos) {
    const int id = GetDlgCtrlID(bar);
    int v = GetScrollPos(bar, SB_CTL);
    const int step = id == ID_RES ? 1 : 5, page = id == ID_SENS ? 25 : id == ID_RES ? 1 : 10;
    switch (code) {
    case SB_LINELEFT: v -= step; break;
    case SB_LINERIGHT: v += step; break;
    case SB_PAGELEFT: v -= page; break;
    case SB_PAGERIGHT: v += page; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: v = pos; break;
    default: return;
    }
    Bindings b;
    hos_bind_get(&b);
    if (id == ID_SENS) b.mouse_sens = (v < 5 ? 5 : v > 500 ? 500 : v) / 100.0;
    else if (id == ID_PITCH) b.mouse_pitch = (v < 25 ? 25 : v > 250 ? 250 : v) / 100.0;
    else if (id == ID_HOLD) b.mouse_hold = (v < 25 ? 25 : v > 400 ? 400 : v) / 100.0;
    else if (id == ID_RES) {
        const int n = hos_render_mode_count();
        b.render_mode = v < 0 ? 0 : v >= n ? n - 1 : v;
    }
    else b.walk_scale = (v < 10 ? 10 : v > 100 ? 100 : v) / 100.0;
    hos_bind_set(&b);
    refresh();
}

static void layout(void) {
    /* left: the buttons and d-pad; right: the sticks and walk, then the mouse */
    static const int left[] = {ACT_A, ACT_B, ACT_X, ACT_Y, ACT_L, ACT_R, ACT_ZL, ACT_ZR, ACT_PLUS, ACT_MINUS,
                               ACT_LS, ACT_RS, ACT_DLEFT, ACT_DUP, ACT_DRIGHT, ACT_DDOWN};
    static const int right[] = {ACT_LUP, ACT_LDOWN, ACT_LLEFT, ACT_LRIGHT, ACT_WALK, ACT_RUP, ACT_RDOWN, ACT_RLEFT,
                                ACT_RRIGHT};
    const int row = 28, label_w = 250, btn_w = 105, col_w = label_w + 2 * (btn_w + 6) + 24;
    make("STATIC", "Buttons", 0, 12, 10, 200, 20, -1, bold);
    make("STATIC", "Movement and camera keys", 0, 12 + col_w, 10, 300, 20, -1, bold);
    for (int side = 0; side < 2; side++) {
        const int *list = side ? right : left, n = side ? (int)(sizeof right / sizeof *right) : (int)(sizeof left / sizeof *left);
        const int x = 12 + side * col_w;
        for (int i = 0; i < n; i++) {
            const int a = list[i], y = 36 + i * row;
            char t[96];
            if (side) snprintf(t, sizeof t, "%s", hos_bind_action_label(a));
            else snprintf(t, sizeof t, "%s  -  %s", hos_bind_action_button(a), hos_bind_action_label(a));
            make("STATIC", t, SS_CENTERIMAGE, x, y, label_w, 24, -1, font);
            for (int s = 0; s < BIND_SLOTS; s++)
                bind_btn[a][s] = make("BUTTON", "", BS_PUSHBUTTON, x + label_w + s * (btn_w + 6), y, btn_w, 24,
                                      ID_BIND + a * BIND_SLOTS + s, font);
        }
    }
    const int mx = 12 + col_w, my = 36 + 9 * row + 14;
    make("STATIC", "Mouse", 0, mx, my, 200, 20, -1, bold);
    make("BUTTON", "Mouse turns the camera (click the game to capture the mouse; Esc frees it)", BS_AUTOCHECKBOX, mx,
         my + 24, 470, 22, ID_MOUSE_CAMERA, font);
    make("BUTTON", "Invert up / down", BS_AUTOCHECKBOX, mx, my + 48, 300, 22, ID_INVERT, font);
    make("STATIC", "", 0, mx, my + 78, 470, 20, ID_SENS_LABEL, font);
    HWND sb = make("SCROLLBAR", "", SBS_HORZ, mx, my + 98, 460, 18, ID_SENS, font);
    SetScrollRange(sb, SB_CTL, 5, 500, FALSE);
    make("STATIC", "", 0, mx, my + 126, 470, 20, ID_WALK_LABEL, font);
    HWND wb = make("SCROLLBAR", "", SBS_HORZ, mx, my + 146, 460, 18, ID_WALK, font);
    make("STATIC", "", 0, mx, my + 174, 470, 20, ID_PITCH_LABEL, font);
    HWND pb = make("SCROLLBAR", "", SBS_HORZ, mx, my + 194, 460, 18, ID_PITCH, font);
    SetScrollRange(pb, SB_CTL, 25, 250, FALSE);
    make("STATIC", "", 0, mx, my + 222, 470, 20, ID_HOLD_LABEL, font);
    HWND hb = make("SCROLLBAR", "", SBS_HORZ, mx, my + 242, 460, 18, ID_HOLD, font);
    SetScrollRange(hb, SB_CTL, 25, 400, FALSE);
    make("STATIC", "Graphics", 0, mx, my + 274, 200, 20, -1, bold);
    make("STATIC", "", 0, mx, my + 298, 470, 20, ID_RES_LABEL, font);
    HWND rb = make("SCROLLBAR", "", SBS_HORZ, mx, my + 318, 460, 18, ID_RES, font);
    SetScrollRange(rb, SB_CTL, 0, hos_render_mode_count() - 1, FALSE);
    SetScrollRange(wb, SB_CTL, 10, 100, FALSE);
    const int by = BOTTOM_Y;
    status = make("STATIC", "Click a binding, then press the key or mouse button for it. Changes save at once.",
                  SS_LEFT, 12, by, 2 * col_w - 24, 40, ID_STATUS, font);
    make("BUTTON", "Reset to defaults", BS_PUSHBUTTON, 12, by + 46, 160, 28, ID_DEFAULTS, font);
    make("BUTTON", "Close (F1)", BS_DEFPUSHBUTTON, 2 * col_w - 12 - 130, by + 46, 130, 28, ID_CLOSE, font);
}

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id >= ID_BIND && id < ID_BIND + ACT_COUNT * BIND_SLOTS && HIWORD(wp) == BN_CLICKED) {
            start_listening((id - ID_BIND) / BIND_SLOTS, (id - ID_BIND) % BIND_SLOTS);
            return 0;
        }
        Bindings b;
        hos_bind_get(&b);
        if (id == ID_MOUSE_CAMERA) { b.mouse_camera = IsDlgButtonChecked(w, id) == BST_CHECKED; hos_bind_set(&b); }
        else if (id == ID_INVERT) { b.mouse_invert_y = IsDlgButtonChecked(w, id) == BST_CHECKED; hos_bind_set(&b); }
        else if (id == ID_DEFAULTS) {
            stop_listening();
            const Bindings old = b;   /* (the graphics settings: kept) */
            hos_bind_defaults(&b);
            b.render_mode = old.render_mode, b.aniso = old.aniso, b.reflex = old.reflex;
            b.button_icons = old.button_icons, b.output_res = old.output_res, b.fps_limit = old.fps_limit;
            hos_bind_set(&b);
            set_status("All bindings and mouse settings are back to the defaults.");
            refresh();
        } else if (id == ID_CLOSE || id == IDCANCEL) DestroyWindow(w);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_LISTEN) listen_tick();
        return 0;
    case WM_MOUSEWHEEL:
        if (listen_act >= 0 && listen_armed) bind_to((short)HIWORD(wp) > 0 ? BIND_WHEEL_UP : BIND_WHEEL_DOWN);
        return 0;
    case WM_HSCROLL:
        if (lp) scroll((HWND)lp, LOWORD(wp), (short)HIWORD(wp));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_F1 && listen_act < 0) DestroyWindow(w);
        return 0;   /* (keys while waiting are read by listen_tick) */
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;   /* (Alt is bindable: no menu loop on its own) */
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        KillTimer(w, TIMER_LISTEN);
        listen_act = -1;
        win = NULL;
        hos_bind_ui_active = 0;
        if (owner) SetForegroundWindow(owner);
        hos_log("input: bindings window closed");
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* F1 (display.c, on its thread): opens the window, or closes it when open. */
void hos_bind_ui_toggle(HWND game_window) {
    if (win) { DestroyWindow(win); return; }
    owner = game_window;
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = "SkyboundBindings";
        RegisterClassA(&wc);
        registered = 1;
    }
    typedef UINT(WINAPI * GetDpi)(HWND);
    GetDpi get = (GetDpi)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    dpi = get && game_window ? (int)get(game_window) : 96;
    if (dpi < 96) dpi = 96;
    if (font) DeleteObject(font);
    if (bold) DeleteObject(bold);
    font = CreateFontA(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                       "Segoe UI");
    bold = CreateFontA(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, "Segoe UI");
    const int col_w = 250 + 2 * (105 + 6) + 24;
    RECT r = {0, 0, S(2 * col_w), S(BOTTOM_Y + 46 + 28 + 12)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    RECT g;
    GetWindowRect(game_window, &g);
    int x = g.left + ((g.right - g.left) - (r.right - r.left)) / 2, y = g.top + ((g.bottom - g.top) - (r.bottom - r.top)) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    win = CreateWindowExA(WS_EX_DLGMODALFRAME, "SkyboundBindings", "SkyboundTOTK - keyboard and mouse", style, x, y,
                          r.right - r.left, r.bottom - r.top, game_window, NULL, GetModuleHandleA(NULL), NULL);
    if (!win) { hos_log("input: could not open the bindings window (%lu)", GetLastError()); return; }
    hos_bind_ui_active = 1;
    layout();
    refresh();
    ShowWindow(win, SW_SHOW);
    SetForegroundWindow(win);
    hos_log("input: bindings window open");
}
