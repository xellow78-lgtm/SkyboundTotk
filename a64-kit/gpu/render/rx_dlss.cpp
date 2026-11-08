// NVIDIA DLSS Super Resolution (NGX) on wgpu's Vulkan device, in FSR 2's place (rx_fsr2.cpp gives it the same inputs:
// the scene's colour, depth and motion vectors at the render size, the jitter, Link's mask; the output at the
// display size, then the same blit into the game's target).
//
// Needs (build.py turns RX_DLSS on when they are there): wgpu_native.dll built with the TOTK Vulkan interop
// (third_party/wgpu-native/totk-src: wgpuTotk*), NVIDIA's DLSS SDK files and the Vulkan headers
// (third_party/wgpu-native/build_dlss_deps.py fetches both; not in the repository), nvngx_dlss.dll next to the exe.
// On: RX_DLSS=1 (with RX_FSR2=1: the same hook). Vulkan only (RX_BACKEND is made vulkan).
//
// How: NGX wants the Vulkan instance / device extensions it needs before they are made (dlss_before_instance,
// dlss_before_device), then the device (dlss_after_device: NGX init, DLSS available?). Each frame dlss_record puts a
// raw command into the frame's encoder (wgpuTotkCommandEncoderRaw): wgpu moves the inputs to the sampled layout and
// the output to the storage one, then, when the encoder is finished, the callback evaluates DLSS on the raw
// VkCommandBuffer. DLSS puts the images back in the states it got them in.
//
// The feature (NGX's network for a mode, an output size and a render size): a mode takes render sizes in its
// "dynamic" range only (NGX says which: about 50%..100% of the output for Quality / Balanced / Performance at
// 1080p; none for Ultra Performance), so a feature is made at the largest render size when the game's size is in
// that range, else at exactly the game's size (the menus' 800x448, Ultra Performance's 640x360). Up to 4 are kept
// (each one made is a hitch of up to a few seconds); 30 failures in a row turn DLSS off, FSR 2 takes over.
//
// The model (NGX's render preset), every mode the same: dlss_preset in input.ini (F1 > Graphics, the launcher; live: the
// features are made again): NVIDIA's default for each mode (in the SDK used: K for DLAA / Quality / Balanced, M for
// Performance, L for Ultra Performance: transformer models), or K, M, L or J for all. RX_DLSS_PRESET=<NGX's number>
// overrides. RX_DLSS_MASK=0 (no bias mask from Link's
// G-buffer marks), RX_DLSS_JX / RX_DLSS_JY / RX_DLSS_MVX / RX_DLSS_MVY = -1 (a sign, checking), RX_DLSS_LOG=1 (NGX's
// log into the game's log).
#include "rx_internal.h"

#if RX_DLSS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <mutex>
#include <utility>
#include <vector>

// wgpu-native's TOTK exports (totk-src/wgpu-native/src/totk_vk.rs)
extern "C" {
extern volatile int hos_dlss_preset;   // hos/input_bind.c: the DLSS model setting
struct WGPUTotkVulkanHandles {
    uint64_t instance, physical_device, device, queue;
    uint32_t queue_family, queue_index;
    const void* get_instance_proc_addr;
    const void* get_device_proc_addr;
};
void wgpuTotkAddVulkanExtensions(const char* const* instance_extensions, size_t instance_count,
                                 const char* const* device_extensions, size_t device_count);
bool wgpuTotkAdapterVulkanHandles(WGPUAdapter adapter, WGPUTotkVulkanHandles* out);
bool wgpuTotkDeviceVulkanHandles(WGPUDevice device, WGPUTotkVulkanHandles* out);
uint64_t wgpuTotkTextureVkImage(WGPUTexture texture);
uint64_t wgpuTotkTextureViewVkImageView(WGPUTextureView view);
}

namespace rx {
namespace {

constexpr uint32_t kResource = 1u << 4, kStorageRW = 1u << 10;   // (wgpu's TextureUses)
// (a project ID: NGX wants one that looks like a GUID when the application has no NVIDIA ID; made up for this port)
const char* const kProjectId = "6b0f3c52-8d1e-4a77-9c2b-5e4f7a1d9038";

struct State {
    bool wanted = false, extensions_asked = false, ready = false;
    std::atomic<bool> failed{false};
    WGPUTotkVulkanHandles vk{};
    NVSDK_NGX_Parameter* params = nullptr;
    // the features made (kept: each one made again is a hitch), the one in use is `feature` with its creation sizes
    struct Slot {
        NVSDK_NGX_Handle* h = nullptr;
        uint32_t w = 0, h_ = 0, dw = 0, dh = 0;
        int quality = -1;
        int64_t used = 0;
    };
    static constexpr int kSlots = 4;
    Slot slots[kSlots];
    std::vector<std::pair<NVSDK_NGX_Handle*, int64_t>> graveyard;   // (released later: release_old)
    NVSDK_NGX_Handle* feature = nullptr;
    uint32_t fw = 0, fh = 0, fdw = 0, fdh = 0;
    int fquality = -1;
    unsigned preset = 0;   // (NGX's render preset the features were made with: 0 its default)
    std::mutex mu;   // (NGX is not thread safe: the callbacks run where encoders are finished)
    int64_t evaluations = 0, errors = 0, creations = 0;
    int failures_in_a_row = 0;
    // each mode's render sizes for an output size (NGX_DLSS_GET_OPTIMAL_SETTINGS), asked once per output size
    struct Range {
        bool known = false, ok = false;
        uint32_t opt_w, opt_h, min_w, min_h, max_w, max_h;
    };
    uint32_t range_dw = 0, range_dh = 0;
    Range ranges[6];
};
// the render preset to make features with: RX_DLSS_PRESET (NGX's number), else dlss_preset (0 NGX's default)
unsigned preset_now() {
    static const char* env = getenv("RX_DLSS_PRESET");
    if (env) return static_cast<unsigned>(std::atoi(env));
    static const unsigned kPresets[5] = {0, NVSDK_NGX_DLSS_Hint_Render_Preset_K, NVSDK_NGX_DLSS_Hint_Render_Preset_M,
                                         NVSDK_NGX_DLSS_Hint_Render_Preset_L, NVSDK_NGX_DLSS_Hint_Render_Preset_J};
    const int i = hos_dlss_preset;
    return kPresets[i >= 0 && i < 5 ? i : 0];
}
const char* preset_name(unsigned p) {
    static char b[8];
    std::snprintf(b, sizeof b, "%c", p >= 1 && p <= 26 ? (char)('A' + p - 1) : '?');
    return b;
}

State& S() {
    static State s;
    return s;
}

std::wstring data_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

const NVSDK_NGX_FeatureCommonInfo* common_info() {
    static NVSDK_NGX_FeatureCommonInfo info{};
    static std::wstring dir = data_path();
    static const wchar_t* paths[1] = {dir.c_str()};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    if (env_flag("RX_DLSS_LOG")) {
        info.LoggingInfo.LoggingCallback = [](const char* m, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
            hos_log("rx: NGX: %s", m);
        };
        info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
        info.LoggingInfo.DisableOtherLoggingSinks = true;
    }
    return &info;
}

NVSDK_NGX_FeatureDiscoveryInfo discovery() {
    NVSDK_NGX_FeatureDiscoveryInfo d{};
    d.SDKVersion = NVSDK_NGX_Version_API;
    d.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    d.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d.Identifier.v.ProjectDesc.EngineVersion = "1.0";
    static std::wstring dir = data_path();
    d.ApplicationDataPath = dir.c_str();
    d.FeatureInfo = common_info();
    return d;
}

void add_extensions(uint32_t n, const VkExtensionProperties* props, bool device) {
    std::vector<const char*> names;
    std::string list;
    // (RX_DLSS_DROP_EXT=<name>[,...], checking: those device extensions are not asked for)
    const char* drop = device ? getenv("RX_DLSS_DROP_EXT") : nullptr;
    for (uint32_t i = 0; i < n && props; i++) {
        if (drop && std::strstr(drop, props[i].extensionName)) {
            hos_log("rx: DLSS: device extension %s left out (RX_DLSS_DROP_EXT)", props[i].extensionName);
            continue;
        }
        names.push_back(props[i].extensionName);
        list += " ";
        list += props[i].extensionName;
    }
    if (device) wgpuTotkAddVulkanExtensions(nullptr, 0, names.data(), names.size());
    else wgpuTotkAddVulkanExtensions(names.data(), names.size(), nullptr, 0);
    hos_log("rx: DLSS: %s extensions NGX asks for:%s", device ? "device" : "instance", list.empty() ? " none" : list.c_str());
}

VkFormat vk_format(WGPUTextureFormat f) {
    switch (f) {
    case WGPUTextureFormat_RG11B10Ufloat: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case WGPUTextureFormat_RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case WGPUTextureFormat_RG16Float: return VK_FORMAT_R16G16_SFLOAT;
    case WGPUTextureFormat_R32Float: return VK_FORMAT_R32_SFLOAT;
    case WGPUTextureFormat_R16Float: return VK_FORMAT_R16_SFLOAT;
    case WGPUTextureFormat_R8Unorm: return VK_FORMAT_R8_UNORM;
    case WGPUTextureFormat_RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    default: return VK_FORMAT_UNDEFINED;
    }
}

NVSDK_NGX_Resource_VK resource(WGPUTexture t, WGPUTextureView v, bool rw) {
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    return NVSDK_NGX_Create_ImageView_Resource_VK(
        reinterpret_cast<VkImageView>(static_cast<uintptr_t>(wgpuTotkTextureViewVkImageView(v))),
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(wgpuTotkTextureVkImage(t))), range,
        vk_format(wgpuTextureGetFormat(t)), wgpuTextureGetWidth(t), wgpuTextureGetHeight(t), rw);
}

// one frame's evaluation, for the callback (it runs later, where the encoder is finished)
struct Call {
    NVSDK_NGX_Resource_VK color, depth, mv, mask, out;
    bool has_mask;
    uint32_t rw, rh, mw, mh, dw, dh;
    float jx, jy, mvx, mvy, dt_ms, sharpness;
    int reset, quality;
};

const char* result_text(NVSDK_NGX_Result r) {
    static thread_local char b[160];
    const wchar_t* w = GetNGXResultAsString(r);
    std::snprintf(b, sizeof b, "0x%08x %ls", (unsigned)r, w ? w : L"");
    return b;
}

// the render sizes NGX takes for a mode at this output size (its "dynamic" range: a feature of that mode takes any
// render size in it; a mode without one (Ultra Performance, seen: min = max) takes only the size it was made for)
const State::Range& range(State& s, uint32_t dw, uint32_t dh, int quality) {
    static const State::Range none{};
    if (quality < 0 || quality > 5) return none;
    if (s.range_dw != dw || s.range_dh != dh) {
        for (auto& r : s.ranges) r = State::Range{};
        s.range_dw = dw, s.range_dh = dh;
    }
    State::Range& r = s.ranges[quality];
    if (r.known) return r;
    r.known = true;
    // (what NVIDIA's NGX_DLSS_GET_OPTIMAL_SETTINGS in nvsdk_ngx_helpers.h does; that header pulls in the D3D and
    // CUDA ones, not fetched here)
    NVSDK_NGX_Result res = NVSDK_NGX_Result_FAIL_OutOfDate;
    void* cb = nullptr;
    NVSDK_NGX_Parameter_GetVoidPointer(s.params, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, &cb);
    if (cb) {
        NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_Parameter_Width, dw);
        NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_Parameter_Height, dh);
        NVSDK_NGX_Parameter_SetI(s.params, NVSDK_NGX_Parameter_PerfQualityValue, quality);
        NVSDK_NGX_Parameter_SetI(s.params, NVSDK_NGX_Parameter_RTXValue, 0);
        res = reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(cb)(s.params);
    }
    if (NVSDK_NGX_SUCCEED(res)) {
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_OutWidth, &r.opt_w);
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_OutHeight, &r.opt_h);
        r.min_w = r.max_w = r.opt_w, r.min_h = r.max_h = r.opt_h;   // (older DLSS: no dynamic range given)
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, &r.max_w);
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, &r.max_h);
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, &r.min_w);
        NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, &r.min_h);
    }
    r.ok = NVSDK_NGX_SUCCEED(res) && r.max_w && r.max_h;
    if (r.ok)
        hos_log("rx: DLSS: mode %d at %ux%u: renders at %ux%u, takes %ux%u .. %ux%u", quality, dw, dh, r.opt_w, r.opt_h,
                r.min_w, r.min_h, r.max_w, r.max_h);
    else
        hos_log("rx: DLSS: mode %d at %ux%u: NGX gave no render sizes (%s)", quality, dw, dh, result_text(res));
    return r;
}

// Can a feature of this mode made at (cw, ch) take a render size of (rw, rh)? Its own size always (seen: 800x448 in
// a Quality feature made at 800x448); otherwise the mode's dynamic range. (8 pixels of slack under the minimum: the
// scene is a multiple of 8 high, and 960x536 was taken by a range starting at 960x540 in the last log.)
// (cw, ch) = (0, 0): would a feature made for the dynamic range take it. One made at a size outside the range (for
// exactly that size) takes only that size.
bool takes(const State::Range& r, uint32_t cw, uint32_t ch, uint32_t rw, uint32_t rh) {
    if (rw == cw && rh == ch) return true;
    if (!r.ok || (r.min_w == r.max_w && r.min_h == r.max_h)) return false;
    auto in = [&](uint32_t w, uint32_t h) { return w + 8 >= r.min_w && h + 8 >= r.min_h && w <= r.max_w && h <= r.max_h; };
    return (!cw || in(cw, ch)) && in(rw, rh);
}

// A feature out of use is released some evaluations later (the GPU may still be running a frame that uses it; a
// frame in flight is long done 16 evaluations later)
void release_old(State& s, bool all) {
    auto& g = s.graveyard;
    for (size_t i = 0; i < g.size();) {
        if (all || s.evaluations > g[i].second + 16) {
            NVSDK_NGX_VULKAN_ReleaseFeature(g[i].first);
            g[i] = g.back();
            g.pop_back();
        } else {
            i++;
        }
    }
}

// A feature made at (cw, ch) for this call's output and mode into use: one kept from before, else a new one (in the
// least recently used slot). `made`: a new one or another kept one (its history is not this frame's: reset).
bool create(State& s, VkCommandBuffer cmd, uint32_t cw, uint32_t ch, const Call* c) {
    State::Slot* victim = &s.slots[0];
    for (auto& sl : s.slots) {
        if (sl.h && sl.w == cw && sl.h_ == ch && sl.dw == c->dw && sl.dh == c->dh && sl.quality == c->quality) {
            sl.used = s.evaluations;
            s.feature = sl.h;
            s.fw = cw, s.fh = ch, s.fdw = c->dw, s.fdh = c->dh, s.fquality = c->quality;
            return true;
        }
        if (!sl.h || (victim->h && sl.used < victim->used)) victim = &sl;
    }
    if (victim->h) {
        if (victim->h == s.feature) s.feature = nullptr;
        s.graveyard.push_back({victim->h, s.evaluations});
        *victim = State::Slot{};
    }
    s.feature = nullptr;
    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth = cw;
    cp.Feature.InHeight = ch;
    cp.Feature.InTargetWidth = c->dw;
    cp.Feature.InTargetHeight = c->dh;
    cp.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(c->quality);
    cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                              NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if (const unsigned p = preset_now()) {
        for (auto key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance})
            NVSDK_NGX_Parameter_SetUI(s.params, key, p);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT(cmd, 1, 1, &s.feature, s.params, &cp);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (NVSDK_NGX_FAILED(r)) {
        hos_log("rx: DLSS: the feature was not made (%ux%u -> %ux%u, mode %d): %s", cw, ch, c->dw, c->dh, c->quality,
                result_text(r));
        s.feature = nullptr;
        s.fw = s.fh = 0;
        return false;
    }
    s.fw = cw, s.fh = ch, s.fdw = c->dw, s.fdh = c->dh, s.fquality = c->quality;
    victim->h = s.feature, victim->w = cw, victim->h_ = ch, victim->dw = c->dw, victim->dh = c->dh;
    victim->quality = c->quality, victim->used = s.evaluations;
    s.creations++;
    if (s.creations <= 40 || s.creations % 100 == 0)
        hos_log("rx: DLSS: feature made (#%lld, %.0f ms): %ux%u -> %ux%u, mode %d", (long long)s.creations, ms, cw, ch,
                c->dw, c->dh, c->quality);
    return true;
}

void evaluate(uint64_t cmd_raw, void* user) {
    Call* c = static_cast<Call*>(user);
    State& s = S();
    std::lock_guard lk(s.mu);
    if (s.failed) {
        delete c;
        return;
    }
    VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(static_cast<uintptr_t>(cmd_raw));
    if (const unsigned p = preset_now(); p != s.preset) {   // (another model: every feature made again with it)
        for (auto& sl : s.slots)
            if (sl.h) s.graveyard.push_back({sl.h, s.evaluations}), sl = State::Slot{};
        s.feature = nullptr;
        s.fw = s.fh = 0;
        s.fquality = -1;
        if (s.evaluations) hos_log("rx: DLSS: the model is now %s", p ? preset_name(p) : "NVIDIA's default");
        s.preset = p;
    }
    release_old(s, false);
    // The feature: made once for the mode and output size and kept while the render size stays in what it takes
    // (the game's DRS moving inside the mode's dynamic range: no new feature). Made again (each one is a hitch:
    // NGX builds its network) when the mode or the output changes, or the render size leaves that: at the mode's
    // largest size if the new size is in its dynamic range, else at exactly the render size.
    const State::Range& rg = range(s, c->dw, c->dh, c->quality);
    const bool same = s.feature && s.fdw == c->dw && s.fdh == c->dh && s.fquality == c->quality;
    bool fresh = false;
    if (!same || !takes(rg, s.fw, s.fh, c->rw, c->rh)) {
        uint32_t cw = c->rw, ch = c->rh;
        if (takes(rg, 0, 0, c->rw, c->rh)) {
            cw = std::min(std::max(c->mw, c->rw), rg.max_w);
            ch = std::min(std::max(c->mh, c->rh), rg.max_h);
        }
        if (s.fw == cw && s.fh == ch && same) {
            // (in use already)
        } else if (!create(s, cmd, cw, ch, c)) {
            if (++s.failures_in_a_row >= 30) {
                hos_log("rx: DLSS: 30 failures in a row: DLSS off, FSR 2 from here");
                s.failed = true;
            }
            delete c;
            return;
        }
        fresh = true;
    }
    NVSDK_NGX_VK_DLSS_Eval_Params e{};
    e.Feature.pInColor = &c->color;
    e.Feature.pInOutput = &c->out;
    e.Feature.InSharpness = c->sharpness;
    e.pInDepth = &c->depth;
    e.pInMotionVectors = &c->mv;
    e.pInBiasCurrentColorMask = c->has_mask ? &c->mask : nullptr;
    e.InJitterOffsetX = c->jx;
    e.InJitterOffsetY = c->jy;
    e.InRenderSubrectDimensions.Width = c->rw;
    e.InRenderSubrectDimensions.Height = c->rh;
    e.InReset = (c->reset || fresh) ? 1 : 0;
    e.InMVScaleX = c->mvx;
    e.InMVScaleY = c->mvy;
    e.InFrameTimeDeltaInMsec = c->dt_ms;
    NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, s.feature, s.params, &e);
    if (NVSDK_NGX_FAILED(r) && (s.fw != c->rw || s.fh != c->rh)) {
        // NGX did not take this render size after all: a feature made for exactly it (that one it takes)
        if (s.errors < 40) hos_log("rx: DLSS: %ux%u not taken by the feature made at %ux%u (%s): one made for it", c->rw,
                                   c->rh, s.fw, s.fh, result_text(r));
        if (create(s, cmd, c->rw, c->rh, c)) {
            e.InReset = 1;
            r = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, s.feature, s.params, &e);
        }
    }
    s.evaluations++;
    if (NVSDK_NGX_FAILED(r)) {
        if (s.errors++ < 40 || s.errors % 500 == 0)
            hos_log("rx: DLSS: evaluation failed (%lld so far): %ux%u -> %ux%u, mode %d: %s", (long long)s.errors, c->rw,
                    c->rh, c->dw, c->dh, c->quality, result_text(r));
        if (++s.failures_in_a_row >= 30) {
            hos_log("rx: DLSS: 30 failures in a row: DLSS off, FSR 2 from here");
            s.failed = true;
        }
    } else {
        s.failures_in_a_row = 0;
        if (s.evaluations == 1) hos_log("rx: DLSS: first evaluation: %ux%u -> %ux%u", c->rw, c->rh, c->dw, c->dh);
    }
    delete c;
}

}   // namespace

void dlss_before_instance() {
    State& s = S();
    s.wanted = env_flag("RX_DLSS");
    if (!s.wanted) return;
    NVSDK_NGX_FeatureDiscoveryInfo d = discovery();
    uint32_t n = 0;
    VkExtensionProperties* props = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &n, &props);
    if (NVSDK_NGX_FAILED(r)) {
        hos_log("rx: DLSS: NGX did not say which instance extensions it needs: %s; DLSS off", result_text(r));
        s.wanted = false;
        return;
    }
    add_extensions(n, props, false);
}

bool dlss_wants_vulkan() { return S().wanted; }

void dlss_before_device(WGPUAdapter adapter) {
    State& s = S();
    if (!s.wanted) return;
    WGPUTotkVulkanHandles h{};
    if (!wgpuTotkAdapterVulkanHandles(adapter, &h)) {
        hos_log("rx: DLSS: the adapter is not a Vulkan one: DLSS off");
        s.wanted = false;
        return;
    }
    NVSDK_NGX_FeatureDiscoveryInfo d = discovery();
    uint32_t n = 0;
    VkExtensionProperties* props = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
        reinterpret_cast<VkInstance>(static_cast<uintptr_t>(h.instance)),
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(h.physical_device)), &d, &n, &props);
    if (NVSDK_NGX_FAILED(r)) {
        hos_log("rx: DLSS: NGX did not say which device extensions it needs: %s; DLSS off", result_text(r));
        s.wanted = false;
        return;
    }
    add_extensions(n, props, true);
    s.extensions_asked = true;
}

void dlss_after_device() {
    State& s = S();
    if (!s.wanted || !s.extensions_asked) return;
    if (env_flag("RX_DLSS_NO_NGX")) {   // (checking: the device as DLSS has it, NGX not started: FSR 2)
        hos_log("rx: DLSS: NGX not started (RX_DLSS_NO_NGX): FSR 2");
        return;
    }
    if (!wgpuTotkDeviceVulkanHandles(dev.device, &s.vk)) {
        hos_log("rx: DLSS: no Vulkan device: DLSS off");
        return;
    }
    static std::wstring dir = data_path();
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", dir.c_str(),
        reinterpret_cast<VkInstance>(static_cast<uintptr_t>(s.vk.instance)),
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(s.vk.physical_device)),
        reinterpret_cast<VkDevice>(static_cast<uintptr_t>(s.vk.device)),
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(const_cast<void*>(s.vk.get_instance_proc_addr)),
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(const_cast<void*>(s.vk.get_device_proc_addr)), common_info(),
        NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r)) {
        hos_log("rx: DLSS: NGX did not start: %s; DLSS off", result_text(r));
        return;
    }
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&s.params);
    if (NVSDK_NGX_FAILED(r) || !s.params) {
        hos_log("rx: DLSS: no NGX parameters: %s; DLSS off", result_text(r));
        return;
    }
    int available = 0, needs_driver = 0;
    unsigned major = 0, minor = 0;
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
    NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
    NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
    if (!available) {
        int why = 0;
        NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &why);
        hos_log("rx: DLSS: not available on this PC (init result 0x%08x%s); DLSS off", (unsigned)why,
                needs_driver ? ", needs a newer NVIDIA driver" : "");
        if (needs_driver) hos_log("rx: DLSS: the driver needs to be at least %u.%u", major, minor);
        return;
    }
    s.ready = true;
    hos_log("rx: DLSS: ready (NGX on the Vulkan device; nvngx_dlss.dll from %ls)", dir.c_str());
}

bool dlss_ready() {
    State& s = S();
    return s.ready && !s.failed;
}

void dlss_trim() {
    State& s = S();
    if (!s.ready) return;
    std::lock_guard lk(s.mu);
    int n = 0;
    for (auto& sl : s.slots)
        if (sl.h && sl.h != s.feature) {
            s.graveyard.push_back({sl.h, s.evaluations});   // (released 16 evaluations later: release_old)
            sl = State::Slot{};
            n++;
        }
    if (n) hos_log("rx: DLSS: %d kept feature(s) released (VRAM)", n);
}

bool dlss_record(WGPUCommandEncoder e, const DlssInputs& in) {
    if (!dlss_ready()) return false;
    Call* c = new Call{};
    c->color = resource(in.color, in.color_v, false);
    c->depth = resource(in.depth, in.depth_v, false);
    c->mv = resource(in.mv, in.mv_v, false);
    c->out = resource(in.out, in.out_v, true);
    static const bool mask_on = !getenv("RX_DLSS_MASK") || *getenv("RX_DLSS_MASK") != '0';
    c->has_mask = mask_on && in.mask && in.mask_v;
    if (c->has_mask) c->mask = resource(in.mask, in.mask_v, false);
    if (!c->color.Resource.ImageViewInfo.Image || !c->depth.Resource.ImageViewInfo.Image ||
        !c->mv.Resource.ImageViewInfo.Image || !c->out.Resource.ImageViewInfo.Image) {
        static int lines;
        if (lines++ < 5) hos_log("rx: DLSS: an input has no Vulkan image (not the patched wgpu_native.dll?)");
        delete c;
        return false;
    }
    static const float sx = getenv("RX_DLSS_JX") ? (float)atof(getenv("RX_DLSS_JX")) : 1.0f;
    static const float sy = getenv("RX_DLSS_JY") ? (float)atof(getenv("RX_DLSS_JY")) : 1.0f;
    static const float mx = getenv("RX_DLSS_MVX") ? (float)atof(getenv("RX_DLSS_MVX")) : 1.0f;
    static const float my = getenv("RX_DLSS_MVY") ? (float)atof(getenv("RX_DLSS_MVY")) : 1.0f;
    c->rw = in.rw, c->rh = in.rh, c->mw = in.mw, c->mh = in.mh, c->dw = in.dw, c->dh = in.dh;
    c->jx = in.jx * sx, c->jy = in.jy * sy;
    c->mvx = (float)in.rw * mx, c->mvy = (float)in.rh * my;   // (the motion vectors are in UV: to render pixels)
    c->dt_ms = in.dt_ms;
    c->sharpness = 0.0f;
    c->reset = in.reset ? 1 : 0;
    c->quality = in.quality;
    const WGPUTexture textures[5] = {in.color, in.depth, in.mv, in.out, in.mask};
    const uint32_t states[5] = {kResource, kResource, kResource, kStorageRW, kResource};
    const WGPUTextureView views[5] = {in.color_v, in.depth_v, in.mv_v, in.out_v, in.mask_v};
    const uint32_t n = c->has_mask ? 5 : 4;
    ce_totk_raw(e, textures, states, n, views, n, evaluate, c);
    return true;
}

}   // namespace rx

#else   // (no DLSS in this build)

namespace rx {
void dlss_before_instance() {}
bool dlss_wants_vulkan() { return false; }
void dlss_before_device(WGPUAdapter) {}
void dlss_after_device() {}
bool dlss_ready() { return false; }
bool dlss_record(WGPUCommandEncoder, const DlssInputs&) { return false; }
void dlss_trim() {}
}   // namespace rx

#endif
