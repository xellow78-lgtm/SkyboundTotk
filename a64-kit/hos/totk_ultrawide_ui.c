/* The HUD at other aspect ratios (totk_ultrawide.c): the cameras see the other shape and the present shows the 16:9
   frame at that shape, so the 2D layouts, made for 16:9, must be squeezed back by the same factor to keep their
   shapes. TOTK's layouts live in one archive, content:/UI/LayoutArchive/Common.Product.<version>.Nin_NX_NVN.blarc.zs
   (zstd with the game's zs.zsdic dictionary, a SARC of .bflyt layouts and .bflan animations).
   At the start (hos_aspect_init) tools/button_icons.py --layout makes a copy for the shape from the player's own
   archive (a mod's, when a mod has it): every layout's root pane squeezed, full-screen backdrops stretched back to the
   edges, the HUD's parts moved out to the screen's corners and edges (HOS_HUD=center: they stay in the middle 16:9);
   the pane lists are MacNativeGames' (MIT; see the tool). It is cached in <user>/ultrawide_ui. The first open of the
   archive gets that copy. Without Python (or if the tool fails) the copy is made here, roots only: every layout but
   the full-screen ones squeezed, the HUD in the middle. zstd: third_party/zstd (BSD). */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../third_party/zstd/zstddeclib.c"

extern float hos_view_aspect;
extern const char *hos_user_dir, *hos_romfs_dir;
int hos_mods_romfs(const char *rel, char *out, size_t cap);
int hos_run_tool(const char *cwd, const char *args, const char *log_name);   /* buttons.c */
static char prepared[MAX_PATH];   /* the tool's copy for this session's shape */

/* (each build names its layout archive after itself: v1.4.3's is Common.Product.140.Nin_NX_NVN.blarc.zs, v1.2.1's
   .110, v1.0.0's .100) */
#if GAME_VERSION == 143
static const char *const kArchive = "content:/UI/LayoutArchive/Common.Product.140.Nin_NX_NVN.blarc.zs";
#elif GAME_VERSION == 121
static const char *const kArchive = "content:/UI/LayoutArchive/Common.Product.110.Nin_NX_NVN.blarc.zs";
#else
static const char *const kArchive = "content:/UI/LayoutArchive/Common.Product.100.Nin_NX_NVN.blarc.zs";
#endif

static uint8_t *read_host(const char *path, size_t *n) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    uint8_t *b = (uint8_t *)malloc((size_t)sz.QuadPart);
    DWORD got = 0;
    size_t done = 0;
    while (b && done < (size_t)sz.QuadPart &&
           ReadFile(h, b + done, (DWORD)((size_t)sz.QuadPart - done > (1u << 30) ? (1u << 30) : (size_t)sz.QuadPart - done), &got, NULL) && got)
        done += got;
    CloseHandle(h);
    if (b && done != (size_t)sz.QuadPart) { free(b); return NULL; }
    *n = done;
    return b;
}

static uint8_t *unzstd(const uint8_t *src, size_t n, const void *dict, size_t dict_size, size_t *out_n) {
    const unsigned long long size = ZSTD_getFrameContentSize(src, n);
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > (1ull << 30)) return NULL;
    uint8_t *out = (uint8_t *)malloc((size_t)size);
    ZSTD_DCtx *dc = ZSTD_createDCtx();
    const size_t r = dict ? ZSTD_decompress_usingDict(dc, out, (size_t)size, src, n, dict, dict_size)
                          : ZSTD_decompressDCtx(dc, out, (size_t)size, src, n);
    ZSTD_freeDCtx(dc);
    if (ZSTD_isError(r) || r != size) { free(out); return NULL; }
    *out_n = r;
    return out;
}

/* SARC: each file's name, data and size, in order; 0 = done */
typedef struct { const char *name; uint8_t *data; size_t size; } SarcFile;
static int sarc_files(uint8_t *b, size_t n, SarcFile *out, int cap) {
    if (n < 0x14 || memcmp(b, "SARC", 4)) return -1;
    const uint16_t hdr = *(uint16_t *)(b + 4);
    const uint32_t data_off = *(uint32_t *)(b + 0xC);
    const uint16_t count = *(uint16_t *)(b + hdr + 6);
    const size_t names = hdr + 12 + 16u * count + 8;
    int k = 0;
    for (int i = 0; i < count && k < cap; i++) {
        const uint32_t *node = (const uint32_t *)(b + hdr + 12 + 16u * i);
        const uint32_t attr = node[1], s = node[2], e = node[3];
        if (data_off + e > n) return -1;
        out[k].name = (const char *)(b + names + (attr & 0xFFFF) * 4u);
        out[k].data = b + data_off + s;
        out[k].size = e - s;
        k++;
    }
    return k;
}

static uint8_t *find_bytes(uint8_t *h, size_t n, const char *needle) {
    const size_t m = strlen(needle);
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(h + i, needle, m)) return h + i;
    return NULL;
}

/* full-screen layers: they keep filling the whole (stretched) view */
static int full_screen(const char *name) {
    return strstr(name, "BG") || strstr(name, "Fade") || strstr(name, "Wipe") || strstr(name, "Shutter") ||
           strstr(name, "Overlay") || strstr(name, "Pa_Map_00");
}

/* the layouts squeezed: a .bflyt's root pane (pan1: its name 0x0C in, its scale x 0x38 after the name, y 0x3C) */
static int narrow_layouts(uint8_t *sarc, size_t n, float sx, float sy) {
    SarcFile files[4096];
    const int count = sarc_files(sarc, n, files, 4096);
    int patched = 0;
    for (int i = 0; i < count; i++) {
        const char *nm = files[i].name;
        const size_t len = strlen(nm);
        if (strncmp(nm, "blyt/", 5) || len < 7 || strcmp(nm + len - 6, ".bflyt") || full_screen(nm)) continue;
        uint8_t *root = find_bytes(files[i].data, files[i].size, "RootPane");
        if (!root || root + 0x40 > files[i].data + files[i].size) continue;
        memcpy(root + 0x38, &sx, 4);
        memcpy(root + 0x3C, &sy, 4);
        patched++;
    }
    return patched;
}

/* a zstd frame of raw blocks (no compression): what the game's decompressor reads as it is */
static int write_raw_zstd(const char *path, const uint8_t *b, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const uint8_t head[5] = {0x28, 0xB5, 0x2F, 0xFD, 0xE0};   /* magic; single segment, 8-byte content size */
    fwrite(head, 1, 5, f);
    const uint64_t size = n;
    fwrite(&size, 1, 8, f);
    for (size_t done = 0; done < n || n == 0;) {
        const size_t chunk = n - done > 131072 ? 131072 : n - done;
        const uint32_t last = done + chunk == n;
        const uint32_t bh = last | (0u << 1) | (uint32_t)(chunk << 3);   /* raw block */
        const uint8_t bb[3] = {(uint8_t)bh, (uint8_t)(bh >> 8), (uint8_t)(bh >> 16)};
        fwrite(bb, 1, 3, f);
        fwrite(b + done, 1, chunk, f);
        done += chunk;
        if (last) break;
    }
    fclose(f);
    return 1;
}

static void full_path(const char *p, char *out, size_t cap) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *c = tmp; *c; c++) if (*c == '/') *c = '\\';
    if (!GetFullPathNameA(tmp, (DWORD)cap, out, NULL)) snprintf(out, cap, "%s", tmp);
}
/* a RomFS file as the game loads it (a mod's first), as a full path; 0 if there is none */
static int romfs_file(const char *rel, char *out, size_t cap) {
    char p[MAX_PATH];
    if (!hos_mods_romfs(rel, p, sizeof p)) snprintf(p, sizeof p, "%s/%s", hos_romfs_dir ? hos_romfs_dir : "romfs", rel);
    full_path(p, out, cap);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

/* totk_ultrawide.c, at the start with another shape: the tool's copy of the layout archive for it (made once for each
   shape, HUD placement and source file) */
void hos_ultrawide_ui_prepare(void) {
    prepared[0] = 0;
    if (hos_view_aspect <= 0) return;
    char src[MAX_PATH], dic[MAX_PATH], dir[MAX_PATH], rel[MAX_PATH];
    if (!romfs_file(kArchive + 9, src, sizeof src) || !romfs_file("Pack/ZsDic.pack.zs", dic, sizeof dic)) {
        hos_log("ultrawide: no %s: the HUD stays stretched", kArchive + 9);
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA a;
    GetFileAttributesExA(src, GetFileExInfoStandard, &a);
    snprintf(rel, sizeof rel, "%s/ultrawide_ui", hos_user_dir ? hos_user_dir : "user");
    full_path(rel, dir, sizeof dir);
    CreateDirectoryA(dir, NULL);
    const char *e = getenv("HOS_HUD");
    const char *hud = e && !_stricmp(e, "center") ? "center" : "corner";
    const int w = (int)(hos_view_aspect * 10000 + 0.5f), h = 10000;
    char out[MAX_PATH];
    snprintf(out, sizeof out, "%s\\layout2_%d_%s_%lu_%08lx%08lx.blarc.zs", dir, w, hud, a.nFileSizeLow,
             a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
        char args[4 * MAX_PATH];
        snprintf(args, sizeof args, "--layout \"%s\" \"%s\" \"%s\" %d %d %s", src, dic, out, w, h, hud);
        if (!hos_run_tool(dir, args, "layout.log") || GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
            hos_log("ultrawide: the HUD's layouts could not be made by the tool (%s\\layout.log): roots only", dir);
            return;
        }
    }
    snprintf(prepared, sizeof prepared, "%s", out);
    hos_log("ultrawide: the HUD's layouts for %.4f (HUD %s): %s", hos_view_aspect, hud, out);
}

/* nn_fs.c, OpenFile: the copy for the layout archive at another aspect ratio (0: the file itself) */
int hos_ultrawide_redirect(const char *guest, char *out, size_t cap) {
    if (hos_view_aspect <= 0 || strncmp(guest, "content:", 8)) return 0;
    const char *rel = guest + 8;
    while (*rel == '/') rel++;
    if (strcmp(rel, kArchive + 9)) return 0;   /* (kArchive past "content:/") */
    if (prepared[0]) {
        snprintf(out, cap, "%s", prepared);
        return 1;
    }
    char dir[600];
    snprintf(dir, sizeof dir, "%s/ultrawide_ui", hos_user_dir ? hos_user_dir : "user");
    CreateDirectoryA(dir, NULL);
    snprintf(out, cap, "%s/roots_%05d.blarc.zs", dir, (int)(hos_view_aspect * 10000 + 0.5f));
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 1;   /* (made before) */
    char orig[600], dic_path[600];
    if (!hos_fs_host_path(kArchive, orig, sizeof orig) ||
        !hos_fs_host_path("content:/Pack/ZsDic.pack.zs", dic_path, sizeof dic_path))
        return 0;
    size_t an = 0, dn = 0, pn = 0, sn = 0;
    uint8_t *arch = read_host(orig, &an), *dics = read_host(dic_path, &dn), *pack = NULL, *sarc = NULL;
    int ok = 0;
    if (arch && dics && (pack = unzstd(dics, dn, NULL, 0, &pn))) {
        SarcFile f[16];
        const int k = sarc_files(pack, pn, f, 16);
        for (int i = 0; i < k && !sarc; i++)
            if (!strcmp(f[i].name, "zs.zsdic")) sarc = unzstd(arch, an, f[i].data, f[i].size, &sn);
        if (sarc) {
            const float k = (16.0f / 9.0f) / hos_view_aspect;   /* (> 1 below 16:9: the height is squeezed) */
            const float sx = k < 1 ? k : 1, sy = k > 1 ? 1 / k : 1;
            const int n = narrow_layouts(sarc, sn, sx, sy);
            ok = n > 0 && write_raw_zstd(out, sarc, sn);
            hos_log("ultrawide: the HUD's layout roots squeezed to %.3f x %.3f (%d layouts) -> %s", sx, sy, n,
                    ok ? out : "(failed)");
        }
    }
    if (!ok) hos_log("ultrawide: the HUD's layout archive could not be made: the HUD stays stretched");
    free(arch);
    free(dics);
    free(pack);
    free(sarc);
    return ok;
}
