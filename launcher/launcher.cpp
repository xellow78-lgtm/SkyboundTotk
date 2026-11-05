// SkyboundTOTK Launcher: builds the game from your own game files and starts it with the settings chosen here.
//
// Three pages, in the look of the game's F1 menu (gpu/render/rx_menu.cpp):
//   Play      whether the game is built and what it will start with; Play
//   Settings  how it starts (upscaler, sharpness, fullscreen, window size: environment variables the game reads) and
//             the game's own settings that F1 also changes (3D resolution, anisotropic filtering, NVIDIA Reflex, button
//             icons, the mouse: user\input.ini), the mods folder, the log file
//   Build     your game files (extracted ExeFS and RomFS folders, or the base game and update .nsp files with your
//             prod.keys), then launcher/pipeline.py: game files, source changes, build tools, DLSS library,
//             translating the game's code, compiling, installing, each with its progress and the build's output
// Its own settings are in launcher.ini next to it (key = value; launcher.defaults.ini, when there, fills keys
// launcher.ini lacks; env.NAME = value lines add environment variables for the game). It is drawn with GDI+, one
// window, no browser.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <objidl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;
using std::wstring;

namespace {

// ---- text and files ----
wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
wstring trim(wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && iswspace(s[i])) i++;
    return s.substr(i);
}
bool exists(const wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::string read_file(const wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string s;
    char buf[65536];
    DWORD n;
    while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n) s.append(buf, n);
    CloseHandle(h);
    return s;
}
bool write_file(const wstring& p, const std::string& s) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n;
    WriteFile(h, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(h);
    return n == s.size();
}
wstring parent(const wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    return i == wstring::npos ? wstring() : p.substr(0, i);
}
wstring exe_dir() {
    wchar_t b[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, b, MAX_PATH * 4);
    return parent(b);
}
wstring file_time(const wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return {};
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &st);
    static const wchar_t* const months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                            L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    wchar_t b[64];
    swprintf(b, 64, L"%ls %u, %u at %u:%02u", months[(st.wMonth + 11) % 12], st.wDay, st.wYear, st.wHour, st.wMinute);
    return b;
}

// ---- launcher.ini (key = value) ----
struct Config {
    std::map<wstring, wstring> v;
    wstring path;
    void load(const wstring& p, bool fill_only) {
        std::string s = read_file(p);
        size_t at = 0;
        while (at < s.size()) {
            size_t e = s.find('\n', at);
            if (e == std::string::npos) e = s.size();
            wstring line = trim(widen(s.substr(at, e - at)));
            at = e + 1;
            if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
            size_t eq = line.find(L'=');
            if (eq == wstring::npos) continue;
            wstring k = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
            if (!fill_only || !v.count(k)) v[k] = val;
        }
    }
    void save() {
        std::string s = "# SkyboundTOTK Launcher's settings (the launcher writes this; env.NAME = value adds a variable)\n";
        for (auto& [k, val] : v) s += narrow(k) + " = " + narrow(val) + "\n";
        write_file(path, s);
    }
    wstring get(const wstring& k, const wstring& d = L"") const {
        auto it = v.find(k);
        return it == v.end() ? d : it->second;
    }
    int geti(const wstring& k, int d) const {
        auto it = v.find(k);
        return it == v.end() || it->second.empty() ? d : _wtoi(it->second.c_str());
    }
    double getf(const wstring& k, double d) const {
        auto it = v.find(k);
        return it == v.end() || it->second.empty() ? d : _wtof(it->second.c_str());
    }
};

// ---- the game's user\input.ini: the keys this launcher sets, the rest kept as it is ----
struct GameIni {
    wstring path;
    std::vector<std::string> lines;
    std::map<std::string, std::string> v;
    static std::string key_of(const std::string& line, std::string* val) {
        std::string l = line.substr(0, line.find('#'));
        size_t eq = l.find('=');
        if (eq == std::string::npos) return {};
        auto t = [](std::string x) {
            while (!x.empty() && (x.back() == ' ' || x.back() == '\t' || x.back() == '\r')) x.pop_back();
            size_t i = x.find_first_not_of(" \t");
            return i == std::string::npos ? std::string() : x.substr(i);
        };
        if (val) *val = t(l.substr(eq + 1));
        return t(l.substr(0, eq));
    }
    void load() {
        lines.clear();
        v.clear();
        std::string s = read_file(path);
        size_t at = 0;
        while (at < s.size()) {
            size_t e = s.find('\n', at);
            if (e == std::string::npos) e = s.size();
            std::string line = s.substr(at, e - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            std::string val, k = key_of(line, &val);
            if (!k.empty()) v[k] = val;
            at = e + 1;
        }
    }
    void set(const std::string& k, const std::string& val) {
        v[k] = val;
        for (auto& line : lines) {
            std::string old, kk = key_of(line, &old);
            if (kk == k) {
                size_t hash = line.find('#');
                line = k + " = " + val + (hash != std::string::npos ? "    " + line.substr(hash) : "");
                save();
                return;
            }
        }
        lines.push_back(k + " = " + val);
        save();
    }
    int geti(const std::string& k, int d) const {
        auto it = v.find(k);
        return it == v.end() || it->second.empty() ? d : atoi(it->second.c_str());
    }
    double getf(const std::string& k, double d) const {
        auto it = v.find(k);
        return it == v.end() || it->second.empty() ? d : atof(it->second.c_str());
    }
    void save() {
        CreateDirectoryW(parent(path).c_str(), nullptr);
        std::string s;
        for (auto& l : lines) s += l + "\n";
        write_file(path, s);
    }
};

// ---- look (the F1 menu's) ----
const Color kBg(255, 11, 14, 19), kPanel(255, 20, 25, 32), kPanel2(255, 28, 34, 43), kLine(255, 44, 52, 64),
    kText(255, 236, 240, 244), kDim(255, 146, 157, 170), kAccent(255, 94, 212, 232), kAccentDim(255, 38, 86, 96),
    kGold(255, 232, 196, 94), kGood(255, 110, 208, 140), kBad(255, 236, 104, 98);

enum Page { kPlay, kSettings, kBuild, kPages };
const wchar_t* const kPageNames[kPages] = {L"Play", L"Settings", L"Build"};

// what a click on a drawn thing does
enum Act {
    A_NONE, A_PAGE, A_PLAY, A_OPEN_LOG, A_OPEN_FOLDER, A_PREV, A_NEXT, A_SLIDER, A_BROWSE, A_CLEAR, A_MODE, A_TOGGLE,
    A_BUILD, A_CANCEL, A_OPEN_BUILD_LOG, A_GO_BUILD, A_UPDATE, A_CHECK
};
struct Hot {
    RectF r;
    Act act;
    int arg;
};

// the settings: an option list, a slider, a toggle or a path
enum Kind { K_OPTION, K_SLIDER, K_TOGGLE, K_PATH, K_SECTION };
struct Setting {
    Kind kind;
    const wchar_t* label;
    const wchar_t* note;
    std::vector<const wchar_t*> options;   // (K_OPTION)
    double lo = 0, hi = 1, step = 0.1;     // (K_SLIDER)
    bool file = false;                     // (K_PATH: a file, else a folder)
};

enum SettingId {
    S_SEC_START, S_UPSCALER, S_SHARP, S_DISPLAY, S_WINDOW, S_VSYNC, S_SEC_GAME, S_OUTRES, S_ASPECT, S_RES, S_FPS, S_FRAMETIMING, S_FPSCOUNTER, S_ANISO, S_REFLEX, S_BUTTONS, S_MOUSECAM,
    S_SENS, S_SEC_FILES, S_MODS, S_MODS_ON, S_LOG, S_COUNT
};
std::vector<Setting> settings_list() {
    std::vector<Setting> s(S_COUNT);
    s[S_SEC_START] = {K_SECTION, L"When the game starts", L""};
    s[S_UPSCALER] = {K_OPTION, L"Upscaler", L"DLSS needs an NVIDIA RTX card and the DLSS library (Build page).",
                     {L"Off", L"AMD FSR 2", L"NVIDIA DLSS"}};
    s[S_SHARP] = {K_SLIDER, L"FSR 2 sharpness", L"0 = soft, 1 = sharpest."};
    s[S_SHARP].lo = 0, s[S_SHARP].hi = 1, s[S_SHARP].step = 0.05;
    s[S_DISPLAY] = {K_OPTION, L"Display", L"F11 in the game switches too.", {L"Windowed", L"Fullscreen"}};
    s[S_WINDOW] = {K_OPTION, L"Window size", L"The window's size before fullscreen.",
                   {L"1280 x 720", L"1600 x 900", L"1920 x 1080", L"2560 x 1440"}};
    s[S_VSYNC] = {K_OPTION, L"VSync", L"Off: frames past the screen's rate (Frame rate limit), less input lag.",
                  {L"On", L"Off (no tearing)", L"Off (tearing)"}};
    s[S_SEC_GAME] = {K_SECTION, L"In the game (F1 changes these too)", L""};
    s[S_OUTRES] = {K_OPTION, L"Output resolution", L"The size the game draws its frame and HUD in.",
                   {L"1920 x 1080", L"Display", L"2560 x 1440", L"3840 x 2160"}};
    s[S_ASPECT] = {K_OPTION, L"Aspect ratio", L"Wider or 16:10 screens: more world at the sides, the HUD keeps its shape.",
                   {L"16:9 (game's own)", L"Display's shape", L"16:10", L"21:9", L"32:9"}};
    s[S_RES] = {K_OPTION, L"3D resolution", L"The 3D scene's share of it, before the upscaler.",
                {L"Game's own", L"Quality", L"Balanced", L"Performance", L"Ultra Performance", L"Native (DLAA)"}};
    s[S_FPS] = {K_OPTION, L"Frame rate limit", L"Beta above 60: the camera may turn faster, climbing may fail.",
                {L"30", L"60", L"90", L"120", L"144", L"165", L"240", L"Display's rate"}};
    s[S_FRAMETIMING] = {K_OPTION, L"Frame timing", L"Responsive: no speed-up after a slow frame on G-Sync / FreeSync.",
                        {L"Smooth", L"Responsive"}};
    s[S_FPSCOUNTER] = {K_OPTION, L"FPS counter", L"In the top left corner of the game.",
                       {L"Off", L"FPS", L"FPS + frame time"}};
    s[S_ANISO] = {K_OPTION, L"Anisotropic filtering", L"Sharper ground and walls at an angle.",
                  {L"Game's own", L"2x", L"4x", L"8x", L"16x"}};
    s[S_REFLEX] = {K_OPTION, L"NVIDIA Reflex", L"Less input lag (NVIDIA, with the DLSS library).",
                   {L"Off", L"On", L"On + Boost"}};
    s[S_BUTTONS] = {K_OPTION, L"Button icons", L"Automatic: the device you play with.",
                    {L"Automatic", L"Keyboard", L"Xbox", L"PlayStation", L"Switch"}};
    s[S_MOUSECAM] = {K_TOGGLE, L"Mouse turns the camera", L"Click the game to capture the mouse; Esc frees it."};
    s[S_SENS] = {K_SLIDER, L"Mouse sensitivity", L"1.00 = the default speed."};
    s[S_SENS].lo = 0.05, s[S_SENS].hi = 5, s[S_SENS].step = 0.05;
    s[S_SEC_FILES] = {K_SECTION, L"Files", L""};
    s[S_MODS] = {K_PATH, L"Mods folder", L"TKMM's output, or emulator-style mod folders."};
    s[S_MODS_ON] = {K_TOGGLE, L"Use mods", L""};
    s[S_LOG] = {K_PATH, L"Log file", L"What the game prints (send it with a bug report)."};
    s[S_LOG].file = true;
    return s;
}

// the build's steps (launcher/pipeline.py's)
struct BuildStep {
    std::string key;
    wstring label;
    std::string state = "wait";
    float progress = 0;
};

// ---- state ----
struct App {
    HWND hwnd = nullptr;
    float s = 1;   // (dpi / 96)
    int page = kPlay;
    std::vector<Hot> hots;
    int hover = -1;
    Config cfg;
    GameIni ini;
    wstring dir, source, dist;
    float scroll = 0, scroll_max = 0;
    bool dragging = false;
    int drag_setting = -1;
    RectF drag_track;
    // the game, while it runs
    HANDLE game = nullptr;
    DWORD game_exit = STILL_ACTIVE;
    int64_t game_started = 0;
    // the build, while it runs
    HANDLE build = nullptr, job = nullptr;
    std::vector<BuildStep> steps;
    std::deque<wstring> log;
    std::mutex mu;
    std::vector<std::string> incoming;
    std::string pending;
    wstring build_text, build_end;
    bool build_ok = false, build_ran = false;
    float spin = 0;
    wstring status;   // (a line under the page title: what just happened)
    // updates (pipeline.py --check): the newest commit of the update branch on GitHub
    enum { U_NONE, U_CHECKING, U_AVAILABLE, U_CURRENT, U_FAILED } update = U_NONE;
    std::string update_sha, update_subject, update_why;
    int64_t update_time = 0;
    // a first build from the release package: Python downloaded first, Microsoft's license asked when the build needs it
    bool python_busy = false, pending_fetch = false;
    std::string need_msvc_license;   // (the license's address, from the build's @@need line)
} app;

// where updates come from: launcher.ini update_repo / update_branch (update_check = 0: no check at the start)
wstring update_repo() { return app.cfg.get(L"update_repo", L"xellow78-lgtm/SkyboundTotk"); }
wstring update_branch() { return app.cfg.get(L"update_branch", L"update-1.4.3"); }

const UINT WM_BUILD_LINES = WM_APP + 1, WM_GAME_EXIT = WM_APP + 2, WM_BUILD_EXIT = WM_APP + 3, WM_UPDATE_CHECKED = WM_APP + 4,
           WM_PYTHON_READY = WM_APP + 5;

wstring cfg_default_mods() {
    // (TKMM's output next to the skybound folder, as rebuild_and_test.bat looks for it)
    wstring root = parent(app.source);
    if (root.size() >= 2) {
        wstring m = root.substr(0, 2) + L"\\tkmm'";
        if (is_dir(m)) return m;
    }
    return L"";
}

void load_all() {
    app.dir = exe_dir();
    app.cfg = Config{};
    app.cfg.path = app.dir + L"\\launcher.ini";
    app.cfg.load(app.cfg.path, false);
    app.cfg.load(app.dir + L"\\launcher.defaults.ini", true);
    app.source = app.cfg.get(L"source");
    if (app.source.empty()) {   // (the launcher in <source>\dist\SkyboundTOTK)
        wstring up = parent(parent(app.dir));
        if (exists(up + L"\\a64-kit\\build.py")) app.source = up;
    }
    // (the release package: the launcher alone in a folder, with pipeline.py; the first build downloads the source
    // into "source" beside it, and the tools (Python, clang, Microsoft's C++ tools, Rust) beside that)
    if (app.source.empty() && exists(app.dir + L"\\pipeline.py")) app.source = app.dir + L"\\source";
    app.dist = app.source.empty() ? app.dir : app.source + L"\\dist\\SkyboundTOTK";
    if (!exists(app.dist + L"\\SkyboundTOTK.exe") && exists(app.dir + L"\\SkyboundTOTK.exe")) app.dist = app.dir;
    if (!app.cfg.v.count(L"mods")) app.cfg.v[L"mods"] = cfg_default_mods();
    if (!app.cfg.v.count(L"log")) app.cfg.v[L"log"] = app.dist + L"\\skybound.log";
    app.ini.path = app.dist + L"\\user\\input.ini";
    app.ini.load();
    if (app.steps.empty()) {
        static const std::pair<const char*, const wchar_t*> st[] = {
            {"update", L"Updates"}, {"files", L"Game files"}, {"overlay", L"Source changes"}, {"tools", L"Build tools"},
            {"dlss", L"DLSS library"}, {"lift", L"Translate the game's code"}, {"compile", L"Compile"},
            {"install", L"Install"}};
        for (auto& [k, l] : st) app.steps.push_back({k, l});
    }
}

const int kFpsValues[8] = {30, 60, 90, 120, 144, 165, 240, 0};   // (input.ini frame_rate_limit; 0 the display's)
int fps_index(int v) {
    for (int i = 0; i < 8; i++)
        if (kFpsValues[i] == v) return i;
    return 1;
}

// a setting's current value: an index (options, toggles) or a number (sliders)
double setting_value(int id) {
    auto& c = app.cfg;
    auto& g = app.ini;
    switch (id) {
    case S_UPSCALER: return c.geti(L"upscaler", 1);
    case S_SHARP: return c.getf(L"sharpness", 0.5);
    case S_DISPLAY: return c.geti(L"fullscreen", 0);
    case S_WINDOW: return c.geti(L"window", 0);
    case S_VSYNC: return c.geti(L"vsync", 0);
    case S_OUTRES: return g.geti("output_resolution", 0);
    case S_FPS: return fps_index(g.geti("frame_rate_limit", 60));
    case S_ASPECT: return g.geti("aspect_ratio", 0);
    case S_FPSCOUNTER: return g.geti("fps_counter", 0);
    case S_FRAMETIMING: return g.geti("frame_timing", 0);
    case S_RES: return g.geti("render_resolution", 0);
    case S_ANISO: return g.geti("anisotropic_filtering", 0);
    case S_REFLEX: return g.geti("nvidia_reflex", 0);
    case S_BUTTONS: return g.geti("button_icons", 0);
    case S_MOUSECAM: return g.geti("mouse_camera", 1);
    case S_SENS: return g.getf("mouse_sensitivity", 1.0);
    case S_MODS_ON: return c.geti(L"mods_on", 1);
    }
    return 0;
}
void set_setting(int id, double v) {
    auto& c = app.cfg;
    auto& g = app.ini;
    char b[32];
    switch (id) {
    case S_UPSCALER: c.v[L"upscaler"] = std::to_wstring((int)v); break;
    case S_SHARP: {
        wchar_t w[16];
        swprintf(w, 16, L"%.2f", v);
        c.v[L"sharpness"] = w;
        break;
    }
    case S_DISPLAY: c.v[L"fullscreen"] = std::to_wstring((int)v); break;
    case S_WINDOW: c.v[L"window"] = std::to_wstring((int)v); break;
    case S_VSYNC: c.v[L"vsync"] = std::to_wstring((int)v); break;
    case S_MODS_ON: c.v[L"mods_on"] = std::to_wstring((int)v); break;
    case S_OUTRES: g.set("output_resolution", std::to_string((int)v)); return;
    case S_FPS: g.set("frame_rate_limit", std::to_string(kFpsValues[std::clamp((int)v, 0, 7)])); return;
    case S_RES: g.set("render_resolution", std::to_string((int)v)); return;
    case S_ASPECT: g.set("aspect_ratio", std::to_string((int)v)); return;
    case S_FPSCOUNTER: g.set("fps_counter", std::to_string((int)v)); return;
    case S_FRAMETIMING: g.set("frame_timing", std::to_string((int)v)); return;
    case S_ANISO: g.set("anisotropic_filtering", std::to_string((int)v)); return;
    case S_REFLEX: g.set("nvidia_reflex", std::to_string((int)v)); return;
    case S_BUTTONS: g.set("button_icons", std::to_string((int)v)); return;
    case S_MOUSECAM: g.set("mouse_camera", std::to_string((int)v)); return;
    case S_SENS: snprintf(b, sizeof b, "%.3f", v); g.set("mouse_sensitivity", b); return;
    default: return;
    }
    c.save();
}

// ---- drawing helpers ----
struct Draw {
    Graphics& g;
    float s;
    FontFamily family{L"Segoe UI"};
    const FontFamily* fam = &family;   // (Segoe UI, else the system's sans-serif)
    std::map<std::pair<int, int>, Font*> fonts;
    explicit Draw(Graphics& gr, float sc) : g(gr), s(sc) {
        if (family.GetLastStatus() != Ok || !family.IsAvailable()) fam = FontFamily::GenericSansSerif();
    }
    ~Draw() {
        for (auto& [k, f] : fonts) delete f;
    }
    Font* font(float px, int style = FontStyleRegular) {
        auto key = std::make_pair((int)(px * 10), style);
        auto it = fonts.find(key);
        if (it != fonts.end()) return it->second;
        Font* f = new Font(fam, px * s, style, UnitPixel);
        fonts[key] = f;
        return f;
    }
    static void rounded(GraphicsPath& p, RectF r, float rad) {
        rad = std::min(rad, std::min(r.Width, r.Height) / 2);
        if (rad <= 0.5f) { p.AddRectangle(r); return; }
        const float d = rad * 2;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
        p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
        p.CloseFigure();
    }
    void fill(RectF r, Color c, float rad = 0) {
        SolidBrush b(c);
        if (rad <= 0) { g.FillRectangle(&b, r); return; }
        GraphicsPath p;
        rounded(p, r, rad * s);
        g.FillPath(&b, &p);
    }
    void outline(RectF r, Color c, float width, float rad = 0) {
        Pen pen(c, width * s);
        GraphicsPath p;
        rounded(p, r, rad * s);
        g.DrawPath(&pen, &p);
    }
    // text in r: align 0 left, 1 centre, 2 right; vertically centred
    void text(const wstring& t, RectF r, float px, Color c, int align = 0, int style = FontStyleRegular) {
        StringFormat f;
        f.SetAlignment(align == 0 ? StringAlignmentNear : align == 1 ? StringAlignmentCenter : StringAlignmentFar);
        f.SetLineAlignment(StringAlignmentCenter);
        f.SetTrimming(StringTrimmingEllipsisPath);
        f.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush b(c);
        g.DrawString(t.c_str(), (INT)t.size(), font(px, style), r, &f, &b);
    }
    void text_wrap(const wstring& t, RectF r, float px, Color c) {
        StringFormat f;
        f.SetTrimming(StringTrimmingEllipsisWord);
        SolidBrush b(c);
        g.DrawString(t.c_str(), (INT)t.size(), font(px), r, &f, &b);
    }
    // shapes for the symbols (GDI+ falls back to no other font: Segoe UI lacks them)
    void arrow(RectF r, bool right, Color c) {   // a small triangle pointing left or right
        const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, h = 5.5f * s, w = 4.5f * s;
        PointF p[3];
        if (right) p[0] = PointF(cx - w, cy - h), p[1] = PointF(cx + w, cy), p[2] = PointF(cx - w, cy + h);
        else p[0] = PointF(cx + w, cy - h), p[1] = PointF(cx - w, cy), p[2] = PointF(cx + w, cy + h);
        SolidBrush b(c);
        g.FillPolygon(&b, p, 3);
    }
    void check(RectF r, Color c, float width = 2.4f) {
        Pen pen(c, width * s);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        PointF p[3] = {PointF(r.X + r.Width * 0.2f, r.Y + r.Height * 0.52f), PointF(r.X + r.Width * 0.42f, r.Y + r.Height * 0.74f),
                       PointF(r.X + r.Width * 0.8f, r.Y + r.Height * 0.28f)};
        g.DrawLines(&pen, p, 3);
    }
    void cross(RectF r, Color c) {
        Pen pen(c, 2.4f * s);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        const float k = r.Width * 0.27f;
        g.DrawLine(&pen, r.X + k, r.Y + k, r.X + r.Width - k, r.Y + r.Height - k);
        g.DrawLine(&pen, r.X + r.Width - k, r.Y + k, r.X + k, r.Y + r.Height - k);
    }
    void dash(RectF r, Color c) {
        Pen pen(c, 2 * s);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, r.X + r.Width * 0.3f, r.Y + r.Height / 2, r.X + r.Width * 0.7f, r.Y + r.Height / 2);
    }
    float width(const wstring& t, float px, int style = FontStyleRegular) {
        RectF out;
        g.MeasureString(t.c_str(), (INT)t.size(), font(px, style), PointF(0, 0), &out);
        return out.Width;
    }
};

int hot(RectF r, Act act, int arg = 0) {
    app.hots.push_back({r, act, arg});
    return (int)app.hots.size() - 1;
}
bool hovered(RectF r) {
    return app.hover >= 0 && app.hover < (int)app.hots.size() && app.hots[app.hover].r.Equals(r);
}

void button(Draw& d, RectF r, const wstring& label, Act act, int arg = 0, bool primary = false, bool enabled = true) {
    const bool h = enabled && (hot(r, enabled ? act : A_NONE, arg), hovered(r));
    if (primary) {
        d.fill(r, !enabled ? kAccentDim : h ? Color(255, 140, 228, 242) : kAccent, 8);
        d.text(label, r, 17, enabled ? Color(255, 8, 20, 26) : kDim, 1, FontStyleBold);
    } else {
        d.fill(r, h ? kPanel2 : kPanel, 7);
        d.outline(r, h ? kGold : kLine, h ? 1.5f : 1, 7);
        d.text(label, r, 14, enabled ? kText : kDim, 1);
    }
}

bool built() { return exists(app.dist + L"\\SkyboundTOTK.exe") && exists(app.dist + L"\\image.bin"); }
bool dlss_library() { return exists(app.dist + L"\\nvngx_dlss.dll"); }

wstring slider_text(int, double v) {
    wchar_t b[32];
    swprintf(b, 32, L"%.2f", v);
    return b;
}

// ---- pages ----
void page_title(Draw& d, RectF c, const wstring& title, const wstring& sub) {
    d.text(title, RectF(c.X, c.Y, c.Width, 40 * d.s), 30, kText, 0, FontStyleBold);
    d.text(sub, RectF(c.X, c.Y + 42 * d.s, c.Width, 22 * d.s), 14, kDim);
}

void card(Draw& d, RectF r, const wstring& label, const wstring& value, const wstring& note, Color vc) {
    d.fill(r, kPanel, 10);
    d.text(label, RectF(r.X + 16 * d.s, r.Y + 12 * d.s, r.Width - 32 * d.s, 20 * d.s), 13, kDim);
    d.text(value, RectF(r.X + 16 * d.s, r.Y + 34 * d.s, r.Width - 32 * d.s, 30 * d.s), 20, vc, 0, FontStyleBold);
    d.text(note, RectF(r.X + 16 * d.s, r.Y + 66 * d.s, r.Width - 32 * d.s, 20 * d.s), 12.5f, kDim);
}

void draw_play(Draw& d, RectF c) {
    const float s = d.s;
    const bool ok = built();
    page_title(d, c, L"Tears of the Kingdom",
               ok ? L"Built " + file_time(app.dist + L"\\SkyboundTOTK.exe") + L"   \u00B7   TOTK 1.4.3"
                  : L"Not built yet: choose your game files on the Build page.");
    // the cards: what it starts with
    auto list = settings_list();
    const int up = (int)setting_value(S_UPSCALER);
    const bool no_dlss = up == 2 && !dlss_library();
    const float cw = (c.Width - 2 * 14 * s) / 3, cy = c.Y + 92 * s, ch = 96 * s;
    card(d, RectF(c.X, cy, cw, ch), L"Upscaler", list[S_UPSCALER].options[std::clamp(up, 0, 2)],
         no_dlss ? L"DLSS library missing: FSR 2 runs" : up ? L"Sharpness " + slider_text(S_SHARP, setting_value(S_SHARP)) : L"Native resolution",
         no_dlss ? kGold : kAccent);
    card(d, RectF(c.X + cw + 14 * s, cy, cw, ch), L"Resolution",
         list[S_OUTRES].options[std::clamp((int)setting_value(S_OUTRES), 0, 3)],
         wstring(L"3D: ") + list[S_RES].options[std::clamp((int)setting_value(S_RES), 0, 5)] + L"  \u00B7  " +
             ((int)setting_value(S_DISPLAY) ? L"Fullscreen" : L"Windowed"),
         kText);
    const wstring mods = app.cfg.get(L"mods");
    const bool mods_on = (int)setting_value(S_MODS_ON) && !mods.empty() && is_dir(mods);
    card(d, RectF(c.X + 2 * (cw + 14 * s), cy, cw, ch), L"Mods", mods_on ? L"On" : L"Off",
         mods_on ? mods : mods.empty() ? L"No mods folder set" : L"Folder not found", mods_on ? kGood : kDim);

    // the play panel
    RectF hero(c.X, cy + ch + 18 * s, c.Width, 200 * s);
    d.fill(hero, kPanel, 12);
    {
        LinearGradientBrush glow(PointF(hero.X, hero.Y), PointF(hero.X + hero.Width, hero.Y + hero.Height),
                                 Color(40, 94, 212, 232), Color(0, 94, 212, 232));
        GraphicsPath p;
        Draw::rounded(p, hero, 12 * s);
        d.g.FillPath(&glow, &p);
    }
    const bool running = app.game != nullptr;
    wstring line1, line2;
    if (running) {
        const int64_t secs = (int64_t)(GetTickCount64() - app.game_started) / 1000;
        line1 = L"Playing";
        line2 = L"For " + std::to_wstring(secs / 60) + L" min " + std::to_wstring(secs % 60) + L" s. Close the game's window to stop.";
    } else if (app.game_exit != STILL_ACTIVE) {
        line1 = app.game_exit == 0 ? L"Game closed" : L"The game stopped";
        wchar_t b[96];
        swprintf(b, 96, app.game_exit == 0 ? L"Exit code 0." : L"Exit code 0x%08X: its log says what happened.", app.game_exit);
        line2 = b;
    } else if (ok) {
        line1 = L"Ready";
        line2 = L"Your saves and mods stay where they are. The game's own settings are on F1 too.";
    } else {
        line1 = L"Build the game first";
        line2 = L"It is made from your own copy of the game, on this PC. Nothing of the game is downloaded.";
    }
    d.text(line1, RectF(hero.X + 28 * s, hero.Y + 30 * s, hero.Width - 320 * s, 40 * s), 26, kText, 0, FontStyleBold);
    d.text_wrap(line2, RectF(hero.X + 28 * s, hero.Y + 78 * s, hero.Width - 340 * s, 60 * s), 14, kDim);
    RectF play(hero.X + hero.Width - 268 * s, hero.Y + 52 * s, 240 * s, 64 * s);
    if (ok) button(d, play, running ? L"PLAYING\u2026" : L"PLAY", A_PLAY, 0, true, !running && !app.build);
    else button(d, play, L"GO TO BUILD", A_GO_BUILD, 0, true, true);
    button(d, RectF(hero.X + 28 * s, hero.Y + hero.Height - 52 * s, 150 * s, 34 * s), L"Open the log", A_OPEN_LOG, 0,
           false, exists(app.cfg.get(L"log")));
    button(d, RectF(hero.X + 190 * s, hero.Y + hero.Height - 52 * s, 170 * s, 34 * s), L"Open the game folder",
           A_OPEN_FOLDER, 0, false, is_dir(app.dist));

    // updates: the newest commit of the update branch
    RectF ur(c.X, hero.Y + hero.Height + 14 * s, c.Width, 64 * s);
    d.fill(ur, kPanel, 10);
    wstring u1, u2;
    Color uc = kDim;
    if (app.update == App::U_AVAILABLE) {
        wchar_t when[64] = L"";
        if (app.update_time > 0) {
            const time_t t = (time_t)app.update_time;
            struct tm tm;
            localtime_s(&tm, &t);
            wcsftime(when, 64, L"%Y-%m-%d %H:%M", &tm);
        }
        u1 = L"Update available", uc = kGold;
        u2 = widen(app.update_subject) + L"   \u00B7   " + when;
    } else if (app.update == App::U_CHECKING) {
        u1 = L"Checking for updates\u2026";
        u2 = update_repo() + L" (" + update_branch() + L")";
    } else if (app.update == App::U_CURRENT) {
        u1 = L"Up to date", uc = kGood;
        u2 = update_repo() + L" (" + update_branch() + L") at " + widen(app.update_sha.substr(0, 10));
    } else if (app.update == App::U_FAILED) {
        u1 = L"Could not check for updates";
        u2 = widen(app.update_why.substr(0, 160));
    } else {
        u1 = L"Updates";
        u2 = L"Not checked yet.";
    }
    d.text(u1, RectF(ur.X + 20 * s, ur.Y + 9 * s, ur.Width - 300 * s, 24 * s), 15, uc, 0, FontStyleBold);
    d.text(u2, RectF(ur.X + 20 * s, ur.Y + 34 * s, ur.Width - 300 * s, 20 * s), 12.5f, kDim);
    RectF ub(ur.X + ur.Width - 248 * s, ur.Y + 13 * s, 228 * s, 38 * s);
    if (app.update == App::U_AVAILABLE)
        button(d, ub, L"UPDATE AND BUILD", A_UPDATE, 0, true, !app.build && !running);
    else
        button(d, ub, L"Check now", A_CHECK, 0, false, app.update != App::U_CHECKING);
}


void draw_settings(Draw& d, RectF c) {
    const float s = d.s;
    page_title(d, c, L"Settings", L"Saved as you change them. The game reads them when it starts.");
    RectF view(c.X, c.Y + 80 * s, c.Width, c.Height - 80 * s);
    auto list = settings_list();
    const float row_h = 64 * s, gap = 8 * s;
    float total = 0;
    for (auto& st : list) total += (st.kind == K_SECTION ? 40 * s : row_h) + gap;
    app.scroll_max = std::max(0.0f, total - view.Height);
    app.scroll = std::clamp(app.scroll, 0.0f, app.scroll_max);
    Region clip(view);
    d.g.SetClip(&clip);
    float y = view.Y - app.scroll;
    for (int id = 0; id < (int)list.size(); id++) {
        const Setting& st = list[id];
        if (st.kind == K_SECTION) {
            d.text(st.label, RectF(view.X + 4 * s, y + 12 * s, view.Width, 26 * s), 13, kAccent, 0, FontStyleBold);
            y += 40 * s + gap;
            continue;
        }
        RectF r(view.X, y, view.Width - 10 * s, row_h);
        y += row_h + gap;
        if (r.Y + r.Height < view.Y || r.Y > view.Y + view.Height) continue;
        d.fill(r, kPanel, 9);
        const float vx = r.X + r.Width * 0.56f, vw = r.Width * 0.44f - 16 * s;
        d.text(st.label, RectF(r.X + 18 * s, r.Y + 10 * s, vx - r.X - 24 * s, 24 * s), 16, kText);
        if (*st.note) d.text(st.note, RectF(r.X + 18 * s, r.Y + 34 * s, vx - r.X - 24 * s, 20 * s), 12.5f, kDim);
        RectF val(vx, r.Y + 12 * s, vw, row_h - 24 * s);
        const double v = setting_value(id);
        if (st.kind == K_OPTION) {
            const int n = (int)st.options.size(), i = std::clamp((int)v, 0, n - 1);
            RectF left(val.X, val.Y, 40 * s, val.Height), right(val.X + val.Width - 40 * s, val.Y, 40 * s, val.Height);
            hot(left, A_PREV, id);
            const bool hl = hovered(left);
            hot(right, A_NEXT, id);
            const bool hr = hovered(right);
            d.fill(val, kPanel2, 7);
            d.arrow(left, false, hl ? kGold : kDim);
            d.arrow(right, true, hr ? kGold : kDim);
            d.text(st.options[i], RectF(left.X + left.Width, val.Y, val.Width - 80 * s, val.Height), 15,
                   hl || hr ? kGold : kText, 1);
        } else if (st.kind == K_TOGGLE) {
            const bool on = v != 0;
            RectF sw(val.X + val.Width - 64 * s, val.Y + (val.Height - 30 * s) / 2, 64 * s, 30 * s);
            hot(RectF(val.X, val.Y, val.Width, val.Height), A_TOGGLE, id);
            d.text(on ? L"On" : L"Off", RectF(val.X, val.Y, val.Width - 76 * s, val.Height), 15, on ? kText : kDim, 2);
            d.fill(sw, on ? kAccent : kPanel2, 15);
            const float k = on ? sw.X + sw.Width - 27 * s : sw.X + 3 * s;
            d.fill(RectF(k, sw.Y + 3 * s, 24 * s, 24 * s), on ? Color(255, 8, 20, 26) : kDim, 12);
        } else if (st.kind == K_SLIDER) {
            const float f = (float)((v - st.lo) / (st.hi - st.lo));
            RectF track(val.X + 4 * s, val.Y + val.Height / 2 - 3 * s, val.Width - 80 * s, 6 * s);
            RectF grab(track.X - 6 * s, val.Y, track.Width + 12 * s, val.Height);
            hot(grab, A_SLIDER, id);
            const bool h = hovered(grab) || (app.dragging && app.drag_setting == id);
            if (app.dragging && app.drag_setting == id) app.drag_track = track;
            d.fill(track, kPanel2, 3);
            d.fill(RectF(track.X, track.Y, track.Width * std::clamp(f, 0.0f, 1.0f), track.Height), kAccent, 3);
            const float kx = track.X + track.Width * std::clamp(f, 0.0f, 1.0f);
            d.fill(RectF(kx - 9 * s, track.Y - 6 * s, 18 * s, 18 * s), h ? kGold : kText, 9);
            d.text(slider_text(id, v), RectF(track.X + track.Width + 8 * s, val.Y, 68 * s, val.Height), 15, kText, 2);
        } else if (st.kind == K_PATH) {
            const wstring p = app.cfg.get(id == S_MODS ? L"mods" : L"log");
            RectF box(val.X, val.Y, val.Width - 172 * s, val.Height);
            d.fill(box, kPanel2, 7);
            d.text(p.empty() ? L"(none)" : p, RectF(box.X + 10 * s, box.Y, box.Width - 20 * s, box.Height), 13,
                   p.empty() ? kDim : kText);
            button(d, RectF(box.X + box.Width + 8 * s, val.Y, 84 * s, val.Height), L"Browse", A_BROWSE, id);
            button(d, RectF(box.X + box.Width + 100 * s, val.Y, 72 * s, val.Height), L"Clear", A_CLEAR, id);
        }
    }
    d.g.ResetClip();
    if (app.scroll_max > 0) {   // (the scroll bar)
        const float th = view.Height * view.Height / (view.Height + app.scroll_max);
        const float ty = view.Y + (view.Height - th) * (app.scroll / app.scroll_max);
        d.fill(RectF(view.X + view.Width - 4 * s, ty, 4 * s, th), kLine, 2);
    }
}

// the Build page's game-file inputs, by mode (0: folders, 1: .nsp files)
struct PathRow { const wchar_t* key; const wchar_t* label; bool file; const wchar_t* filter; };
const PathRow kFolderRows[] = {{L"exefs", L"ExeFS folder (1.4.3)", false, nullptr},
                               {L"romfs", L"RomFS folder (with the update applied)", false, nullptr}};
const PathRow kNspRows[] = {{L"nsp", L"Base game (.nsp)", true, L"*.nsp"},
                            {L"update", L"Update 1.4.3 (.nsp)", true, L"*.nsp"},
                            {L"keys", L"Your prod.keys", true, L"*.keys"}};
const PathRow kExtraRows[] = {{L"overlay", L"Source changes folder (optional)", false, nullptr},
                              {L"source", L"SkyboundTOTK source folder", false, nullptr}};
// the Browse button's arg: 100 + a row of kFolderRows, 200 + kNspRows, 300 + kExtraRows
const PathRow* path_row(int arg) {
    if (arg >= 300 && arg < 300 + 2) return &kExtraRows[arg - 300];
    if (arg >= 200 && arg < 200 + 3) return &kNspRows[arg - 200];
    if (arg >= 100 && arg < 100 + 2) return &kFolderRows[arg - 100];
    return nullptr;
}

void draw_build(Draw& d, RectF c) {
    const float s = d.s;
    const bool running = app.build != nullptr;
    page_title(d, c, L"Build", running ? L"Building\u2026 you can leave this open and come back."
                                       : L"From your own game files to the game, on this PC. Steps already done are skipped.");
    const float colw = c.Width * 0.5f - 10 * s;
    // left: the inputs
    RectF L(c.X, c.Y + 80 * s, colw, c.Height - 80 * s);
    const int mode = app.cfg.geti(L"mode", 0);
    RectF seg(L.X, L.Y, L.Width, 38 * s);
    d.fill(seg, kPanel, 9);
    for (int m = 0; m < 2; m++) {
        RectF half(seg.X + 4 * s + m * (seg.Width - 8 * s) / 2, seg.Y + 4 * s, (seg.Width - 8 * s) / 2, seg.Height - 8 * s);
        hot(half, A_MODE, m);
        if (mode == m) d.fill(half, kAccentDim, 7);
        d.text(m ? L".nsp files + keys" : L"Extracted folders", half, 14, mode == m ? kText : hovered(half) ? kGold : kDim, 1);
    }
    float y = seg.Y + seg.Height + 14 * s;
    auto path_input = [&](const PathRow& row, int arg) {
        d.text(row.label, RectF(L.X + 2 * s, y, L.Width, 20 * s), 13, kDim);
        y += 22 * s;
        RectF box(L.X, y, L.Width - 92 * s, 34 * s);
        const wstring v = row.key == wstring(L"source") ? app.source : app.cfg.get(row.key);
        d.fill(box, kPanel, 7);
        d.text(v.empty() ? L"(not chosen)" : v, RectF(box.X + 10 * s, box.Y, box.Width - 20 * s, box.Height), 13,
               v.empty() ? kDim : kText);
        button(d, RectF(box.X + box.Width + 8 * s, y, 84 * s, 34 * s), L"Browse", A_BROWSE, arg, false, !running);
        y += 34 * s + 12 * s;
    };
    if (mode == 0)
        for (int i = 0; i < 2; i++) path_input(kFolderRows[i], 100 + i);
    else
        for (int i = 0; i < 3; i++) path_input(kNspRows[i], 200 + i);
    if (mode == 0 && app.cfg.get(L"exefs").empty() && app.cfg.get(L"romfs").empty() &&
        is_dir(app.source + L"\\game\\romfs")) {
        d.text(L"(empty: the game files already in the source's game folder)", RectF(L.X, y - 6 * s, L.Width, 18 * s),
               12, kDim);
        y += 16 * s;
    }
    y += 4 * s;
    for (int i = 0; i < 2; i++) path_input(kExtraRows[i], 300 + i);
    // DLSS
    RectF dl(L.X, y, L.Width, 44 * s);
    hot(dl, running ? A_NONE : A_TOGGLE, 1000);
    const bool dlss = app.cfg.geti(L"dlss", 0) != 0;
    d.fill(dl, hovered(dl) ? kPanel2 : kPanel, 9);
    RectF box(dl.X + 12 * s, dl.Y + 11 * s, 22 * s, 22 * s);
    d.fill(box, dlss ? kAccent : kPanel2, 5);
    if (dlss) d.check(box, Color(255, 8, 20, 26));
    d.text(L"Make the DLSS library (DLSS + Reflex, once: ~20\u201330 min)", RectF(box.X + 32 * s, dl.Y, dl.Width - 50 * s, dl.Height),
           13, kText);
    y += 44 * s + 16 * s;
    RectF go(L.X, std::max(y, L.Y + L.Height - 58 * s), L.Width, 54 * s);
    if (running) button(d, go, L"CANCEL", A_CANCEL, 0, false, true);
    else button(d, go, built() ? L"BUILD AGAIN" : L"BUILD", A_BUILD, 0, true, app.game == nullptr);

    // right: the steps, the output
    RectF R(c.X + colw + 20 * s, c.Y + 80 * s, colw, c.Height - 80 * s);
    float sy = R.Y;
    for (size_t i = 0; i < app.steps.size(); i++) {
        const BuildStep& st = app.steps[i];
        RectF r(R.X, sy, R.Width, 34 * s);
        sy += 38 * s;
        const bool run = st.state == "run";
        d.fill(r, run ? kPanel2 : kPanel, 8);
        RectF icon(r.X + 8 * s, r.Y + 7 * s, 20 * s, 20 * s);
        if (st.state == "done") d.check(icon, kGood);
        else if (st.state == "skip") d.dash(icon, kDim);
        else if (st.state == "fail") d.cross(icon, kBad);
        else if (run) {
            Pen pen(kAccent, 2.5f * s);
            d.g.DrawArc(&pen, icon.X + 2 * s, icon.Y + 2 * s, icon.Width - 4 * s, icon.Height - 4 * s, app.spin, 270);
        } else d.fill(RectF(icon.X + 6 * s, icon.Y + 6 * s, 8 * s, 8 * s), kLine, 4);
        d.text(st.label, RectF(r.X + 36 * s, r.Y, r.Width * 0.6f, r.Height), 14,
               st.state == "wait" || st.state == "skip" ? kDim : kText);
        const wchar_t* state = st.state == "done" ? L"done" : st.state == "skip" ? L"not needed" : st.state == "fail" ? L"failed" : L"";
        if (run) {
            RectF bar(r.X + r.Width * 0.62f, r.Y + 14 * s, r.Width * 0.38f - 14 * s, 6 * s);
            d.fill(bar, kBg, 3);
            d.fill(RectF(bar.X, bar.Y, bar.Width * std::clamp(st.progress, 0.0f, 1.0f), bar.Height), kAccent, 3);
        } else {
            d.text(state, RectF(r.X, r.Y, r.Width - 12 * s, r.Height), 12.5f, st.state == "fail" ? kBad : kDim, 2);
        }
    }
    // what it is doing, how it ended
    RectF line(R.X, sy + 2 * s, R.Width, 22 * s);
    if (!app.build_end.empty()) d.text(app.build_end, line, 14, app.build_ok ? kGood : kBad, 0, FontStyleBold);
    else d.text(app.build_text, line, 13, kDim);
    sy += 30 * s;
    RectF out(R.X, sy, R.Width, R.Y + R.Height - sy - 44 * s);
    d.fill(out, Color(255, 7, 9, 12), 8);
    {
        FontFamily consolas(L"Consolas");
        const FontFamily* mono = consolas.IsAvailable() ? &consolas : FontFamily::GenericMonospace();
        Font f(mono, 11.5f * s, FontStyleRegular, UnitPixel);
        SolidBrush b(kDim);
        StringFormat sf;
        sf.SetFormatFlags(StringFormatFlagsNoWrap);
        sf.SetTrimming(StringTrimmingEllipsisCharacter);
        const float lh = 15 * s;
        const int rows = std::max(1, (int)((out.Height - 16 * s) / lh));
        std::lock_guard<std::mutex> lock(app.mu);
        const int first = std::max(0, (int)app.log.size() - rows);
        for (int i = first; i < (int)app.log.size(); i++) {
            const wstring& t = app.log[i];
            RectF lr(out.X + 10 * s, out.Y + 8 * s + (i - first) * lh, out.Width - 20 * s, lh);
            d.g.DrawString(t.c_str(), (INT)t.size(), &f, lr, &sf, &b);
        }
        if (app.log.empty()) {
            SolidBrush dim(kLine);
            const wstring t = L"The build's output shows here.";
            d.g.DrawString(t.c_str(), (INT)t.size(), &f, RectF(out.X + 10 * s, out.Y + 8 * s, out.Width, lh), &sf, &dim);
        }
    }
    button(d, RectF(R.X, R.Y + R.Height - 36 * s, 170 * s, 34 * s), L"Open the build log", A_OPEN_BUILD_LOG, 0, false,
           exists(app.dist + L"\\launcher_build.log"));
}

void paint(HDC hdc, int w, int h) {
    Bitmap bmp(w, h, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    Draw d(g, app.s);
    const float s = app.s;
    app.hots.clear();
    d.fill(RectF(0, 0, (float)w, (float)h), kBg);
    // the side bar
    const float side = 250 * s;
    d.fill(RectF(0, 0, side, (float)h), kPanel);
    {
        LinearGradientBrush glow(PointF(0, 0), PointF(0, (float)h), Color(36, 94, 212, 232), Color(0, 94, 212, 232));
        g.FillRectangle(&glow, RectF(0, 0, side, (float)h));
    }
    d.text(L"SKYBOUND", RectF(28 * s, 34 * s, side, 36 * s), 26, kText, 0, FontStyleBold);
    d.text(L"Tears of the Kingdom on PC", RectF(28 * s, 70 * s, side, 20 * s), 13, kAccent);
    for (int p = 0; p < kPages; p++) {
        RectF r(16 * s, 130 * s + p * 54 * s, side - 32 * s, 46 * s);
        hot(r, A_PAGE, p);
        const bool sel = app.page == p, h = hovered(r);
        if (sel) d.fill(r, kAccentDim, 9);
        else if (h) d.fill(r, kPanel2, 9);
        if (sel) d.fill(RectF(r.X, r.Y + 10 * s, 4 * s, r.Height - 20 * s), kAccent, 2);
        d.text(kPageNames[p], RectF(r.X + 22 * s, r.Y, r.Width, r.Height), 17, sel ? kText : h ? kGold : kDim, 0,
               sel ? FontStyleBold : FontStyleRegular);
        if (p == kBuild && app.build) {
            Pen pen(kAccent, 2.2f * s);
            g.DrawArc(&pen, r.X + r.Width - 34 * s, r.Y + 14 * s, 18 * s, 18 * s, app.spin, 270);
        }
    }
    wstring foot = app.game ? L"Playing" : app.build ? L"Building\u2026" : built() ? L"Ready to play" : L"Not built yet";
    d.text(foot, RectF(28 * s, h - 62 * s, side - 40 * s, 20 * s), 13, app.game || app.build ? kAccent : kDim);
    d.text(L"Not affiliated with Nintendo", RectF(28 * s, h - 38 * s, side - 40 * s, 18 * s), 11.5f, kLine);
    // the page
    RectF content(side + 40 * s, 34 * s, w - side - 80 * s, h - 68 * s);
    if (app.page == kPlay) draw_play(d, content);
    else if (app.page == kSettings) draw_settings(d, content);
    else draw_build(d, content);
    if (!app.status.empty())
        d.text(app.status, RectF(content.X, h - 30 * s, content.Width, 20 * s), 12.5f, kGold, 2);
    Graphics screen(hdc);
    screen.DrawImage(&bmp, 0, 0);
}

void redraw() { InvalidateRect(app.hwnd, nullptr, FALSE); }

// ---- pickers ----
wstring pick(bool file, const wchar_t* filter, const wstring& start) {
    wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (file ? FOS_FILEMUSTEXIST : FOS_PICKFOLDERS));
    if (file && filter) {
        COMDLG_FILTERSPEC spec[2] = {{filter, filter}, {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, spec);
    }
    if (!start.empty()) {
        IShellItem* item = nullptr;
        wstring dir = is_dir(start) ? start : parent(start);
        if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(app.hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

// ---- running things ----
wstring python_exe() {
    std::string p = read_file(app.dist + L"\\python.txt");
    wstring w = trim(widen(p));
    if (!w.empty() && exists(w)) return w;
    const wstring own = parent(app.source) + L"\\python\\python.exe";   // (the one the launcher downloaded)
    if (!app.source.empty() && exists(own)) return own;
    return L"python";
}

// runs a command line without a window and waits (at most `ms`): its exit code, or -1
int run_quiet(wstring cmd, DWORD ms) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    CloseHandle(pi.hThread);
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    return (int)code;
}

// a Python 3.10 or newer to run the build with ("python" may be Windows' Store shortcut, which runs nothing)
bool python_ok() {
    return run_quiet(L"\"" + python_exe() + L"\" -c \"import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)\"",
                     20000) == 0;
}

// Python for the build, when the PC has none: python-build-standalone's (github.com/astral-sh/python-build-standalone,
// Python's own license) into <the source's parent>\python, with Windows' own curl.exe and tar.exe; WM_PYTHON_READY
// (wparam 1: done)
void start_python_download() {
    if (app.python_busy) return;
    app.python_busy = true;
    const wstring root = parent(app.source);
    const wstring url = app.cfg.get(L"python_url",
        L"https://github.com/astral-sh/python-build-standalone/releases/download/20261026/"
        L"cpython-3.13.18%2B20261026-x86_64-pc-windows-msvc-install_only.tar.gz");
    app.status = L"Downloading Python for the build (about 30 MB)\u2026";
    redraw();
    HWND w = app.hwnd;
    std::thread([root, url, w] {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        CreateDirectoryW(root.c_str(), nullptr);
        const wstring tgz = root + L"\\python-download.tar.gz";
        bool ok = run_quiet(L"\"" + wstring(sys) + L"\\curl.exe\" -L --fail -s -o \"" + tgz + L"\" \"" + url + L"\"",
                            600000) == 0;
        ok = ok && run_quiet(L"\"" + wstring(sys) + L"\\tar.exe\" -xf \"" + tgz + L"\" -C \"" + root + L"\"", 600000) == 0;
        DeleteFileW(tgz.c_str());
        ok = ok && GetFileAttributesW((root + L"\\python\\python.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
        PostMessageW(w, WM_PYTHON_READY, ok ? 1 : 0, 0);
    }).detach();
}

// an environment block: this process's, with `extra` set (NAME -> value; empty value: removed)
std::vector<wchar_t> env_block(const std::map<wstring, wstring>& extra) {
    std::map<wstring, wstring, bool (*)(const wstring&, const wstring&)> all(
        [](const wstring& a, const wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    wchar_t* e = GetEnvironmentStringsW();
    for (wchar_t* p = e; *p; p += wcslen(p) + 1) {
        wstring kv = p;
        size_t eq = kv.find(L'=', 1);
        if (eq != wstring::npos) all[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    FreeEnvironmentStringsW(e);
    for (auto& [k, v] : extra) {
        if (v.empty()) all.erase(k);
        else all[k] = v;
    }
    std::vector<wchar_t> block;
    for (auto& [k, v] : all) {
        wstring kv = k + L"=" + v;
        block.insert(block.end(), kv.begin(), kv.end());
        block.push_back(0);
    }
    block.push_back(0);
    return block;
}

void play() {
    if (app.game || !built()) return;
    std::map<wstring, wstring> env;
    const int up = (int)setting_value(S_UPSCALER);
    env[L"RX_FSR2"] = up >= 1 ? L"1" : L"";
    env[L"RX_DLSS"] = up == 2 ? L"1" : L"";
    wchar_t b[32];
    swprintf(b, 32, L"%.2f", setting_value(S_SHARP));
    env[L"RX_FSR2_SHARPNESS"] = b;
    env[L"HOS_FULLSCREEN"] = (int)setting_value(S_DISPLAY) ? L"1" : L"";
    static const wchar_t* const sizes[] = {L"1280x720", L"1600x900", L"1920x1080", L"2560x1440"};
    env[L"HOS_WINDOW_SIZE"] = sizes[std::clamp((int)setting_value(S_WINDOW), 0, 3)];
    // VSync off: mailbox (each refresh shows the newest finished frame) or immediate (no wait: tearing); the game then
    // runs to its frame rate limit, past a 60 Hz screen's rate
    static const wchar_t* const present[] = {L"", L"mailbox", L"immediate"};
    env[L"RX_PRESENT"] = present[std::clamp((int)setting_value(S_VSYNC), 0, 2)];
    const wstring mods = app.cfg.get(L"mods");
    env[L"HOS_MODS"] = (int)setting_value(S_MODS_ON) && !mods.empty() ? mods : L"";
    for (auto& [k, v] : app.cfg.v)   // (env.NAME = value: more for the game)
        if (k.rfind(L"env.", 0) == 0 && k.size() > 4) env[k.substr(4)] = v;
    auto block = env_block(env);
    const wstring log = app.cfg.get(L"log", app.dist + L"\\skybound.log");
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    if (out != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = out;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    wstring exe = app.dist + L"\\SkyboundTOTK.exe";
    wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        block.data(), app.dist.c_str(), &si, &pi)) {
        app.status = L"Could not start the game (error " + std::to_wstring(GetLastError()) + L")";
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        redraw();
        return;
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    CloseHandle(pi.hThread);
    app.game = pi.hProcess;
    app.game_exit = STILL_ACTIVE;
    app.game_started = (int64_t)GetTickCount64();
    app.status.clear();
    HANDLE h = pi.hProcess;
    HWND w = app.hwnd;
    std::thread([h, w] {
        WaitForSingleObject(h, INFINITE);
        PostMessageW(w, WM_GAME_EXIT, 0, 0);
    }).detach();
    SetTimer(app.hwnd, 1, 500, nullptr);
    redraw();
}

void add_log(const wstring& line) {
    std::lock_guard<std::mutex> lock(app.mu);
    app.log.push_back(line);
    while (app.log.size() > 400) app.log.pop_front();
}

wstring quote(const wstring& s) { return L"\"" + s + L"\""; }


// the newest pipeline: the source changes folder's or the source's, whichever is newer, then the one next to this
// launcher (after an update the source's is the newest)
int64_t mtime(const wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return (int64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
}
wstring pipeline_path() {
    const wstring ov = app.cfg.get(L"overlay");
    const wstring a = ov.empty() ? wstring() : ov + L"\\launcher\\pipeline.py", b = app.source + L"\\launcher\\pipeline.py";
    const int64_t ta = a.empty() ? -1 : mtime(a), tb = mtime(b);
    if (ta >= 0 || tb >= 0) return ta > tb ? a : b;
    const wstring c = app.dir + L"\\pipeline.py";
    return exists(c) ? c : wstring();
}

// pipeline.py --check in the background: WM_UPDATE_CHECKED when it answered
void start_update_check() {
    if (app.update == App::U_CHECKING || app.source.empty() || !exists(app.source + L"\\a64-kit\\build.py")) return;
    const wstring pipeline = pipeline_path();
    if (pipeline.empty()) return;
    app.update = App::U_CHECKING;
    wstring args = quote(python_exe()) + L" -u " + quote(pipeline) + L" --source " + quote(app.source) + L" --check " +
                   quote(update_repo()) + L" --branch " + quote(update_branch());
    HWND w = app.hwnd;
    const wstring cwd = app.source;
    std::thread([args, w, cwd]() mutable {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        CreatePipe(&rd, &wr, &sa, 0);
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = wr;
        PROCESS_INFORMATION pi{};
        std::string out;
        if (CreateProcessW(nullptr, args.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi)) {
            CloseHandle(wr);
            char buf[4096];
            DWORD n;
            while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            CloseHandle(wr);
            out = "@@updatefail could not run Python";
        }
        CloseHandle(rd);
        auto* copy = new std::string(out);
        PostMessageW(w, WM_UPDATE_CHECKED, 0, (LPARAM)copy);
    }).detach();
}
void update_checked(const std::string& out) {
    app.update = App::U_FAILED;
    app.update_why = "no answer";
    size_t at = 0;
    while (at < out.size()) {
        size_t e = out.find('\n', at);
        std::string l = out.substr(at, e == std::string::npos ? std::string::npos : e - at);
        at = e == std::string::npos ? out.size() : e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.rfind("@@update ", 0) == 0) {
            char sha[64] = {0};
            long long t = 0;
            int used = 0;
            if (sscanf(l.c_str() + 9, "%63s %lld %n", sha, &t, &used) >= 2) {
                app.update = App::U_AVAILABLE, app.update_sha = sha, app.update_time = t;
                app.update_subject = l.substr(9 + used);
            }
        } else if (l.rfind("@@uptodate ", 0) == 0) {
            app.update = App::U_CURRENT, app.update_sha = l.substr(11);
        } else if (l.rfind("@@updatefail ", 0) == 0) {
            app.update = App::U_FAILED, app.update_why = l.substr(13);
        }
    }
    redraw();
}

void start_build(bool fetch = false) {
    if (app.build || app.game || app.python_busy) return;
    const bool fresh = !app.source.empty() && !exists(app.source + L"\\a64-kit\\build.py");
    if (app.source.empty() || (fresh && !exists(app.dir + L"\\pipeline.py"))) {
        app.status = L"Choose the SkyboundTOTK source folder first (the one with a64-kit in it).";
        redraw();
        return;
    }
    if (fresh) fetch = true;   // (the release package's first build: the source from GitHub first)
    CreateDirectoryW(app.source.c_str(), nullptr);
    // (no Python: the launcher's own, then the build; launcher.ini own_python = 1: its own even when the PC has one)
    const bool own_wanted = app.cfg.geti(L"own_python", 0) && !exists(parent(app.source) + L"\\python\\python.exe");
    if (own_wanted || !python_ok()) {
        app.pending_fetch = fetch;
        start_python_download();
        return;
    }
    app.need_msvc_license.clear();
    const wstring ov = app.cfg.get(L"overlay");
    const wstring pipeline = pipeline_path();
    if (pipeline.empty()) {
        app.status = L"launcher\\pipeline.py not found in the source.";
        redraw();
        return;
    }
    wstring args = quote(python_exe()) + L" -u " + quote(pipeline) + L" --source " + quote(app.source);
    const int mode = app.cfg.geti(L"mode", 0);
    auto add = [&](const wchar_t* flag, const wstring& v) {
        if (!v.empty()) args += L" " + wstring(flag) + L" " + quote(v);
    };
    if (mode == 0) {
        add(L"--exefs", app.cfg.get(L"exefs"));
        add(L"--romfs", app.cfg.get(L"romfs"));
    } else {
        if (app.cfg.get(L"nsp").empty() || app.cfg.get(L"update").empty()) {
            app.status = L"Choose both .nsp files (the base game and the 1.4.3 update).";
            redraw();
            return;
        }
        add(L"--nsp", app.cfg.get(L"nsp"));
        add(L"--update", app.cfg.get(L"update"));
        add(L"--keys", app.cfg.get(L"keys"));
    }
    add(L"--overlay", ov);
    if (app.cfg.geti(L"dlss", 0)) args += L" --dlss";
    if (app.cfg.geti(L"msvc_license", 0)) args += L" --msvc-license";
    if (fetch) args += L" --fetch " + quote(update_repo()) + L" --branch " + quote(update_branch());
    CreateDirectoryW(parent(app.dist).c_str(), nullptr);   // (<source>\\dist first: a first build has neither)
    CreateDirectoryW(app.dist.c_str(), nullptr);
    add(L"--log", app.dist + L"\\launcher_build.log");

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    auto block = env_block({{L"PYTHONIOENCODING", L"utf-8"}, {L"PYTHONUNBUFFERED", L"1"}});
    if (!CreateProcessW(nullptr, args.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, block.data(),
                        app.source.c_str(), &si, &pi)) {
        app.status = L"Could not run Python (" + python_exe() + L"): is it installed?";
        CloseHandle(rd);
        CloseHandle(wr);
        redraw();
        return;
    }
    CloseHandle(wr);
    // (a job: Cancel ends the build's whole tree of tools, and so does closing the launcher)
    app.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(app.job, JobObjectExtendedLimitInformation, &li, sizeof li);
    AssignProcessToJobObject(app.job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    app.build = pi.hProcess;
    app.build_ran = true;
    app.build_end.clear();
    app.build_text = L"Starting\u2026";
    for (auto& st : app.steps) st.state = "wait", st.progress = 0;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        app.log.clear();
    }
    add_log(L"> " + args);
    HWND w = app.hwnd;
    HANDLE proc = pi.hProcess;
    std::thread([rd, w, proc] {
        char buf[8192];
        DWORD n;
        std::string part;
        while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) {
            part.append(buf, n);
            std::vector<std::string> lines;
            size_t at;
            while ((at = part.find('\n')) != std::string::npos) {
                std::string l = part.substr(0, at);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
                part.erase(0, at + 1);
            }
            if (!lines.empty()) {
                std::lock_guard<std::mutex> lock(app.mu);
                app.incoming.insert(app.incoming.end(), lines.begin(), lines.end());
                PostMessageW(w, WM_BUILD_LINES, 0, 0);
            }
        }
        CloseHandle(rd);
        WaitForSingleObject(proc, INFINITE);
        PostMessageW(w, WM_BUILD_EXIT, 0, 0);
    }).detach();
    SetTimer(app.hwnd, 2, 50, nullptr);
    redraw();
}

void build_lines() {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        lines.swap(app.incoming);
    }
    for (const std::string& l : lines) {
        if (l.rfind("@@step ", 0) == 0) {
            char key[32] = {0}, state[16] = {0};
            if (sscanf(l.c_str() + 7, "%31s %15s", key, state) == 2)
                for (auto& st : app.steps)
                    if (st.key == key) {
                        st.state = state;
                        if (st.state == "run") st.progress = 0;
                    }
        } else if (l.rfind("@@progress ", 0) == 0) {
            float f = 0;
            char text[512] = {0};
            const int got = sscanf(l.c_str() + 11, "%f %511[^\n]", &f, text);
            for (auto& st : app.steps)
                if (st.state == "run") st.progress = f;
            if (got == 2) app.build_text = widen(text);
        } else if (l.rfind("@@need msvc_license ", 0) == 0) {
            app.need_msvc_license = l.substr(20);
        } else if (l.rfind("@@end ", 0) == 0) {
            app.build_ok = l.compare(6, 2, "ok") == 0;
            app.build_end = widen(l.size() > 9 ? l.substr(app.build_ok ? 9 : 11) : "");
        } else {
            add_log(widen(l));
        }
    }
    redraw();
}

void open_path(const wstring& p, bool select) {
    if (select) {
        wstring a = L"/select,\"" + p + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void click(const Hot& h) {
    auto list = settings_list();
    switch (h.act) {
    case A_PAGE: app.page = h.arg, app.scroll = 0; break;
    case A_GO_BUILD: app.page = kBuild; break;
    case A_PLAY: play(); break;
    case A_OPEN_LOG: open_path(app.cfg.get(L"log"), false); break;
    case A_OPEN_BUILD_LOG: open_path(app.dist + L"\\launcher_build.log", false); break;
    case A_OPEN_FOLDER: open_path(app.dist, false); break;
    case A_PREV:
    case A_NEXT: {
        const int n = (int)list[h.arg].options.size();
        set_setting(h.arg, ((int)setting_value(h.arg) + (h.act == A_NEXT ? 1 : n - 1)) % n);
        break;
    }
    case A_TOGGLE:
        if (h.arg == 1000) {
            app.cfg.v[L"dlss"] = app.cfg.geti(L"dlss", 0) ? L"0" : L"1";
            app.cfg.save();
        } else {
            set_setting(h.arg, setting_value(h.arg) ? 0 : 1);
        }
        break;
    case A_MODE: app.cfg.v[L"mode"] = std::to_wstring(h.arg); app.cfg.save(); break;
    case A_BROWSE: {
        if (h.arg == S_MODS || h.arg == S_LOG) {
            const wchar_t* key = h.arg == S_MODS ? L"mods" : L"log";
            wstring p;
            if (h.arg == S_LOG) {   // (a log file: a folder picked, the file named in it)
                p = pick(false, nullptr, app.cfg.get(key));
                if (!p.empty()) p += L"\\skybound.log";
            } else {
                p = pick(false, nullptr, app.cfg.get(key));
            }
            if (!p.empty()) app.cfg.v[key] = p, app.cfg.save();
        } else if (const PathRow* row = path_row(h.arg)) {
            const wstring cur = row->key == wstring(L"source") ? app.source : app.cfg.get(row->key);
            wstring p = pick(row->file, row->filter, cur);
            if (!p.empty()) {
                app.cfg.v[row->key] = p;
                app.cfg.save();
                if (row->key == wstring(L"source")) load_all();
            }
        }
        break;
    }
    case A_CLEAR:
        app.cfg.v[h.arg == S_MODS ? L"mods" : L"log"] = L"";
        app.cfg.save();
        break;
    case A_BUILD: start_build(); break;
    case A_UPDATE: app.page = kBuild, start_build(true); break;
    case A_CHECK: start_update_check(); break;
    case A_CANCEL:
        if (app.job) TerminateJobObject(app.job, 1);
        app.build_end = L"Cancelled. Build again to continue where it stopped.";
        app.build_ok = false;
        break;
    default: break;
    }
    redraw();
}

void slider_to(int x) {
    if (app.drag_setting < 0) return;
    auto list = settings_list();
    const Setting& st = list[app.drag_setting];
    const RectF& t = app.drag_track;
    float f = t.Width > 0 ? (x - t.X) / t.Width : 0;
    f = std::clamp(f, 0.0f, 1.0f);
    double v = st.lo + f * (st.hi - st.lo);
    v = std::round(v / st.step) * st.step;
    set_setting(app.drag_setting, std::clamp(v, st.lo, st.hi));
    redraw();
}

int hit(int x, int y) {
    for (int i = (int)app.hots.size() - 1; i >= 0; i--)
        if (app.hots[i].act != A_NONE && app.hots[i].r.Contains((float)x, (float)y)) return i;
    return -1;
}

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        if (rc.right > 0 && rc.bottom > 0) paint(hdc, rc.right, rc.bottom);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (app.dragging) { slider_to(x); return 0; }
        const int h = hit(x, y);
        if (h != app.hover) {
            app.hover = h;
            redraw();
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, w, 0};
        TrackMouseEvent(&t);
        SetCursor(LoadCursor(nullptr, h >= 0 ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE: app.hover = -1; redraw(); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, app.hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = hit(x, y);
        if (h >= 0 && app.hots[h].act == A_SLIDER) {
            app.dragging = true;
            app.drag_setting = app.hots[h].arg;
            // (the track: as drawn, from the hot area less its margins)
            RectF r = app.hots[h].r;
            app.drag_track = RectF(r.X + 6 * app.s, r.Y, r.Width - 12 * app.s, r.Height);
            SetCapture(w);
            slider_to(x);
            return 0;
        }
        if (h >= 0) {
            const Hot copy = app.hots[h];
            click(copy);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (app.dragging) {
            app.dragging = false;
            app.drag_setting = -1;
            ReleaseCapture();
            redraw();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (app.page == kSettings) {
            app.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 60 * app.s;
            app.scroll = std::clamp(app.scroll, 0.0f, app.scroll_max);
            redraw();
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
            app.page = (app.page + (GetKeyState(VK_SHIFT) < 0 ? kPages - 1 : 1)) % kPages;
            redraw();
        } else if (wp == VK_RETURN && app.page == kPlay) {
            play();
        } else if (wp == VK_F5) {
            load_all();
            redraw();
        }
        return 0;
    case WM_TIMER:
        if (wp == 2) app.spin = std::fmod(app.spin + 18, 360.0f);
        redraw();
        return 0;
    case WM_ACTIVATE:   // (input.ini may have changed in the game's F1 menu)
        if (LOWORD(wp) != WA_INACTIVE && !app.game) app.ini.load(), redraw();
        break;
    case WM_BUILD_LINES: build_lines(); return 0;
    case WM_BUILD_EXIT: {
        build_lines();
        DWORD code = 1;
        GetExitCodeProcess(app.build, &code);
        CloseHandle(app.build);
        app.build = nullptr;
        if (app.job) CloseHandle(app.job), app.job = nullptr;
        if (app.build_end.empty()) {
            app.build_ok = code == 0;
            app.build_end = code == 0 ? L"Built." : L"The build stopped (exit code " + std::to_wstring(code) + L").";
        }
        for (auto& st : app.steps)
            if (st.state == "run") st.state = app.build_ok ? "done" : "fail";
        KillTimer(w, 2);
        load_all();
        if (app.update == App::U_AVAILABLE || app.update == App::U_FAILED) start_update_check();   // (after an update)
        FlashWindow(w, TRUE);
        redraw();
        if (!app.need_msvc_license.empty()) {   // (the build needs Microsoft's C++ tools: their license, then again)
            const wstring url = widen(app.need_msvc_license);
            app.need_msvc_license.clear();
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            const wstring q = L"The build needs Microsoft's C++ Build Tools (about 1 GB). The launcher can download them "
                              L"from Microsoft into a folder next to the game (no admin, nothing installed system-wide).\n\n"
                              L"Their license just opened in your browser:\n" + url +
                              L"\n\nDo you accept Microsoft's license and want them downloaded?";
            if (MessageBoxW(w, q.c_str(), L"Microsoft C++ Build Tools", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                app.cfg.v[L"msvc_license"] = L"1";
                app.cfg.save();
                start_build();
            }
        }
        return 0;
    }
    case WM_PYTHON_READY: {
        app.python_busy = false;
        if (wp && python_ok()) {
            app.status = L"Python is ready.";
            start_build(app.pending_fetch);
        } else {
            app.status = L"Python could not be downloaded. Install Python 3.10+ from python.org, then Build again.";
        }
        redraw();
        return 0;
    }
    case WM_UPDATE_CHECKED: {
        std::string* out = (std::string*)lp;
        update_checked(*out);
        delete out;
        return 0;
    }
    case WM_GAME_EXIT: {
        DWORD code = 0;
        GetExitCodeProcess(app.game, &code);
        CloseHandle(app.game);
        app.game = nullptr;
        app.game_exit = code;
        KillTimer(w, 1);
        app.ini.load();   // (F1 in the game may have changed them)
        redraw();
        return 0;
    }
    case WM_DPICHANGED: {
        app.s = HIWORD(wp) / 96.0f;
        const RECT* r = (const RECT*)lp;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize = {(LONG)(1000 * app.s), (LONG)(660 * app.s)};
        return 0;
    }
    case WM_CLOSE:
        if (app.build &&
            MessageBoxW(w, L"A build is running. Stop it and close? (Build again later to continue where it stopped.)",
                        L"SkyboundTOTK Launcher", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        if (app.job) TerminateJobObject(app.job, 1);
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    // (sharp on high-dpi screens)
    typedef BOOL(WINAPI * SetCtx)(HANDLE);
    if (auto f = (SetCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        f((HANDLE)-4);   // (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi;
    ULONG_PTR token;
    GdiplusStartup(&token, &gsi, nullptr);
    load_all();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SkyboundLauncher";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    HDC screen = GetDC(nullptr);
    app.s = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);
    RECT r{0, 0, (LONG)(1180 * app.s), (LONG)(720 * app.s)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"SkyboundTOTK Launcher", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    typedef UINT(WINAPI * GetDpi)(HWND);
    if (auto f = (GetDpi)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"))
        if (UINT dpi = f(app.hwnd)) app.s = dpi / 96.0f;
    BOOL dark = TRUE;   // (a dark title bar: DWMWA_USE_IMMERSIVE_DARK_MODE)
    DwmSetWindowAttribute(app.hwnd, 20, &dark, sizeof dark);
    if (!built()) app.page = kBuild;
    if (const wchar_t* p = wcsstr(cmd, L"--page=")) app.page = std::clamp(_wtoi(p + 7), 0, (int)kPages - 1);   // (tests)
    if (wcsstr(cmd, L"--build-now")) app.page = kBuild, start_build();   // (tests)
    ShowWindow(app.hwnd, show);
    UpdateWindow(app.hwnd);
    if (app.cfg.geti(L"update_check", 1) && !wcsstr(cmd, L"--build-now")) start_update_check();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(token);
    CoUninitialize();
    return 0;
}
