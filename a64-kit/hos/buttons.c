/* Button prompts that match the device played with: keyboard keys (the player's own bindings), Xbox or PlayStation
   buttons in place of the Switch's (or the Switch's own). Bindings.button_icons (input.ini button_icons, F1 > Graphics):
   0 the device played with, 1 keyboard and mouse, 2 Xbox, 3 PlayStation, 4 Switch.

   Two places draw them:
   - the prompts in text ("Press [A] to ...") are glyphs of a font in the font archives (Font/<name>.bfarc.zs). The game
     reads those once, at the start, so they are drawn for the device in use then (Auto: the controller connected,
     else the keyboard) and change at the next start. tools/button_icons.py makes the archives for each style (and
     the resource size table that fits them) into <user>/button_icons/<style>/; hos_buttons_romfs serves them in place
     of the game's own (nn_fs.c, before mods).
   - the button guide in play (bottom right) draws its keys from textures, which the renderer swaps as it decodes them
     (gpu/render/rx_texture.cpp, hos_button_hud): those follow the device as it changes (the last one pressed).
   The cache is made again when what it was made from changes: the game's (or a mod's) files, the keyboard bindings,
   the tool. That runs the tool at the start (button_icons.py and python.txt next to the exe: build.py puts them
   there; HOS_PYTHON=<python.exe> overrides), a few seconds, with its output in <user>/button_icons/tool.log. Without
   Python, fontTools or zstd the prompts stay the Switch's. HOS_NO_BUTTON_ICONS=1 leaves all of this out. */
#include "hos.h"
#include "input_bind.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

volatile int hos_device_style;   /* the device last played with: 1 keyboard and mouse, 2 Xbox, 3 PlayStation, 4 Switch */

void hos_note_device(int style) {
    if (hos_device_style == style) return;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 40)
        hos_log("buttons: playing with %s", style == 1 ? "the keyboard and mouse" : style == 2 ? "an Xbox-style controller"
                                            : style == 3 ? "a PlayStation controller" : "a Switch controller");
    hos_device_style = style;
}

#ifdef __EMSCRIPTEN__   /* (the browser: the Switch's prompts) */
int hos_buttons_romfs(const char *rel, char *out, size_t cap) { (void)rel, (void)out, (void)cap; return 0; }
int hos_button_hud_style(void) { return 4; }
int hos_button_hud(uint64_t hash, uint32_t w, uint32_t h, const uint8_t **mask) {
    (void)hash, (void)w, (void)h, (void)mask;
    return 0;
}
#else

extern const char *hos_romfs_dir, *hos_user_dir;
int hos_mods_romfs(const char *rel, char *out, size_t cap);   /* mods.c */
int hos_sdl_connected_style(int wait_ms);                    /* pad_sdl.c */

#define TOOL_VERSION "1"   /* (bumped when button_icons.py draws differently: the cache is made again) */
static const char *const style_names[5] = {"auto", "keyboard", "xbox", "playstation", "switch"};

static int text_style;                  /* the style the prompts in text are drawn in (0 until decided) */
static char serve_dir[MAX_PATH];        /* <cache>/<style>: its files are served ("" none) */
static char served[16][128];            /* ...which (RomFS paths, lower case, '/') */
static int nserved;
typedef struct { uint64_t hash; uint32_t w, h; const uint8_t *mask[3]; } HudEntry;
static HudEntry *hud;
static int nhud;
static uint8_t *hud_data;

static void lower_slash(char *s) {
    for (; *s; s++) *s = *s == '\\' ? '/' : (char)tolower((unsigned char)*s);
}

/* the file the game loads for a RomFS path (a mod's, or the game's own), and whether there is one */
static int effective(const char *rel, char *out, size_t cap) {
    char p[MAX_PATH];
    if (!hos_mods_romfs(rel, p, sizeof p)) snprintf(p, sizeof p, "%s/%s", hos_romfs_dir, rel);
    for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
    if (!GetFullPathNameA(p, (DWORD)cap, out, NULL)) snprintf(out, cap, "%s", p);   /* (the tool runs elsewhere) */
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

typedef struct { char *s; size_t n, cap; } Buf;
static void addf(Buf *b, const char *f, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, f);
        const int n = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? b->cap - b->n : 0, f, ap);
        va_end(ap);
        if (n < 0) return;
        if (b->s && b->n + (size_t)n < b->cap) { b->n += (size_t)n; return; }
        b->cap = (b->cap + (size_t)n + 1) * 2;
        b->s = (char *)realloc(b->s, b->cap);
    }
}

static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 0x100000001b3ull;
    return h;
}

/* the request's source lines (kind rel|host) for the RomFS files matching dir\pattern, and their identity in *id */
static int add_sources(Buf *req, Buf *id, const char *kind, const char *dir, const char *pattern) {
    char find[MAX_PATH];
    snprintf(find, sizeof find, "%s\\%s\\%s", hos_romfs_dir, dir, pattern);
    for (char *p = find; *p; p++) if (*p == '/') *p = '\\';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(find, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char rel[MAX_PATH], host[MAX_PATH];
        snprintf(rel, sizeof rel, "%s/%s", dir, fd.cFileName);
        if (!effective(rel, host, sizeof host)) continue;
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExA(host, GetFileExInfoStandard, &a)) continue;
        addf(req, "%s %s|%s\n", kind, rel, host);
        addf(id, "%s|%s|%lu|%lu|%lu|%lu\n", rel, host, a.nFileSizeHigh, a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime,
             a.ftLastWriteTime.dwLowDateTime);
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

static char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    const size_t got = s ? fread(s, 1, (size_t)(n > 0 ? n : 0), f) : 0;
    fclose(f);
    if (!s) return NULL;
    s[got] = 0;
    if (size) *size = got;
    return s;
}

static void exe_dir(char *out, size_t cap) {
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    while (n && out[n - 1] != '\\' && out[n - 1] != '/') n--;
    out[n] = 0;
}

/* runs the tool on the request; 1 if it ran and finished */
static int run_tool(const char *cache, const char *request) {
    char dir[MAX_PATH], tool[MAX_PATH], python[MAX_PATH] = "python";
    exe_dir(dir, sizeof dir);
    snprintf(tool, sizeof tool, "%sbutton_icons.py", dir);
    if (GetFileAttributesA(tool) == INVALID_FILE_ATTRIBUTES) {
        hos_log("buttons: %s not found (build.py puts it there): the Switch's button prompts", tool);
        return 0;
    }
    if (getenv("HOS_PYTHON")) snprintf(python, sizeof python, "%s", getenv("HOS_PYTHON"));
    else {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%spython.txt", dir);
        char *s = read_file(path, NULL);
        if (s) {
            char *e = s + strlen(s);
            while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
            if (*s) snprintf(python, sizeof python, "%s", s);
            free(s);
        }
    }
    char log_path[MAX_PATH], cmd[3 * MAX_PATH + 32];
    snprintf(log_path, sizeof log_path, "%s\\tool.log", cache);
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" \"%s\"", python, tool, request);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE log = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si = {0};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    hos_log("buttons: making the button icons (%s)", cmd);
    const int64_t t0 = hos_now_ns();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cache, &si, &pi)) {
        hos_log("buttons: could not run %s (error %lu): the Switch's button prompts", python, GetLastError());
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        return 0;
    }
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 180000) != WAIT_OBJECT_0) {
        hos_log("buttons: the tool took over 3 minutes: stopped");
        TerminateProcess(pi.hProcess, 1);
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    hos_log("buttons: the tool %s in %.1f s (its output: %s)", code ? "failed" : "finished", (hos_now_ns() - t0) / 1e9,
            log_path);
    return code == 0;
}

static void load_hud(const char *cache) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\hud.bin", cache);
    size_t size = 0;
    char *d = read_file(path, &size);
    uint32_t head[4];
    if (!d || size < 16 || memcmp(d, "SKBH", 4)) { free(d); return; }
    memcpy(head, d, 16);
    const uint32_t count = head[2], styles = head[3];
    if (head[1] != 1 || styles != 3 || count > 64) { free(d); return; }
    HudEntry *e = (HudEntry *)calloc(count ? count : 1, sizeof *e);
    size_t at = 16;
    uint32_t n = 0;
    for (; n < count; n++) {
        if (at + 16 > size) break;
        memcpy(&e[n].hash, d + at, 8);
        memcpy(&e[n].w, d + at + 8, 4);
        memcpy(&e[n].h, d + at + 12, 4);
        at += 16;
        const size_t one = (size_t)e[n].w * e[n].h;
        if (!one || one > 1024 * 1024 || at + one * 3 > size) break;
        for (int s = 0; s < 3; s++) e[n].mask[s] = (const uint8_t *)d + at + one * s;
        at += one * 3;
    }
    hud_data = (uint8_t *)d;
    hud = e;
    nhud = (int)n;
    hos_log("buttons: %d button guide textures", nhud);
}

static void prepare(void) {
    if (getenv("HOS_NO_BUTTON_ICONS") || !hos_romfs_dir) return;
    Bindings b;
    hos_bind_get(&b);
    int style = b.button_icons;
    if (!style) {   /* Auto: the controller connected (SDL looks first), else the keyboard */
        style = hos_sdl_connected_style(1500);
        if (!style) {
            typedef DWORD(WINAPI * GetStateFn)(DWORD, void *);
            HMODULE x = LoadLibraryA("xinput1_4.dll");
            if (!x) x = LoadLibraryA("xinput9_1_0.dll");
            GetStateFn get = x ? (GetStateFn)GetProcAddress(x, "XInputGetState") : NULL;
            uint8_t state[16];
            for (DWORD p = 0; get && p < 4 && !style; p++)
                if (get(p, state) == ERROR_SUCCESS) style = 2;
        }
        if (!style) style = 1;
    }
    text_style = style;
    hos_log("buttons: prompts in text: %s%s", style_names[style], b.button_icons ? "" : " (the device found at the start)");
    if (b.button_icons == 4) return;   /* (the Switch's, always: nothing to make) */

    char cache[MAX_PATH], rel_cache[MAX_PATH];
    snprintf(rel_cache, sizeof rel_cache, "%s\\button_icons", hos_user_dir ? hos_user_dir : "user");
    for (char *p = rel_cache; *p; p++) if (*p == '/') *p = '\\';
    if (!GetFullPathNameA(rel_cache, sizeof cache, cache, NULL)) snprintf(cache, sizeof cache, "%s", rel_cache);
    CreateDirectoryA(cache, NULL);

    /* what the cache is made from: the game's files (or a mod's), the keyboard bindings, the tool */
    Buf req = {0}, id = {0};
    char host[MAX_PATH];
    if (!effective("Pack/ZsDic.pack.zs", host, sizeof host)) {
        hos_log("buttons: no Pack/ZsDic.pack.zs in the RomFS: the Switch's button prompts");
        return;
    }
    addf(&req, "zsdic %s\n", host);
    addf(&id, "v" TOOL_VERSION "\n%s\n", host);
    const int fonts = add_sources(&req, &id, "font", "Font", "*.bfarc.zs");
    const int tables = add_sources(&req, &id, "restbl", "System/Resource", "ResourceSizeTable.*.rsizetable.zs");
    add_sources(&req, &id, "layout", "UI/LayoutArchive", "Common.*.blarc.zs");
    if (!fonts || !tables) {
        hos_log("buttons: no font archives (%d) or resource size table (%d) found: the Switch's button prompts", fonts,
                tables);
        free(req.s), free(id.s);
        return;
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        const int k = b.key[a][0] ? b.key[a][0] : b.key[a][1];
        addf(&req, "bind %d %s\n", a, hos_bind_key_name(k));
        addf(&id, "%d=%s\n", a, hos_bind_key_name(k));
    }
    addf(&req, "mouse_camera %d\n", b.mouse_camera);
    addf(&id, "camera=%d\n", b.mouse_camera);
    char tool[MAX_PATH];   /* (the tool itself, by its contents: build.py copies it anew each build) */
    exe_dir(tool, sizeof tool);
    strncat(tool, "button_icons.py", sizeof tool - strlen(tool) - 1);
    size_t tool_size = 0;
    char *tool_text = read_file(tool, &tool_size);
    if (tool_text) addf(&id, "tool|%016llx\n", (unsigned long long)fnv(tool_text, tool_size, 0xcbf29ce484222325ull));
    free(tool_text);
    char stamp[32];
    snprintf(stamp, sizeof stamp, "%016llx", (unsigned long long)fnv(id.s, id.n, 0xcbf29ce484222325ull));
    addf(&req, "stamp %s\nout %s\n", stamp, cache);
    free(id.s);

    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\stamp.txt", cache);
    char *have = read_file(path, NULL);
    int ok = have && !strncmp(have, stamp, 16);
    free(have);
    if (!ok) {
        snprintf(path, sizeof path, "%s\\request.txt", cache);
        FILE *f = fopen(path, "wb");
        if (f) fwrite(req.s, 1, req.n, f), fclose(f);
        if (f && run_tool(cache, path)) {
            snprintf(path, sizeof path, "%s\\stamp.txt", cache);
            have = read_file(path, NULL);
            ok = have && !strncmp(have, stamp, 16);
            free(have);
        }
        if (!ok) hos_log("buttons: the button icons could not be made: the Switch's button prompts (see tool.log)");
    } else {
        hos_log("buttons: button icons from %s (up to date)", cache);
    }
    free(req.s);
    if (!ok) return;
    load_hud(cache);
    if (style == 4) return;
    snprintf(serve_dir, sizeof serve_dir, "%s\\%s", cache, style_names[style]);
    snprintf(path, sizeof path, "%s\\files.txt", serve_dir);
    char *list = read_file(path, NULL);
    for (char *line = list ? strtok(list, "\r\n") : NULL; line && nserved < 16; line = strtok(NULL, "\r\n"))
        if (*line && strlen(line) < sizeof served[0]) {
            snprintf(served[nserved], sizeof served[0], "%s", line);
            lower_slash(served[nserved++]);
        }
    free(list);
    hos_log("buttons: %d files served from %s", nserved, serve_dir);
}

static BOOL CALLBACK prepare_once(PINIT_ONCE once, void *param, void **ctx) {
    (void)once, (void)param, (void)ctx;
    prepare();
    return TRUE;
}

/* nn_fs.c: the file to load for a RomFS path ("Font/x.zs"), when it is one with the button icons drawn: out, 1 */
int hos_buttons_romfs(const char *rel, char *out, size_t cap) {
    /* (the cache is made at the first of these files the game opens: the size table comes first) */
    if (_strnicmp(rel, "Font/", 5) && _strnicmp(rel, "System/Resource/ResourceSizeTable", 33)) return 0;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, prepare_once, NULL, NULL);
    if (!nserved) return 0;
    char key[MAX_PATH];
    snprintf(key, sizeof key, "%s", rel);
    lower_slash(key);
    for (int i = 0; i < nserved; i++)
        if (!strcmp(served[i], key)) {
            snprintf(out, cap, "%s\\%s", serve_dir, rel);
            for (char *p = out; *p; p++) if (*p == '/') *p = '\\';
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 16) hos_log("buttons: %s <- %s", rel, out);
            return 1;
        }
    return 0;
}

/* The style the button guide is drawn in now: the setting, or the device last played with (the renderer, each frame) */
int hos_button_hud_style(void) {
    const int s = hos_button_icons_setting;
    if (s >= 1 && s <= 4) return s;
    return hos_device_style ? hos_device_style : text_style ? text_style : 4;
}

/* The renderer, decoding a 1-byte texture of w x h whose pixels hash to `hash` (FNV-1a 64): whether it is one of the
   button guide's, and *mask its pixels in the style now (NULL: its own) */
int hos_button_hud(uint64_t hash, uint32_t w, uint32_t h, const uint8_t **mask) {
    *mask = NULL;
    for (int i = 0; i < nhud; i++)
        if (hud[i].hash == hash && hud[i].w == w && hud[i].h == h) {
            const int s = hos_button_hud_style();
            if (s >= 1 && s <= 3) *mask = hud[i].mask[s - 1];
            return 1;
        }
    return 0;
}
#endif
