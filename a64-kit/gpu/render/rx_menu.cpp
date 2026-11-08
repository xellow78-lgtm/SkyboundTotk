// The in-game settings menu (F1): drawn by the renderer over the frame, into the window's own image, so it works in
// fullscreen and looks like part of the game. Three pages: Graphics (3D resolution, anisotropic filtering, NVIDIA
// Reflex, button icons, which upscaler runs), Mouse (the mouse camera and its speeds), Controls (the keyboard and mouse bindings).
// Every change applies at once and is saved to input.ini (hos/input_bind.c), as the old settings window did.
//
// Keyboard: Up / Down a row, Left / Right change it (or the key slot on Controls), Enter: toggle, press a button, or
// rebind (then the next key or mouse button, the wheel too; Esc cancels, Delete / Backspace clears), Tab / Shift+Tab
// or Q / E: the page, Esc or F1: close. Mouse: point and click, drag a slider, the wheel scrolls Controls.
// While it is open the game gets no keyboard and mouse input (hos_bind_ui_active); controllers still reach the game.
//
// Drawing: one pipeline, one instanced quad per shape: a rounded rectangle (signed distance: soft edges at any size,
// an outline, a vertical shade) or a glyph from an atlas the system's Segoe UI is drawn into with GDI (the font is
// Windows', read at run time, never shipped). Sizes follow the window's height (made for 1080 lines).
//
// Threads: input arrives on the window's thread (display.c), drawing happens where the frame is presented (the
// submit thread, or frame interpolation's display thread): the menu's state is under one lock.
#include "rx_internal.h"

#if !RX_WEB

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "../../hos/input_bind.h"
}

extern "C" unsigned hos_out_w, hos_out_h;   // hos/totk_res.c

namespace rx {
bool fsr2_wanted();   // rx_fsr2.cpp

namespace {

// ---- what is on the pages ----
enum Page { kGraphics, kMouse, kControls, kPages };
const char* const kPageNames[kPages] = {"Graphics", "Mouse", "Controls"};

enum Kind { kOption, kSlider, kToggle, kBinding, kButton, kInfo, kSection };
enum Id {
    kOutRes, kAspect, kRes, kFps, kFrameTiming, kFrameGen, kFpsCounter, kAniso, kShadowRes, kShadowDist, kReflections, kSurfaceRefl, kReflex, kButtons, kUpscaler, kDlssModel, kMouseCam, kInvert, kSens, kPitch, kHold, kWalk, kResetControls, kBind
};

struct Row {
    Kind kind;
    Id id;
    std::string label, value, note;
    bool enabled = true;
    float frac = 0;     // (a slider: where its knob is, 0..1)
    int act = -1;       // (a binding: the action)
    std::string keys[BIND_SLOTS];
};

const char* const kAnisoNames[5] = {"Game's own", "2x", "4x", "8x", "16x"};
const char* const kShadowResNames[4] = {"Game's own", "1024", "1536", "2048"};   // (input.ini shadow_resolution)
const char* const kShadowDistNames[4] = {"Game's own", "x1.5", "x2", "x3"};     // (input.ini shadow_distance)
const char* const kWaterNames[3] = {"Off", "Natural", "Clear"};                  // (input.ini water_reflections)
const char* const kSurfaceNames[3] = {"Off", "Soft", "Sharp"};                  // (input.ini surface_reflections)
const char* const kReflexNames[3] = {"Off", "On", "On + Boost"};
const int kFpsValues[8] = {30, 60, 90, 120, 144, 165, 200, 0};   // (200 at most: nn_vi.c FPS_MAX)
const char* const kFpsNames[8] = {"30", "60", "90", "120", "144", "165", "200", "Display's rate"};
int fps_index(int v) {
    for (int i = 0; i < 8; i++)
        if (kFpsValues[i] == v) return i;
    return 1;
}
const char* const kOutResNames[4] = {"1920 x 1080", "Display", "2560 x 1440", "3840 x 2160"};
const char* const kAspectNames[5] = {"16:9 (the game's own)", "The display's shape", "16:10", "21:9", "32:9"};
const char* const kCounterNames[3] = {"Off", "FPS", "FPS + frame time"};
const char* const kTimingNames[2] = {"Smooth", "Responsive"};
const char* const kFrameGenNames[4] = {"Off", "2x", "3x", "4x"};
const char* const kDlssModelNames[5] = {"NVIDIA's default", "K (transformer)", "M (transformer)", "L (transformer)",
                                        "J (transformer)"};
const char* const kDlssModelNotes[5] = {
    "Per mode: K for Quality / DLAA, M for Performance, L for Ultra Performance.",
    "Best quality, a little slower. Changes now (a short hitch).",
    "Made for Performance mode. Changes now (a short hitch).",
    "Made for Ultra Performance. Changes now (a short hitch).",
    "Like K: less ghosting, a little more flicker. Changes now."};
const char* const kButtonNames[5] = {"Automatic", "Keyboard", "Xbox", "PlayStation", "Switch"};

// the bindings page: the buttons, the d-pad, the sticks and walk (sections between)
const int kControlOrder[] = {ACT_A, ACT_B, ACT_X, ACT_Y, ACT_L, ACT_R, ACT_ZL, ACT_ZR, ACT_PLUS, ACT_MINUS, ACT_LS,
                             ACT_RS, ACT_DUP, ACT_DDOWN, ACT_DLEFT, ACT_DRIGHT, ACT_LUP, ACT_LDOWN, ACT_LLEFT,
                             ACT_LRIGHT, ACT_WALK, ACT_RUP, ACT_RDOWN, ACT_RLEFT, ACT_RRIGHT};

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::vector<Row> rows_of(int page, const Bindings& b) {
    std::vector<Row> r;
    auto add = [&](Kind k, Id id, std::string label, std::string value, std::string note = {}) -> Row& {
        r.push_back(Row{k, id, std::move(label), std::move(value), std::move(note)});
        return r.back();
    };
    if (page == kGraphics) {
        const bool scaler = fsr2_wanted();
        char now[96];
        std::snprintf(now, sizeof now, "The game's own size. Now %ux%u; at the next start.", hos_out_w, hos_out_h);
        add(kOption, kOutRes, "Output resolution", kOutResNames[std::clamp(b.output_res, 0, 3)], now);
        add(kOption, kAspect, "Aspect ratio", kAspectNames[std::clamp(b.aspect, 0, 4)],
            b.aspect ? "At the next start. More of the world at the sides; the HUD keeps its shape."
                     : "At the next start. Other shapes fill a wide or 16:10 screen.");
        Row& res = add(kOption, kRes, "3D resolution", hos_render_mode_name(b.render_mode),
                       scaler ? "The 3D scene's size before the upscaler." : "Needs FSR 2 or DLSS (set at launch).");
        res.enabled = scaler;
        add(kOption, kFps, "Frame rate limit", kFpsNames[fps_index(b.fps_limit)],
            b.fps_limit == 0 || b.fps_limit > 60 ? "Beta above 60: the camera may turn faster, Link may not climb steps."
                                                 : "The game runs at the right speed at any frame rate.");
        add(kOption, kFrameTiming, "Frame timing", kTimingNames[std::clamp(b.frame_timing, 0, 1)],
            b.frame_timing ? "Each frame steps the game by the last frame's time. For G-Sync / FreeSync."
                           : "Steps the game by the last 5 frames' time: evens out uneven frames.");
        {
            const int fg = present_framegen() ? std::clamp(present_framegen_mult(), 2, 4) - 1 : 0;
            add(kOption, kFrameGen, "Frame generation", kFrameGenNames[fg],
                fg ? "Frames made between the game's (F7). The game is held to the display's rate / this."
                   : "Frames made between the game's: smoother, about a frame later. F7 too.");
        }
        add(kOption, kFpsCounter, "FPS counter", kCounterNames[std::clamp(b.fps_counter, 0, 2)],
            "In the top left corner: frames shown a second.");
        add(kOption, kAniso, "Anisotropic filtering", kAnisoNames[std::clamp(b.aniso, 0, 4)],
            "Sharper ground and walls at an angle. Cheap on a PC.");
        add(kOption, kShadowRes, "Shadow resolution", kShadowResNames[std::clamp(b.shadow_res, 0, 3)],
            b.shadow_res ? "At the next start. Sharper, steadier sun shadows; more video memory."
                         : "At the next start. The game's own shrinks near the ground: blocky, jumpy leaf shadows.");
        add(kOption, kShadowDist, "Shadow distance", kShadowDistNames[std::clamp(b.shadow_dist, 0, 3)],
            b.shadow_dist ? "At the next start. Sharp shadows further out; raise Shadow resolution with it."
                          : "At the next start. Sharp shadows to 20 m, softer in steps to 160 m.");
        add(kOption, kReflections, "Water reflections", kWaterNames[std::clamp(b.water_reflections, 0, 2)],
            b.water_reflections ? "The world mirrored on the water: only what is on screen. Clear: fewer ripples."
                                : "Mirrors what is on screen in the water (screen-space reflections).");
        add(kOption, kSurfaceRefl, "Surface reflections", kSurfaceNames[std::clamp(b.surface_reflections, 0, 2)],
            "Polished stone, ice and metal mirror what is on screen. Soft: blurrier on rougher ones. Sharp: mirrors.");
        Row& rf = add(kOption, kReflex, "NVIDIA Reflex", kReflexNames[std::clamp(b.reflex, 0, 2)],
                      reflex_available() ? "Less input lag. Boost also keeps the GPU's clocks up."
                                         : "Needs an NVIDIA GPU on Vulkan.");
        rf.enabled = reflex_available();
        add(kOption, kButtons, "Button icons", kButtonNames[std::clamp(b.button_icons, 0, 4)],
            b.button_icons ? "Prompts in text change at the next start; the button guide now."
                           : "The device you play with. Prompts in text: as at the start.");
        add(kInfo, kUpscaler, "Upscaler",
            dlss_ready() ? "NVIDIA DLSS" : scaler ? "AMD FSR 2" : "The game's own",
            "Chosen when the game starts.");
        if (dlss_ready())
            add(kOption, kDlssModel, "DLSS model", kDlssModelNames[std::clamp(b.dlss_preset, 0, 4)],
                kDlssModelNotes[std::clamp(b.dlss_preset, 0, 4)]);
    } else if (page == kMouse) {
        add(kToggle, kMouseCam, "Mouse turns the camera", b.mouse_camera ? "On" : "Off",
            "Click the game to capture the mouse; Esc frees it.");
        add(kToggle, kInvert, "Invert up / down", b.mouse_invert_y ? "On" : "Off");
        add(kSlider, kSens, "Sensitivity", fmt("%.2f", b.mouse_sens)).frac = (float)((b.mouse_sens - 0.05) / (5.0 - 0.05));
        add(kSlider, kPitch, "Up / down speed", fmt("%.0f%%", b.mouse_pitch * 100),
            "Of the left / right speed.").frac = (float)((b.mouse_pitch - 0.25) / (2.5 - 0.25));
        add(kSlider, kHold, "Speed while Ultrahand holds", fmt("%.0f%%", b.mouse_hold * 100),
            "100% = the camera's speed.").frac = (float)((b.mouse_hold - 0.25) / (4.0 - 0.25));
        add(kSlider, kWalk, "Walk speed", fmt("%.0f%%", b.walk_scale * 100),
            "The left stick while Walk is held.").frac = (float)((b.walk_scale - 0.1) / (1.0 - 0.1));
    } else {
        for (size_t i = 0; i < sizeof kControlOrder / sizeof *kControlOrder; i++) {
            const int a = kControlOrder[i];
            if (i == 0) add(kSection, kBind, "Buttons", "");
            if (a == ACT_DUP) add(kSection, kBind, "D-pad", "");
            if (a == ACT_LUP) add(kSection, kBind, "Movement", "");
            if (a == ACT_RUP) add(kSection, kBind, "Camera", "");
            Row& row = add(kBinding, kBind, hos_bind_action_label(a), hos_bind_action_button(a));
            row.act = a;
            for (int s = 0; s < BIND_SLOTS; s++) row.keys[s] = b.key[a][s] ? hos_bind_key_name(b.key[a][s]) : "";
        }
        add(kButton, kResetControls, "Reset controls and mouse to the defaults", "");
    }
    return r;
}

bool selectable(const Row& r) { return r.kind != kSection && r.kind != kInfo && r.enabled; }

// ---- state (under mu) ----
struct State {
    std::mutex mu;
    bool open = false;
    int64_t opened_ns = 0;
    int page = kGraphics, row = 0, slot = 0;
    float scroll = 0;             // (the Controls page: how far it is scrolled, in layout pixels)
    int hover_tab = -1, hover_row = -1, hover_part = -1;
    bool dragging = false;
    int drag_row = -1;
    bool listening = false, listen_armed = false;
    int listen_act = -1, listen_slot = 0;
    uint8_t listen_prev[256]{};
    std::string status;
    int64_t status_until = 0;
    int w = 1920, h = 1080;       // (the window's size at the last draw: the layout for the mouse)
};
State& S() {
    static State s;
    return s;
}

void set_status(State& s, std::string t) {
    s.status = std::move(t);
    s.status_until = hos_now_ns() + 6000000000LL;
}

// ---- layout (1080-line pixels, scaled) ----
struct Rect {
    float x, y, w, h;
    bool in(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct Layout {
    float s;                      // scale: the window's height / 1080 (and its width / 1920, the smaller)
    Rect panel, content, tabs[kPages], close;
    float row_h, gap;
};
Layout layout(int w, int h, int page) {
    Layout l{};
    l.s = std::min(w / 1920.0f, h / 1080.0f);
    if (l.s <= 0) l.s = 1;
    const float s = l.s, pw = 1120 * s, ph = 990 * s;
    l.panel = {(w - pw) / 2, (h - ph) / 2, pw, ph};
    for (int i = 0; i < kPages; i++) l.tabs[i] = {l.panel.x + 48 * s + i * 186 * s, l.panel.y + 104 * s, 176 * s, 46 * s};
    l.close = {l.panel.x + pw - 48 * s - 44 * s, l.panel.y + 36 * s, 44 * s, 44 * s};
    l.content = {l.panel.x + 48 * s, l.panel.y + 178 * s, pw - 96 * s, ph - 178 * s - 112 * s};
    l.row_h = (page == kControls ? 50 : page == kGraphics ? 60 : 76) * s;   // (Graphics: up to 12 rows, it scrolls past 10)
    l.gap = 8 * s;
    return l;
}
float row_height(const Layout& l, const Row& r) { return r.kind == kSection ? l.row_h * 0.8f : l.row_h; }
// a row's rectangle (scrolled; not clipped)
Rect row_rect(const Layout& l, const std::vector<Row>& rows, size_t i, float scroll) {
    float y = l.content.y - scroll;
    for (size_t k = 0; k < i; k++) y += row_height(l, rows[k]) + l.gap;
    return {l.content.x, y, l.content.w, row_height(l, rows[i])};
}
float content_height(const Layout& l, const std::vector<Row>& rows) {
    float t = 0;
    for (const Row& r : rows) t += row_height(l, r) + l.gap;
    return t;
}
// the control's part of a row (right side), and its pieces
Rect control_rect(const Layout& l, const Rect& r) {
    const float w = 440 * l.s;
    return {r.x + r.w - w - 20 * l.s, r.y + 12 * l.s, w, r.h - 24 * l.s};
}
enum Part { kNone = -1, kLeft = 0, kRight = 1, kMain = 2, kSlot0 = 10 };
Rect part_rect(const Layout& l, const Row& row, const Rect& r, int part) {
    const Rect c = control_rect(l, r);
    const float s = l.s;
    switch (row.kind) {
    case kOption:
        if (part == kLeft) return {c.x, c.y, 52 * s, c.h};
        if (part == kRight) return {c.x + c.w - 52 * s, c.y, 52 * s, c.h};
        return c;
    case kSlider: return {c.x + 8 * s, c.y, c.w - 110 * s, c.h};
    case kToggle: return {c.x + c.w - 76 * s, c.y + (c.h - 36 * s) / 2, 76 * s, 36 * s};
    case kBinding: {
        const float bw = 180 * s, bh = r.h - 12 * s;
        return {r.x + r.w - 20 * s - (BIND_SLOTS - (part - kSlot0)) * (bw + 10 * s) + 10 * s, r.y + 6 * s, bw, bh};
    }
    case kButton: return {r.x + 8 * s, r.y + 6 * s, r.w - 16 * s, r.h - 12 * s};
    default: return c;
    }
}

// ---- applying ----
void change(State& s, const Row& r, int dir) {
    Bindings b;
    hos_bind_get(&b);
    const double step = dir;
    switch (r.id) {
    case kRes: b.render_mode = (b.render_mode + dir + hos_render_mode_count()) % hos_render_mode_count(); break;
    case kAniso: b.aniso = (b.aniso + dir + 5) % 5; break;
    case kShadowRes: b.shadow_res = (b.shadow_res + dir + 4) % 4; break;
    case kShadowDist: b.shadow_dist = (b.shadow_dist + dir + 4) % 4; break;
    case kReflections: b.water_reflections = (b.water_reflections + dir + 3) % 3; break;
    case kSurfaceRefl: b.surface_reflections = (b.surface_reflections + dir + 3) % 3; break;
    case kReflex: b.reflex = (b.reflex + dir + 3) % 3; break;
    case kFps: b.fps_limit = kFpsValues[(fps_index(b.fps_limit) + dir + 8) % 8]; break;
    case kOutRes: b.output_res = (b.output_res + dir + 4) % 4; break;
    case kAspect: b.aspect = (b.aspect + dir + 5) % 5; break;
    case kFpsCounter: b.fps_counter = (b.fps_counter + dir + 3) % 3; break;
    case kFrameTiming: b.frame_timing = !b.frame_timing; break;
    case kFrameGen: {   // (not a binding: the present settings, present.txt)
        const int fg = present_framegen() ? std::clamp(present_framegen_mult(), 2, 4) - 1 : 0;
        const int next = (fg + dir + 4) % 4;
        present_set_framegen(next ? next + 1 : 0);
        return;
    }
    case kDlssModel: b.dlss_preset = (b.dlss_preset + dir + 5) % 5; break;
    case kButtons: b.button_icons = (b.button_icons + dir + 5) % 5; break;
    case kMouseCam: b.mouse_camera = !b.mouse_camera; break;
    case kInvert: b.mouse_invert_y = !b.mouse_invert_y; break;
    case kSens: b.mouse_sens = std::clamp(b.mouse_sens + step * 0.05, 0.05, 5.0); break;
    case kPitch: b.mouse_pitch = std::clamp(b.mouse_pitch + step * 0.05, 0.25, 2.5); break;
    case kHold: b.mouse_hold = std::clamp(b.mouse_hold + step * 0.05, 0.25, 4.0); break;
    case kWalk: b.walk_scale = std::clamp(b.walk_scale + step * 0.05, 0.1, 1.0); break;
    default: return;
    }
    hos_bind_set(&b);
    (void)s;
}
void set_slider(const Row& r, float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    Bindings b;
    hos_bind_get(&b);
    auto q = [](double v, double lo, double hi, double step) { return std::clamp(std::round(v / step) * step, lo, hi); };
    switch (r.id) {
    case kSens: b.mouse_sens = q(0.05 + f * (5.0 - 0.05), 0.05, 5.0, 0.01); break;
    case kPitch: b.mouse_pitch = q(0.25 + f * (2.5 - 0.25), 0.25, 2.5, 0.01); break;
    case kHold: b.mouse_hold = q(0.25 + f * (4.0 - 0.25), 0.25, 4.0, 0.01); break;
    case kWalk: b.walk_scale = q(0.1 + f * (1.0 - 0.1), 0.1, 1.0, 0.01); break;
    default: return;
    }
    hos_bind_set(&b);
}
void reset_controls(State& s) {
    Bindings b, d;
    hos_bind_get(&b);
    hos_bind_defaults(&d);
    d.render_mode = b.render_mode, d.aniso = b.aniso, d.reflex = b.reflex;   // (the graphics settings: kept)
    d.button_icons = b.button_icons, d.output_res = b.output_res, d.fps_limit = b.fps_limit;
    d.fps_counter = b.fps_counter, d.aspect = b.aspect, d.frame_timing = b.frame_timing, d.dlss_preset = b.dlss_preset;
    d.shadow_res = b.shadow_res, d.shadow_dist = b.shadow_dist, d.water_reflections = b.water_reflections;
    d.surface_reflections = b.surface_reflections;
    hos_bind_set(&d);
    set_status(s, "Controls and mouse settings are back to the defaults.");
}
void start_listen(State& s, int act, int slot) {
    s.listening = true;
    s.listen_armed = false;
    s.listen_act = act;
    s.listen_slot = slot;
    set_status(s, std::string("Press a key or mouse button for ") + hos_bind_action_label(act) +
                      ". Esc cancels, Delete clears.");
}
void bind_to(State& s, int code) {
    Bindings b;
    hos_bind_get(&b);
    const int act = s.listen_act, slot = s.listen_slot;
    b.key[act][slot] = (uint16_t)code;
    if (code && b.key[act][slot ^ 1] == code) b.key[act][slot ^ 1] = 0;
    hos_bind_set(&b);
    s.listening = false;
    std::string m = code ? std::string(hos_bind_key_name(code)) + " now on " + hos_bind_action_label(act) + "."
                         : std::string(hos_bind_action_label(act)) + ": cleared.";
    if (code)
        for (int a = 0; a < ACT_COUNT; a++)
            for (int k = 0; k < BIND_SLOTS; k++)
                if (a != act && b.key[a][k] == code) {
                    m += std::string(" Also on ") + hos_bind_action_label(a) + ": both happen.";
                    a = ACT_COUNT;
                    break;
                }
    set_status(s, m);
}
void activate(State& s, const Row& r, int part) {
    switch (r.kind) {
    case kOption: change(s, r, part == kLeft ? -1 : 1); break;
    case kToggle: change(s, r, 1); break;
    case kBinding: start_listen(s, r.act, part >= kSlot0 ? part - kSlot0 : s.slot); break;
    case kButton:
        if (r.id == kResetControls) reset_controls(s);
        break;
    default: break;
    }
}
// the selected row inside the visible area (Controls scrolls)
void keep_visible(State& s, const Layout& l, const std::vector<Row>& rows) {
    if (rows.empty()) return;
    const float max_scroll = std::max(0.0f, content_height(l, rows) - l.content.h);
    const Rect r = row_rect(l, rows, (size_t)s.row, 0);
    const float top = r.y - l.content.y, bottom = top + r.h;
    if (top - 40 * l.s < s.scroll) s.scroll = top - 40 * l.s;   // (the section title above it too)
    if (bottom > s.scroll + l.content.h) s.scroll = bottom - l.content.h;
    s.scroll = std::clamp(s.scroll, 0.0f, max_scroll);
}
void move_row(State& s, const std::vector<Row>& rows, int dir) {
    if (rows.empty()) return;
    int r = s.row;
    for (size_t n = 0; n < rows.size(); n++) {
        r = (r + dir + (int)rows.size()) % (int)rows.size();
        if (selectable(rows[(size_t)r])) break;
    }
    s.row = r;
}
void first_row(State& s, const std::vector<Row>& rows) {
    s.row = 0;
    if (!rows.empty() && !selectable(rows[0])) move_row(s, rows, 1);
}
void set_page(State& s, int p) {
    s.page = (p + kPages) % kPages;
    s.scroll = 0;
    s.slot = 0;
    Bindings b;
    hos_bind_get(&b);
    first_row(s, rows_of(s.page, b));
}

// ---- the font atlas (GDI, Segoe UI) ----
// the characters drawn: ASCII, and a few more (their slots after 127)
const wchar_t kExtra[] = {0x2039, 0x203A, 0x2191, 0x2193, 0x2190, 0x2192, 0x00B7, 0x2026, 0x2013, 0x00D7};
constexpr int kGlyphs = 128 + sizeof kExtra / sizeof *kExtra;
int glyph_index(uint32_t cp) {
    if (cp >= 32 && cp < 127) return (int)cp;
    for (int i = 0; i < (int)(sizeof kExtra / sizeof *kExtra); i++)
        if (kExtra[i] == cp) return 128 + i;
    return '?';
}
struct Font {
    float px = 0, ascent = 0, height = 0;
    float adv[kGlyphs]{}, cw[kGlyphs]{};
    float u0[kGlyphs]{}, v0[kGlyphs]{}, u1[kGlyphs]{}, v1[kGlyphs]{};
};
enum FontId { kBody, kSmall, kTitle, kStrong, kFonts };

struct Gpu {
    bool ready = false, failed = false;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout layout = nullptr;
    WGPUBindGroup group = nullptr;
    WGPUBuffer uniforms = nullptr, instances = nullptr;
    uint64_t instance_bytes = 0;
    WGPUTexture atlas = nullptr;
    WGPUTextureView atlas_view = nullptr;
    WGPUSampler sampler = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    float atlas_scale = 0;
    Font fonts[kFonts];
};
Gpu& G() {
    static Gpu g;
    return g;
}

// UTF-8 to code points (the strings here are ASCII and a few symbols)
std::vector<uint32_t> decode(const std::string& t) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < t.size();) {
        const uint8_t c = (uint8_t)t[i];
        uint32_t cp = c;
        int n = 1;
        if (c >= 0xF0) cp = c & 7, n = 4;
        else if (c >= 0xE0) cp = c & 15, n = 3;
        else if (c >= 0xC0) cp = c & 31, n = 2;
        for (int k = 1; k < n && i + k < t.size(); k++) cp = cp << 6 | ((uint8_t)t[i + k] & 63);
        out.push_back(cp);
        i += n;
    }
    return out;
}

bool build_atlas(Gpu& g, float scale) {
    constexpr int W = 2048, H = 1024;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    std::memset(bits, 0, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    struct Spec {
        float px;
        int weight;
    } const specs[kFonts] = {{22, FW_NORMAL}, {17, FW_NORMAL}, {36, FW_SEMIBOLD}, {22, FW_SEMIBOLD}};
    int x = 0, y = 0, line = 0;
    for (int f = 0; f < kFonts; f++) {
        Font& font = g.fonts[f];
        font.px = std::max(8.0f, std::round(specs[f].px * scale));
        HFONT hf = CreateFontW(-(int)font.px, 0, 0, 0, specs[f].weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HGDIOBJ old_font = SelectObject(dc, hf);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        font.ascent = (float)tm.tmAscent;
        font.height = (float)tm.tmHeight;
        const int pad = 2, ch = tm.tmHeight + 2 * pad;
        for (int i = 32; i < kGlyphs; i++) {
            if (i >= 127 && i < 128) continue;
            const wchar_t wc = i < 128 ? (wchar_t)i : kExtra[i - 128];
            SIZE sz{};
            GetTextExtentPoint32W(dc, &wc, 1, &sz);
            const int cw = sz.cx + 2 * pad + 2;   // (room for overhangs)
            if (x + cw > W) x = 0, y += line, line = 0;
            if (y + ch > H) break;
            TextOutW(dc, x + pad, y + pad, &wc, 1);
            font.adv[i] = (float)sz.cx;
            font.cw[i] = (float)cw;
            font.u0[i] = (float)x / W, font.v0[i] = (float)y / H;
            font.u1[i] = (float)(x + cw) / W, font.v1[i] = (float)(y + ch) / H;
            x += cw;
            line = std::max(line, ch);
        }
        SelectObject(dc, old_font);
        DeleteObject(hf);
    }
    GdiFlush();
    std::vector<uint8_t> a((size_t)W * H);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < a.size(); i++) a[i] = std::max({p[i * 4], p[i * 4 + 1], p[i * 4 + 2]});
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);

    if (g.atlas_view) wgpuTextureViewRelease(g.atlas_view);
    if (g.atlas) wgpuTextureRelease(g.atlas);
    WGPUTextureDescriptor td{};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {W, H, 1};
    td.format = WGPUTextureFormat_R8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    g.atlas = wgpuDeviceCreateTexture(dev.device, &td);
    g.atlas_view = wgpuTextureCreateView(g.atlas, nullptr);
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = g.atlas;
    WGPUTexelCopyBufferLayout bl{};
    bl.bytesPerRow = W;
    bl.rowsPerImage = H;
    const WGPUExtent3D ext{W, H, 1};
    wgpuQueueWriteTexture(dev.queue, &dst, a.data(), a.size(), &bl, &ext);
    g.atlas_scale = scale;
    return true;
}

const char* const kWgsl = R"(
struct U { screen: vec2f, pad: vec2f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var atlas: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
struct I {
    @location(0) rect: vec4f,     // x, y, w, h (pixels)
    @location(1) uv: vec4f,       // glyphs: the atlas's u0, v0, u1, v1
    @location(2) color: vec4f,    // straight alpha
    @location(3) params: vec4f,   // corner radius, kind (0 shape, 1 glyph, 2 outline), outline width, shade
};
struct V {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) size: vec2f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
    @location(4) params: vec4f,
};
@vertex fn vs(@builtin(vertex_index) vi: u32, i: I) -> V {
    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(0.0, 1.0), vec2f(1.0, 0.0),
                                  vec2f(1.0, 1.0));
    let c = corners[vi];
    let p = i.rect.xy + c * i.rect.zw;
    var o: V;
    o.pos = vec4f(p.x / u.screen.x * 2.0 - 1.0, 1.0 - p.y / u.screen.y * 2.0, 0.0, 1.0);
    o.local = c * i.rect.zw;
    o.size = i.rect.zw;
    o.uv = mix(i.uv.xy, i.uv.zw, c);
    o.color = i.color;
    o.params = i.params;
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let glyph = textureSampleLevel(atlas, samp, v.uv, 0.0).r;
    if (v.params.y > 0.5 && v.params.y < 1.5) {
        return vec4f(v.color.rgb, v.color.a * glyph);
    }
    let r = min(v.params.x, min(v.size.x, v.size.y) * 0.5);
    let h = v.size * 0.5;
    let q = abs(v.local - h) - (h - vec2f(r));
    let d = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
    var a = clamp(0.5 - d, 0.0, 1.0);
    if (v.params.y > 1.5) {
        a = a * clamp(0.5 + d + v.params.z, 0.0, 1.0);
    }
    let shade = 1.0 - v.params.w * (v.local.y / max(v.size.y, 1.0));
    return vec4f(v.color.rgb * shade, v.color.a * a);
}
)";

bool make_gpu(Gpu& g, WGPUTextureFormat format) {
    if (g.failed) return false;
    if (g.ready && g.format == format) return true;
    scope_push();
    if (!g.layout) {
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0;
        e[0].visibility = WGPUShaderStage_Vertex;
        e[0].buffer.type = WGPUBufferBindingType_Uniform;
        e[0].buffer.minBindingSize = 16;
        e[1].binding = 1;
        e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float;
        e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2;
        e[2].visibility = WGPUShaderStage_Fragment;
        e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{};
        ld.entryCount = 3;
        ld.entries = e;
        g.layout = wgpuDeviceCreateBindGroupLayout(dev.device, &ld);
        WGPUBufferDescriptor ud{};
        ud.size = 16;
        ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        g.uniforms = wgpuDeviceCreateBuffer(dev.device, &ud);
        WGPUSamplerDescriptor sd{};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.lodMaxClamp = 1;
        sd.maxAnisotropy = 1;
        g.sampler = wgpuDeviceCreateSampler(dev.device, &sd);
    }
    if (g.pipeline) wgpuRenderPipelineRelease(g.pipeline);
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = sv(kWgsl);
    WGPUShaderModuleDescriptor md{};
    md.nextInChain = &src.chain;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(dev.device, &md);
    WGPUPipelineLayoutDescriptor pd{};
    pd.bindGroupLayoutCount = 1;
    pd.bindGroupLayouts = &g.layout;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(dev.device, &pd);
    WGPUVertexAttribute attrs[4]{};
    for (int i = 0; i < 4; i++) {
        attrs[i].format = WGPUVertexFormat_Float32x4;
        attrs[i].offset = (uint64_t)i * 16;
        attrs[i].shaderLocation = (uint32_t)i;
    }
    WGPUVertexBufferLayout vbl{};
    vbl.stepMode = WGPUVertexStepMode_Instance;
    vbl.arrayStride = 64;
    vbl.attributeCount = 4;
    vbl.attributes = attrs;
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState ct{};
    ct.format = format;
    ct.blend = &blend;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs{};
    fs.module = mod;
    fs.entryPoint = sv("fs");
    fs.targetCount = 1;
    fs.targets = &ct;
    WGPURenderPipelineDescriptor rd{};
    rd.layout = pl;
    rd.vertex.module = mod;
    rd.vertex.entryPoint = sv("vs");
    rd.vertex.bufferCount = 1;
    rd.vertex.buffers = &vbl;
    rd.fragment = &fs;
    rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rd.multisample.count = 1;
    rd.multisample.mask = ~0u;
    g.pipeline = wgpuDeviceCreateRenderPipeline(dev.device, &rd);
    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(mod);
    const std::string err = scope_pop();
    if (!err.empty() || !g.pipeline) {
        hos_log("rx: settings menu: not made, F1 opens the old window: %s", err.c_str());
        g.failed = true;
        return false;
    }
    g.format = format;
    g.ready = true;
    return true;
}

// ---- drawing ----
struct Inst {
    float rect[4], uv[4], color[4], params[4];
};
struct Clip {
    uint32_t first, count;
    Rect scissor;   // (w 0: none)
};
struct Painter {
    std::vector<Inst> inst;
    std::vector<Clip> clips;
    Rect scissor{0, 0, 0, 0};
    float alpha = 1;   // (the fade in)
    void begin_clip(Rect r) {
        scissor = r;
        clips.push_back({(uint32_t)inst.size(), 0, r});
    }
    void end_clip() {
        scissor = {0, 0, 0, 0};
        clips.push_back({(uint32_t)inst.size(), 0, scissor});
    }
    void push(const Inst& i) {
        if (clips.empty()) clips.push_back({0, 0, {0, 0, 0, 0}});
        inst.push_back(i);
        clips.back().count++;
    }
    void rect(Rect r, const float c[4], float radius, float shade = 0) {
        push({{r.x, r.y, r.w, r.h}, {0, 0, 0, 0}, {c[0], c[1], c[2], c[3] * alpha}, {radius, 0, 0, shade}});
    }
    void outline(Rect r, const float c[4], float radius, float width) {
        push({{r.x, r.y, r.w, r.h}, {0, 0, 0, 0}, {c[0], c[1], c[2], c[3] * alpha}, {radius, 2, width, 0}});
    }
    float measure(FontId f, const std::string& t) {
        const Font& font = G().fonts[f];
        float w = 0;
        for (uint32_t cp : decode(t)) w += font.adv[glyph_index(cp)];
        return w;
    }
    // text at x, its line's top at y; returns its width
    float text(FontId f, float x, float y, const std::string& t, const float c[4]) {
        const Font& font = G().fonts[f];
        const float x0 = x;
        x = std::round(x), y = std::round(y);
        for (uint32_t cp : decode(t)) {
            const int g = glyph_index(cp);
            if (cp != ' ')
                push({{x - 2, y - 2, font.cw[g], font.height + 4}, {font.u0[g], font.v0[g], font.u1[g], font.v1[g]},
                      {c[0], c[1], c[2], c[3] * alpha}, {0, 1, 0, 0}});
            x += font.adv[g];
        }
        return x - x0;
    }
    // text vertically centered in r, at x (align 0 left, 1 center, 2 right of r)
    void text_in(FontId f, Rect r, const std::string& t, const float c[4], int align, float pad = 0) {
        const float w = measure(f, t), h = G().fonts[f].height;
        const float x = align == 0 ? r.x + pad : align == 1 ? r.x + (r.w - w) / 2 : r.x + r.w - w - pad;
        text(f, x, r.y + (r.h - h) / 2, t, c);
    }
    // a text cut to fit (an ellipsis)
    std::string fit(FontId f, std::string t, float w) {
        if (measure(f, t) <= w) return t;
        while (!t.empty() && measure(f, t + "\xE2\x80\xA6") > w) t.pop_back();
        return t + "\xE2\x80\xA6";
    }
};

// colors (sRGB, straight alpha: the window's image is not sRGB-encoded by the GPU)
const float kDim[4] = {0.0f, 0.0f, 0.0f, 0.55f};
const float kPanel[4] = {0.075f, 0.082f, 0.105f, 0.94f};
const float kPanelLine[4] = {1, 1, 1, 0.08f};
const float kRowBg[4] = {1, 1, 1, 0.035f};
const float kRowHover[4] = {1, 1, 1, 0.06f};
const float kAccent[4] = {0.37f, 0.83f, 0.91f, 1};
const float kAccentSoft[4] = {0.37f, 0.83f, 0.91f, 0.14f};
const float kText[4] = {0.93f, 0.94f, 0.96f, 1};
const float kText2[4] = {0.60f, 0.63f, 0.68f, 1};
const float kText3[4] = {0.42f, 0.45f, 0.50f, 1};
const float kControl[4] = {1, 1, 1, 0.07f};
const float kControlHover[4] = {1, 1, 1, 0.13f};
const float kGold[4] = {0.91f, 0.77f, 0.42f, 1};
const float kOnDark[4] = {0.05f, 0.07f, 0.09f, 1};

void paint(Painter& p, State& s, int w, int h) {
    Bindings b;
    hos_bind_get(&b);
    const std::vector<Row> rows = rows_of(s.page, b);
    if (s.row >= (int)rows.size() || (s.row >= 0 && !selectable(rows[(size_t)s.row]))) first_row(s, rows);
    const Layout l = layout(w, h, s.page);
    const float sc = l.s;
    const float t = std::min(1.0f, (float)(hos_now_ns() - s.opened_ns) / 120e6f);
    p.alpha = t;

    p.rect({0, 0, (float)w, (float)h}, kDim, 0);
    // the panel: a soft shadow, the body, a hairline
    const float shadow[4] = {0, 0, 0, 0.35f};
    p.rect({l.panel.x - 10 * sc, l.panel.y - 6 * sc, l.panel.w + 20 * sc, l.panel.h + 26 * sc}, shadow, 30 * sc);
    p.rect(l.panel, kPanel, 22 * sc, 0.25f);
    p.outline(l.panel, kPanelLine, 22 * sc, 1);
    // title
    p.text(kTitle, l.panel.x + 48 * sc, l.panel.y + 36 * sc, "Settings", kText);
    const float tw = p.measure(kTitle, "Settings");
    p.text(kSmall, l.panel.x + 48 * sc + tw + 16 * sc, l.panel.y + 50 * sc, "SkyboundTOTK", kGold);
    // close
    p.rect(l.close, s.hover_part == 99 ? kControlHover : kControl, 12 * sc);
    p.text_in(kStrong, l.close, "\xC3\x97", kText, 1);
    // tabs
    for (int i = 0; i < kPages; i++) {
        const bool on = i == s.page;
        if (on) p.rect(l.tabs[i], kAccent, 23 * sc);
        else p.rect(l.tabs[i], s.hover_tab == i ? kControlHover : kControl, 23 * sc);
        p.text_in(kStrong, l.tabs[i], kPageNames[i], on ? kOnDark : kText, 1);
    }
    // rows
    const float max_scroll = std::max(0.0f, content_height(l, rows) - l.content.h);
    s.scroll = std::clamp(s.scroll, 0.0f, max_scroll);
    p.begin_clip(l.content);
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& row = rows[i];
        const Rect r = row_rect(l, rows, i, s.scroll);
        if (r.y + r.h < l.content.y || r.y > l.content.y + l.content.h) continue;
        if (row.kind == kSection) {
            p.text(kSmall, r.x + 4 * sc, r.y + r.h - G().fonts[kSmall].height - 6 * sc, row.label, kGold);
            continue;
        }
        const bool sel = (int)i == s.row, hover = (int)i == s.hover_row;
        p.rect(r, sel ? kAccentSoft : hover ? kRowHover : kRowBg, 14 * sc);
        if (sel) p.rect({r.x, r.y + 14 * sc, 4 * sc, r.h - 28 * sc}, kAccent, 2 * sc);
        const float* label_c = row.enabled ? kText : kText3;
        const bool two_lines = !row.note.empty() && s.page != kControls;
        if (row.kind == kBinding) {
            p.text_in(kStrong, {r.x + 24 * sc, r.y, 60 * sc, r.h}, row.value, kGold, 0);
            p.text_in(kBody, {r.x + 84 * sc, r.y, r.w - 500 * sc, r.h}, row.label, label_c, 0);
        } else if (row.kind == kButton) {
            const Rect br = part_rect(l, row, r, kMain);
            p.rect(br, sel || hover ? kControlHover : kControl, 12 * sc);
            p.text_in(kStrong, br, row.label, kText, 1);
            continue;
        } else if (two_lines) {
            // (the two lines centred in the row: Graphics' rows are shorter)
            const float top = r.y + std::max(4 * sc, (r.h - G().fonts[kBody].height - G().fonts[kSmall].height) / 2);
            p.text(kBody, r.x + 24 * sc, top, row.label, label_c);
            p.text(kSmall, r.x + 24 * sc, top + G().fonts[kBody].height, p.fit(kSmall, row.note, r.w - 520 * sc), kText2);
        } else {
            p.text_in(kBody, {r.x + 24 * sc, r.y, r.w, r.h}, row.label, label_c, 0);
        }
        const Rect c = control_rect(l, r);
        switch (row.kind) {
        case kOption: {
            const Rect lr = part_rect(l, row, r, kLeft), rr = part_rect(l, row, r, kRight);
            p.rect(c, kControl, 12 * sc);
            if (row.enabled) {
                p.rect(lr, hover && s.hover_part == kLeft ? kControlHover : kControl, 12 * sc);
                p.rect(rr, hover && s.hover_part == kRight ? kControlHover : kControl, 12 * sc);
                p.text_in(kTitle, lr, "\xE2\x80\xB9", sel ? kAccent : kText2, 1);
                p.text_in(kTitle, rr, "\xE2\x80\xBA", sel ? kAccent : kText2, 1);
            }
            p.text_in(kStrong, {lr.x + lr.w, c.y, c.w - lr.w - rr.w, c.h},
                      p.fit(kStrong, row.value, c.w - lr.w - rr.w - 16 * sc), row.enabled ? kText : kText3, 1);
            break;
        }
        case kSlider: {
            const Rect tr = part_rect(l, row, r, kMain);
            const float ty = tr.y + tr.h / 2;
            p.rect({tr.x, ty - 3 * sc, tr.w, 6 * sc}, kControl, 3 * sc);
            p.rect({tr.x, ty - 3 * sc, tr.w * std::clamp(row.frac, 0.0f, 1.0f), 6 * sc}, kAccent, 3 * sc);
            const float kx = tr.x + tr.w * std::clamp(row.frac, 0.0f, 1.0f);
            const float kr = (sel || hover ? 11 : 9) * sc;
            p.rect({kx - kr, ty - kr, 2 * kr, 2 * kr}, kText, kr);
            p.text_in(kStrong, {tr.x + tr.w, c.y, c.x + c.w - tr.x - tr.w, c.h}, row.value, kText, 2);
            break;
        }
        case kToggle: {
            const Rect tr = part_rect(l, row, r, kMain);
            const bool on = row.value == "On";
            p.rect(tr, on ? kAccent : kControlHover, tr.h / 2);
            const float kr = tr.h / 2 - 4 * sc;
            const float kx = on ? tr.x + tr.w - 4 * sc - 2 * kr : tr.x + 4 * sc;
            p.rect({kx, tr.y + 4 * sc, 2 * kr, 2 * kr}, on ? kOnDark : kText, kr);
            break;
        }
        case kInfo:
            p.text_in(kStrong, c, row.value, kGold, 2, 8 * sc);
            break;
        case kBinding:
            for (int k = 0; k < BIND_SLOTS; k++) {
                const Rect kr = part_rect(l, row, r, kSlot0 + k);
                const bool waiting = s.listening && s.listen_act == row.act && s.listen_slot == k;
                const bool focus = sel && s.slot == k;
                p.rect(kr, waiting ? kAccent : hover && s.hover_part == kSlot0 + k ? kControlHover : kControl, 10 * sc);
                if (focus && !waiting) p.outline(kr, kAccent, 10 * sc, 2 * sc);
                const std::string label = waiting ? "Press a key\xE2\x80\xA6" : row.keys[k].empty() ? "\xE2\x80\x93"
                                                                                                    : row.keys[k];
                p.text_in(kStrong, kr, p.fit(kStrong, label, kr.w - 12 * sc), waiting ? kOnDark : row.keys[k].empty() ? kText3 : kText, 1);
            }
            break;
        default: break;
        }
    }
    p.end_clip();
    // the scroll bar (Controls)
    if (max_scroll > 0) {
        const float frac = l.content.h / (l.content.h + max_scroll);
        const float bh = l.content.h * frac, by = l.content.y + (l.content.h - bh) * (s.scroll / max_scroll);
        p.rect({l.panel.x + l.panel.w - 28 * sc, by, 5 * sc, bh}, kControlHover, 3 * sc);
    }
    // the status line and the keys
    const float fy = l.panel.y + l.panel.h - 92 * sc;
    if (!s.status.empty() && hos_now_ns() < s.status_until) p.text(kBody, l.panel.x + 48 * sc, fy, s.status, kText);
    else if (s.row >= 0 && s.row < (int)rows.size() && s.page == kControls)
        p.text(kBody, l.panel.x + 48 * sc, fy, "Enter or click a key to change it. Delete clears it.", kText2);
    // the keys, as key caps
    p.rect({l.panel.x + 48 * sc, l.panel.y + l.panel.h - 58 * sc, l.panel.w - 96 * sc, 1 * sc}, kPanelLine, 0);
    struct Hint {
        const char *key, *what;
    };
    const Hint hints[] = {{"\xE2\x86\x91\xE2\x86\x93", "Select"},
                          {"\xE2\x86\x90\xE2\x86\x92", s.page == kControls ? "Key slot" : "Change"},
                          {"Enter", s.page == kControls ? "Rebind" : "Toggle"},
                          {"Tab", "Next page"},
                          {"Esc", "Close"}};
    float hx = l.panel.x + 48 * sc;
    const float hy = l.panel.y + l.panel.h - 44 * sc, kh = G().fonts[kSmall].height + 6 * sc;
    for (const Hint& hint : hints) {
        const float kw = std::max(kh, p.measure(kSmall, hint.key) + 16 * sc);
        p.rect({hx, hy, kw, kh}, kControlHover, 6 * sc);
        p.text_in(kSmall, {hx, hy, kw, kh}, hint.key, kText, 1);
        hx += kw + 8 * sc;
        hx += p.text(kSmall, hx, hy + 3 * sc, hint.what, kText2) + 24 * sc;
    }
}

// The FPS counter (fps_counter in input.ini): frames shown a second over the last half second, and with 2 their
// average time and the longest gap in that time, in a small box in the top left corner. Counted here, at each present
// (with frame interpolation: the frames shown, interpolated ones included).
struct Counter {
    int64_t window_start = 0, last = 0, longest = 0;
    int frames = 0;
    float fps = 0, ms = 0, worst = 0;
} counter;
void count_present() {
    Counter& c = counter;
    const int64_t now = hos_now_ns();
    if (c.last) c.longest = std::max(c.longest, now - c.last);
    if (!c.window_start || now - c.last > 1000000000LL) c.window_start = now, c.frames = 0, c.longest = 0;
    c.last = now;
    c.frames++;
    const int64_t span = now - c.window_start;
    if (span >= 500000000LL && c.frames > 1) {
        c.fps = (float)((c.frames - 1) * 1e9 / (double)span);
        c.ms = (float)(span / 1e6 / (c.frames - 1));
        c.worst = (float)(c.longest / 1e6);
        c.window_start = now, c.frames = 1, c.longest = 0;
    }
}
void paint_counter(Painter& p, int w, int h, int mode) {
    const float sc = std::min(w / 1920.0f, h / 1080.0f);
    char a[32], b[64];
    std::snprintf(a, sizeof a, "%.0f FPS", counter.fps);
    std::snprintf(b, sizeof b, "%.1f ms  (longest %.1f)", counter.ms, counter.worst);
    const float pad = 10 * sc, line = G().fonts[kStrong].height;
    float bw = p.measure(kStrong, a) + 2 * pad, bh = line + 2 * pad * 0.6f;
    if (mode == 2) bw = std::max(bw, p.measure(kSmall, b) + 2 * pad), bh += G().fonts[kSmall].height;
    const Rect box{16 * sc, 16 * sc, bw, bh};
    const float bg[4] = {0, 0, 0, 0.55f};
    p.rect(box, bg, 8 * sc);
    const float* c = counter.fps >= 55 ? kAccent : counter.fps >= 28 ? kGold : kText;
    p.text(kStrong, box.x + pad, box.y + pad * 0.6f, a, c);
    if (mode == 2) p.text(kSmall, box.x + pad, box.y + pad * 0.6f + line, b, kText2);
}

// the mouse over the menu: what it points at
struct Hit {
    int tab = -1, row = -1, part = kNone;
    bool close = false, inside = false;
};
Hit hit_test(State& s, int x, int y) {
    Hit h;
    Bindings b;
    hos_bind_get(&b);
    const std::vector<Row> rows = rows_of(s.page, b);
    const Layout l = layout(s.w, s.h, s.page);
    h.inside = l.panel.in((float)x, (float)y);
    if (l.close.in((float)x, (float)y)) {
        h.close = true;
        return h;
    }
    for (int i = 0; i < kPages; i++)
        if (l.tabs[i].in((float)x, (float)y)) h.tab = i;
    if (!l.content.in((float)x, (float)y)) return h;
    for (size_t i = 0; i < rows.size(); i++) {
        const Rect r = row_rect(l, rows, i, s.scroll);
        if (!r.in((float)x, (float)y) || !selectable(rows[i])) continue;
        h.row = (int)i;
        const Row& row = rows[i];
        if (row.kind == kOption) {
            h.part = part_rect(l, row, r, kLeft).in((float)x, (float)y)    ? kLeft
                     : part_rect(l, row, r, kRight).in((float)x, (float)y) ? kRight
                                                                           : kMain;
        } else if (row.kind == kBinding) {
            for (int k = 0; k < BIND_SLOTS; k++)
                if (part_rect(l, row, r, kSlot0 + k).in((float)x, (float)y)) h.part = kSlot0 + k;
        } else {
            h.part = kMain;
        }
    }
    return h;
}

// a slider dragged to x
void drag_to(State& s, int x) {
    Bindings b;
    hos_bind_get(&b);
    const std::vector<Row> rows = rows_of(s.page, b);
    if (s.drag_row < 0 || s.drag_row >= (int)rows.size()) return;
    const Layout l = layout(s.w, s.h, s.page);
    const Row& row = rows[(size_t)s.drag_row];
    const Rect tr = part_rect(l, row, row_rect(l, rows, (size_t)s.drag_row, s.scroll), kMain);
    set_slider(row, (x - tr.x) / std::max(1.0f, tr.w));
}

bool reserved_key(int vk) { return vk == VK_ESCAPE || vk == VK_F1 || vk == VK_F4 || (vk >= VK_F6 && vk <= VK_F12); }

}  // namespace

// ---- the renderer's side ----
bool menu_open() { return S().open || hos_fps_counter; }

void menu_draw(WGPUCommandEncoder e, WGPUTextureView target, WGPUTextureFormat format, uint32_t w, uint32_t h) {
    State& s = S();
    const int counter_mode = hos_fps_counter;
    if (counter_mode) count_present();
    if ((!s.open && !counter_mode) || !w || !h) return;
    Gpu& g = G();
    if (g.failed) return;
    if (!make_gpu(g, format)) {   // (no menu here: closed, so the game has its input back; F1 opens the old window)
        std::lock_guard lk(s.mu);
        s.open = false;
        hos_bind_ui_active = 0;
        return;
    }
    const float scale = std::min(w / 1920.0f, h / 1080.0f);
    if (!g.atlas || std::fabs(scale - g.atlas_scale) > 0.01f) {
        if (!build_atlas(g, scale)) {
            hos_log("rx: settings menu: no font atlas (GDI); F1 opens the old window");
            g.failed = true;
            std::lock_guard lk(s.mu);
            s.open = false;
            hos_bind_ui_active = 0;
            return;
        }
        if (g.group) wgpuBindGroupRelease(g.group), g.group = nullptr;
    }
    Painter p;
    {
        std::lock_guard lk(s.mu);
        s.w = (int)w, s.h = (int)h;
        if (s.open) paint(p, s, (int)w, (int)h);
    }
    if (counter_mode) {
        p.alpha = 1;
        p.end_clip();
        paint_counter(p, (int)w, (int)h, counter_mode);
    }
    if (p.inst.empty()) return;
    const uint64_t bytes = p.inst.size() * sizeof(Inst);
    if (bytes > g.instance_bytes) {
        if (g.instances) wgpuBufferRelease(g.instances);
        WGPUBufferDescriptor bd{};
        bd.size = std::max<uint64_t>(bytes * 2, 64 << 10);
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        g.instances = wgpuDeviceCreateBuffer(dev.device, &bd);
        g.instance_bytes = bd.size;
    }
    wgpuQueueWriteBuffer(dev.queue, g.instances, 0, p.inst.data(), bytes);
    const float u[4] = {(float)w, (float)h, 0, 0};
    wgpuQueueWriteBuffer(dev.queue, g.uniforms, 0, u, sizeof u);
    if (!g.group) {
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0;
        be[0].buffer = g.uniforms;
        be[0].size = 16;
        be[1].binding = 1;
        be[1].textureView = g.atlas_view;
        be[2].binding = 2;
        be[2].sampler = g.sampler;
        WGPUBindGroupDescriptor bd{};
        bd.layout = g.layout;
        bd.entryCount = 3;
        bd.entries = be;
        g.group = wgpuDeviceCreateBindGroup(dev.device, &bd);
    }
    WGPURenderPassColorAttachment ca{};
    ca.view = target;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pd{};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(e, &pd);
    wgpuRenderPassEncoderSetPipeline(pass, g.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.group, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, g.instances, 0, bytes);
    for (const Clip& c : p.clips) {
        if (!c.count) continue;
        if (c.scissor.w > 0) {
            const uint32_t x0 = (uint32_t)std::clamp(c.scissor.x, 0.0f, (float)w), y0 = (uint32_t)std::clamp(c.scissor.y, 0.0f, (float)h);
            const uint32_t x1 = (uint32_t)std::clamp(c.scissor.x + c.scissor.w, 0.0f, (float)w);
            const uint32_t y1 = (uint32_t)std::clamp(c.scissor.y + c.scissor.h, 0.0f, (float)h);
            if (x1 <= x0 || y1 <= y0) continue;
            wgpuRenderPassEncoderSetScissorRect(pass, x0, y0, x1 - x0, y1 - y0);
        } else {
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, w, h);
        }
        wgpuRenderPassEncoderDraw(pass, 6, c.count, 0, c.first);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

}  // namespace rx

// ---- the window's side (display.c) ----
extern "C" {
int rx_menu_is_open(void) { return rx::S().open ? 1 : 0; }

int rx_menu_toggle(void) {
    using namespace rx;
    if (G().failed) return 0;   // (no menu: the old window instead)
    State& s = S();
    std::lock_guard lk(s.mu);
    s.open = !s.open;
    s.listening = false;
    s.dragging = false;
    hos_bind_ui_active = s.open ? 1 : 0;
    if (s.open) {
        s.opened_ns = hos_now_ns();
        s.status.clear();
        Bindings b;
        hos_bind_get(&b);
        const std::vector<Row> rows = rows_of(s.page, b);
        if (s.row >= (int)rows.size() || !selectable(rows[(size_t)s.row])) first_row(s, rows);
    }
    return 1;
}

void rx_menu_key(int vk) {
    using namespace rx;
    State& s = S();
    std::lock_guard lk(s.mu);
    if (!s.open) return;
    if (s.listening) {   // (the key itself is taken by the polling: rx_menu_tick; Esc cancels)
        if (vk == VK_ESCAPE) {
            s.listening = false;
            set_status(s, "Cancelled.");
        }
        return;
    }
    Bindings b;
    hos_bind_get(&b);
    std::vector<Row> rows = rows_of(s.page, b);
    const Layout l = layout(s.w, s.h, s.page);
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    switch (vk) {
    case VK_ESCAPE:
        s.open = false;
        hos_bind_ui_active = 0;
        return;
    case VK_TAB: set_page(s, s.page + (shift ? -1 : 1)); return;
    case 'Q': set_page(s, s.page - 1); return;
    case 'E': set_page(s, s.page + 1); return;
    case VK_UP: move_row(s, rows, -1); break;
    case VK_DOWN: move_row(s, rows, 1); break;
    case VK_PRIOR: for (int i = 0; i < 8; i++) move_row(s, rows, -1); break;
    case VK_NEXT: for (int i = 0; i < 8; i++) move_row(s, rows, 1); break;
    case VK_LEFT:
    case VK_RIGHT:
        if (s.row >= 0 && s.row < (int)rows.size()) {
            const Row& r = rows[(size_t)s.row];
            if (r.kind == kBinding) s.slot = vk == VK_LEFT ? 0 : BIND_SLOTS - 1;
            else if (r.kind == kOption || r.kind == kSlider) change(s, r, vk == VK_LEFT ? -1 : 1);
            else if (r.kind == kToggle) change(s, r, 1);
        }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (s.row >= 0 && s.row < (int)rows.size()) activate(s, rows[(size_t)s.row], rows[(size_t)s.row].kind == kBinding ? kSlot0 + s.slot : kRight);
        break;
    case VK_DELETE:
    case VK_BACK:
        if (s.row >= 0 && s.row < (int)rows.size() && rows[(size_t)s.row].kind == kBinding) {
            s.listen_act = rows[(size_t)s.row].act;
            s.listen_slot = s.slot;
            bind_to(s, 0);
        }
        break;
    default: return;
    }
    keep_visible(s, l, rows);
}

void rx_menu_mouse(int x, int y, int button, int down) {
    using namespace rx;
    State& s = S();
    std::lock_guard lk(s.mu);
    if (!s.open) return;
    if (s.dragging) {
        if (button == 0) drag_to(s, x);
        if (button == 1 && !down) s.dragging = false;
        return;
    }
    const Hit h = hit_test(s, x, y);
    s.hover_tab = h.tab;
    s.hover_row = h.row;
    s.hover_part = h.close ? 99 : h.part;
    if (button != 1 || !down || s.listening) return;
    if (h.close) {
        s.open = false;
        hos_bind_ui_active = 0;
        return;
    }
    if (h.tab >= 0) {
        set_page(s, h.tab);
        return;
    }
    if (h.row < 0) return;
    Bindings b;
    hos_bind_get(&b);
    const std::vector<Row> rows = rows_of(s.page, b);
    const Row& r = rows[(size_t)h.row];
    s.row = h.row;
    if (r.kind == kBinding) {
        if (h.part >= kSlot0) {
            s.slot = h.part - kSlot0;
            activate(s, r, h.part);
        }
    } else if (r.kind == kSlider) {
        s.dragging = true;
        s.drag_row = h.row;
        drag_to(s, x);
    } else if (r.kind == kOption) {
        if (h.part == kLeft || h.part == kRight) activate(s, r, h.part);
        else activate(s, r, kRight);
    } else {
        activate(s, r, h.part);
    }
}

void rx_menu_wheel(int notches) {
    using namespace rx;
    State& s = S();
    std::lock_guard lk(s.mu);
    if (!s.open) return;
    if (s.listening && s.listen_armed) {
        bind_to(s, notches > 0 ? BIND_WHEEL_UP : BIND_WHEEL_DOWN);
        return;
    }
    const Layout l = layout(s.w, s.h, s.page);
    s.scroll -= notches * 3 * l.row_h * 0.5f;
    if (s.scroll < 0) s.scroll = 0;   // (the far end: clamped when drawn)
}

// (the window's timer, ~60 a second while open) a key or mouse button for the binding being waited for
void rx_menu_tick(void) {
    using namespace rx;
    State& s = S();
    std::lock_guard lk(s.mu);
    if (!s.open || !s.listening) return;
    uint8_t now[256];
    int any_mouse = 0;
    for (int vk = 1; vk < 256; vk++) now[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    for (int vk = VK_LBUTTON; vk <= VK_XBUTTON2; vk++)
        if (vk != VK_CANCEL && now[vk]) any_mouse = 1;
    if (!s.listen_armed) {   // (the click or Enter that started it: wait until it is let go)
        if (!any_mouse && !now[VK_RETURN] && !now[VK_SPACE]) s.listen_armed = true;
        std::memcpy(s.listen_prev, now, sizeof now);
        return;
    }
    for (int vk = 1; vk < 256; vk++) {
        if (!now[vk] || s.listen_prev[vk]) continue;
        // (the left / right Shift, Ctrl, Alt come with the generic ones, which name the key, as input.ini has them)
        if (vk == VK_CANCEL || (vk >= VK_LSHIFT && vk <= VK_RMENU)) continue;
        if (vk == VK_ESCAPE) {
            s.listening = false;
            set_status(s, "Cancelled.");
            break;
        }
        if (vk == VK_DELETE || vk == VK_BACK) {
            bind_to(s, 0);
            break;
        }
        if (reserved_key(vk)) continue;
        if (!hos_bind_key_name(vk)[0]) continue;   // (a key input.ini has no name for)
        bind_to(s, vk);
        break;
    }
    std::memcpy(s.listen_prev, now, sizeof now);
}
}  // extern "C"

#else   // the web build: no menu (the page has its own)

namespace rx {
bool menu_open() { return false; }
void menu_draw(WGPUCommandEncoder, WGPUTextureView, WGPUTextureFormat, uint32_t, uint32_t) {}
}  // namespace rx

#endif
