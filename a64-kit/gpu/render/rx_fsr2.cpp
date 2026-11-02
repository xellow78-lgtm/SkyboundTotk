// AMD FidelityFX Super Resolution 2.2 (temporal upscaling) in place of TOTK's own scale-up of its 3D scene.
//
// TOTK draws its scene at a lower resolution (1600x896 docked) into an HDR target, then one full-screen draw scales it
// to the 1920x1080 frame the HUD is drawn over. With RX_FSR2=1 that draw is replaced: FSR 2 builds the 1920x1080 image
// from the scene's colour, its depth, motion vectors and a sub-pixel jitter of the 3D draws, accumulated over frames.
//   - The game renders no motion vectors. They are made here from the depth and the game's camera constants of this
//     frame and the last (scene_camera, rx_draw.cpp): exact for whatever moves with the world, not for what moves by
//     itself (Link, enemies, grass in the wind).
//   - Jitter: fsr2_jitter() gives the 3D draws into the scene's targets (with a depth buffer, at the scene's size) a
//     Halton sub-pixel offset, FSR's own sequence; the camera constants stay unjittered (so are the motion vectors).
//   - The passes are AMD's GLSL converted to WGSL (rx_fsr2_shaders.h); the host side follows ffx_fsr2.cpp.
// RX_FSR2_SHARPNESS=<0..1> (default 0.5), RX_FSR2_HOOK_FS=<fragment program hex> (the scale-up draw; default TOTK
// 1.4.3's), RX_FSR2_JITTER=0 (no jitter, checking), RX_FSR2_JITTER_SIGN=-1 (the other vertical sign).
#include "rx_internal.h"
#include "rx_fsr2_shaders.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace rx {
namespace {
using namespace fsr2gen;

bool env_on(const char* n) { const char* v = getenv(n); return v && *v && *v != '0'; }
bool fsr2_on() { static const bool on = env_on("RX_FSR2"); return on; }

struct Tex {
    WGPUTexture t = nullptr;
    WGPUTextureView v = nullptr;
    WGPUTextureFormat f = WGPUTextureFormat_Undefined;
    uint32_t w = 0, h = 0, mips = 1;
};

Tex make_tex(const char* label, WGPUTextureFormat f, uint32_t w, uint32_t h, uint32_t mips = 1, bool storage = true) {
    Tex t;
    WGPUTextureDescriptor d{};
    d.label = sv(label);
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | (storage ? WGPUTextureUsage_StorageBinding : 0);
    d.dimension = WGPUTextureDimension_2D;
    d.size = {w ? w : 1, h ? h : 1, 1};
    d.format = f;
    d.mipLevelCount = mips;
    d.sampleCount = 1;
    t.t = wgpuDeviceCreateTexture(dev.device, &d);
    t.v = t.t ? wgpuTextureCreateView(t.t, nullptr) : nullptr;
    t.f = f, t.w = d.size.width, t.h = d.size.height, t.mips = mips;
    return t;
}
WGPUTextureView mip_view(const Tex& t, uint32_t level) {
    WGPUTextureViewDescriptor vd{};
    vd.format = t.f;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.baseMipLevel = level;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    return wgpuTextureCreateView(t.t, &vd);
}
void free_tex(Tex& t) {
    if (t.v) wgpuTextureViewRelease(t.v);
    if (t.t) wgpuTextureRelease(t.t);
    t = {};
}
void upload(const Tex& t, const void* data, uint32_t bytes_per_texel) {
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = t.t;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout l{};
    l.bytesPerRow = t.w * bytes_per_texel;
    l.rowsPerImage = t.h;
    const WGPUExtent3D size{t.w, t.h, 1};
    wgpuQueueWriteTexture(dev.queue, &dst, data, (size_t)t.w * t.h * bytes_per_texel, &l, &size);
}
WGPUBuffer make_buffer(uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor d{};
    d.size = (size + 15) & ~15ull;
    d.usage = usage | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(dev.device, &d);
}
uint16_t to_half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t s = (x >> 16) & 0x8000;
    int e = (int)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFF;
    if (e <= 0) return (uint16_t)s;
    if (e >= 31) return (uint16_t)(s | 0x7C00);
    return (uint16_t)(s | (uint32_t)e << 10 | (m + 0x1000) >> 13);
}

// FSR 2's constants (cbFSR2_t: std140, 128 bytes), the luminance pyramid's and RCAS's
struct Constants {
    int32_t render_size[2], max_render_size[2], display_size[2], input_color_dims[2], luma_mip_dims[2];
    int32_t luma_mip_level, frame_index;
    float device_to_view_depth[4], jitter[2], mv_scale[2], downscale[2], mv_jitter_cancel[2];
    float pre_exposure, prev_pre_exposure, tan_half_fov, jitter_sequence_length, delta_time, dyn_res_change,
        view_to_meters;
    float pad;
};
static_assert(sizeof(Constants) == 128, "cbFSR2 layout");
struct SpdConstants {
    uint32_t mips, num_work_groups, work_group_offset[2], render_size[2], pad[2];
};
struct RcasConstants {
    uint32_t config[4];
};
struct MvConstants {
    float inv_cur[16], vp_prev[16], size[4], dz[4];
};

constexpr int kRing = 8;   // (constant buffers and blit groups in flight: recordings are replayed later)

struct State {
    bool init_failed = false, ready = false, validated = false;
    WGPUSampler point = nullptr, linear = nullptr;
    WGPUBindGroupLayout samplers_layout = nullptr;
    struct P {
        WGPUComputePipeline pipe = nullptr;
        WGPUBindGroupLayout layout = nullptr;
    } pass[6];
    WGPUComputePipeline mv_pipe = nullptr;
    WGPUBindGroupLayout mv_layout = nullptr;
    WGPURenderPipeline blit = nullptr;
    WGPUTextureFormat blit_format = WGPUTextureFormat_Undefined;
    WGPUBindGroupLayout blit_layout = nullptr;
    WGPUBindGroup blit_groups[kRing] = {};
    // resources (sizes: the scene's largest render size, the display's)
    uint32_t mw = 0, mh = 0, dw = 0, dh = 0;
    Tex prepared, dil_mv[2], dil_depth, lock[2], lock_luma, new_locks, up[2], lum, luma_hist[2], dil_reactive, mv, out;
    WGPUTextureView lum_mip4 = nullptr, lum_mip5 = nullptr;
    Tex lanczos, bias, def_reactive, def_exposure, auto_exposure, dummy;
    WGPUBuffer rec_depth = nullptr, spd_atomic = nullptr;
    WGPUBuffer cb[kRing] = {}, cb_spd[kRing] = {}, cb_rcas[kRing] = {}, cb_mv[kRing] = {};
    int64_t dispatches = 0;   // (FSR frames: the ping-pong side and the ring slot)
    bool reset = true;
    float pre_exposure = 1;
    // jitter: this frame's (pixels), set at the first jittered draw of a frame
    int64_t jitter_frame = -1;
    int jitter_index = 0, phase_count = 8;
    float jx = 0, jy = 0;
    int64_t last_fsr_frame = -10;   // the frame FSR last ran (jitter only while it runs)
    int64_t last_ns = 0;
} st;

const char* kMvWgsl = R"(
struct M { inv_cur: mat4x4<f32>, vp_prev: mat4x4<f32>, size: vec4<f32>, dz: vec4<f32> };
@group(0) @binding(0) var dep: texture_depth_2d;
@group(0) @binding(1) var<uniform> m: M;
@group(0) @binding(2) var mv: texture_storage_2d<rg16float, write>;
@compute @workgroup_size(8, 8, 1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (f32(id.x) >= m.size.x || f32(id.y) >= m.size.y) { return; }
  let q = vec2<i32>(id.xy);
  // (the pixel's unjittered position: the jitter moved the image by size.zw pixels)
  let uv = (vec2<f32>(id.xy) + 0.5 - m.size.zw) / m.size.xy;
  let d = textureLoad(dep, q, 0);
  let fy = m.dz.w;
  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, fy * (1.0 - uv.y * 2.0), (d - m.dz.y) / m.dz.x, 1.0);
  var w = m.inv_cur * ndc;
  w = w / w.w;
  let c = m.vp_prev * w;
  let pn = c.xy / c.w;
  let puv = vec2<f32>(pn.x * 0.5 + 0.5, 0.5 - fy * pn.y * 0.5);
  textureStore(mv, q, vec4<f32>(puv - uv, 0.0, 0.0));
}
)";
const char* kBlitWgsl = R"(
@group(0) @binding(0) var src: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  let uv = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));
  return vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(textureLoad(src, vec2<i32>(p.xy), 0).rgb, 1.0);
}
)";

WGPUShaderModule module(const std::string& code, const char* label) {
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = sv(code.c_str());
    WGPUShaderModuleDescriptor md{};
    md.nextInChain = &src.chain;
    md.label = sv(label);
    return wgpuDeviceCreateShaderModule(dev.device, &md);
}

WGPUTextureFormat storage_format(const char* f) {
    static const struct { const char* n; WGPUTextureFormat f; } map[] = {
        {"rgba16float", WGPUTextureFormat_RGBA16Float}, {"rg16float", WGPUTextureFormat_RG16Float},
        {"r16float", WGPUTextureFormat_R16Float},       {"rgba8unorm", WGPUTextureFormat_RGBA8Unorm},
        {"rg8unorm", WGPUTextureFormat_RG8Unorm},       {"r8unorm", WGPUTextureFormat_R8Unorm},
        {"rg32float", WGPUTextureFormat_RG32Float},     {"r32float", WGPUTextureFormat_R32Float},
    };
    for (const auto& m : map)
        if (!std::strcmp(m.n, f)) return m.f;
    return WGPUTextureFormat_Undefined;
}
bool unfilterable(const char* name) {   // (depth and 32-bit float textures: read with textureLoad only)
    return !std::strcmp(name, "r_input_depth") || !std::strcmp(name, "r_input_exposure") ||
           !std::strcmp(name, "r_auto_exposure");
}

WGPUComputePipeline compute_pipeline(WGPUShaderModule mod, std::initializer_list<WGPUBindGroupLayout> layouts) {
    WGPUPipelineLayoutDescriptor pd{};
    pd.bindGroupLayoutCount = layouts.size();
    pd.bindGroupLayouts = layouts.begin();
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(dev.device, &pd);
    WGPUComputePipelineDescriptor cd{};
    cd.layout = pl;
    cd.compute.module = mod;
    cd.compute.entryPoint = sv("main");
    WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(dev.device, &cd);
    wgpuPipelineLayoutRelease(pl);
    return p;
}

bool init_pipelines() {
    if (st.point) return true;
    WGPUSamplerDescriptor sd{};
    sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.lodMaxClamp = 32;
    sd.maxAnisotropy = 1;
    st.point = wgpuDeviceCreateSampler(dev.device, &sd);
    sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Linear;
    st.linear = wgpuDeviceCreateSampler(dev.device, &sd);
    {
        WGPUBindGroupLayoutEntry e[2]{};
        for (int i = 0; i < 2; i++) {
            e[i].binding = i;
            e[i].visibility = WGPUShaderStage_Compute;
            e[i].sampler.type = WGPUSamplerBindingType_Filtering;
        }
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 2;
        bd.entries = e;
        st.samplers_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
    }
    for (int p = 0; p < 6; p++) {
        const Pass& ps = passes[p];
        std::string code;
        for (int i = 0; i < ps.nparts; i++) code += ps.parts[i];
        hos_log("rx: FSR 2: pass %s (%zu bytes of WGSL, %d bindings)", ps.name, code.size(), ps.nbindings);
        std::vector<WGPUBindGroupLayoutEntry> e(ps.nbindings);
        for (int i = 0; i < ps.nbindings; i++) {
            const Binding& b = ps.bindings[i];
            e[i] = {};
            e[i].binding = b.binding;
            e[i].visibility = WGPUShaderStage_Compute;
            switch (b.kind) {
            case kTexture:
                e[i].texture.sampleType = unfilterable(b.name) ? WGPUTextureSampleType_UnfilterableFloat : WGPUTextureSampleType_Float;
                e[i].texture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case kStorageTexture:
                e[i].storageTexture.access = b.read_write ? WGPUStorageTextureAccess_ReadWrite : WGPUStorageTextureAccess_WriteOnly;
                e[i].storageTexture.format = storage_format(b.format);
                e[i].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case kUniform: e[i].buffer.type = WGPUBufferBindingType_Uniform; break;
            case kStorageRO: e[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage; break;
            case kStorageRW: e[i].buffer.type = WGPUBufferBindingType_Storage; break;
            }
        }
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = e.size();
        bd.entries = e.data();
        st.pass[p].layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
        scope_push();
        WGPUShaderModule mod = module(code, ps.name);
        st.pass[p].pipe = mod && st.pass[p].layout ? compute_pipeline(mod, {st.samplers_layout, st.pass[p].layout}) : nullptr;
        if (mod) wgpuShaderModuleRelease(mod);
        const std::string err = scope_pop();
        if (!st.pass[p].pipe || !err.empty()) {
            hos_log("rx: FSR 2: pass %s did not build: off. %s", ps.name, err.c_str());
            return false;
        }
    }
    {
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0;
        e[0].visibility = WGPUShaderStage_Compute;
        e[0].texture.sampleType = WGPUTextureSampleType_Depth;
        e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].binding = 1;
        e[1].visibility = WGPUShaderStage_Compute;
        e[1].buffer.type = WGPUBufferBindingType_Uniform;
        e[2].binding = 2;
        e[2].visibility = WGPUShaderStage_Compute;
        e[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        e[2].storageTexture.format = WGPUTextureFormat_RG16Float;
        e[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 3;
        bd.entries = e;
        st.mv_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
        scope_push();
        WGPUShaderModule mod = module(kMvWgsl, "rx FSR 2 motion vectors");
        st.mv_pipe = compute_pipeline(mod, {st.mv_layout});
        wgpuShaderModuleRelease(mod);
        const std::string err = scope_pop();
        if (!err.empty()) {
            hos_log("rx: FSR 2: the motion vector pass did not build: off. %s", err.c_str());
            return false;
        }
    }
    {
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0;
        e.visibility = WGPUShaderStage_Fragment;
        e.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e.texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 1;
        bd.entries = &e;
        st.blit_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
    }
    for (int i = 0; i < kRing; i++) {
        st.cb[i] = make_buffer(sizeof(Constants), WGPUBufferUsage_Uniform);
        st.cb_spd[i] = make_buffer(sizeof(SpdConstants), WGPUBufferUsage_Uniform);
        st.cb_rcas[i] = make_buffer(sizeof(RcasConstants), WGPUBufferUsage_Uniform);
        st.cb_mv[i] = make_buffer(sizeof(MvConstants), WGPUBufferUsage_Uniform);
    }
    // the fixed tables (ffx_fsr2.cpp): Lanczos 2 weights and the maximum upsample bias, as 16-bit floats
    st.lanczos = make_tex("FSR2 lanczos", WGPUTextureFormat_R16Float, 128, 1, 1, false);
    uint16_t lz[128];
    for (int i = 0; i < 128; i++) {
        const double x = 2.0 * i / 127.0, pi = 3.14159265358979;
        const double y = std::fabs(x) < 1e-6 ? 1.0 : (std::sin(pi * x) / (pi * x)) * (std::sin(0.5 * pi * x) / (0.5 * pi * x));
        lz[i] = to_half((float)y);
    }
    upload(st.lanczos, lz, 2);
    static const float bias[256] = {
#include "rx_fsr2_bias.inc"
    };
    st.bias = make_tex("FSR2 maximum bias", WGPUTextureFormat_R16Float, 16, 16, 1, false);
    uint16_t bz[256];
    for (int i = 0; i < 256; i++) bz[i] = to_half(bias[i] / 2.0f);
    upload(st.bias, bz, 2);
    st.def_reactive = make_tex("FSR2 default reactive", WGPUTextureFormat_R8Unorm, 1, 1, 1, false);
    const uint8_t zero8[4] = {};
    upload(st.def_reactive, zero8, 1);
    st.def_exposure = make_tex("FSR2 default exposure", WGPUTextureFormat_RG32Float, 1, 1, 1, false);
    const float zero2[2] = {0, 0};
    upload(st.def_exposure, zero2, 8);
    st.auto_exposure = make_tex("FSR2 auto exposure", WGPUTextureFormat_RG32Float, 1, 1);
    const float ae[2] = {-1.0f, 1e8f};
    upload(st.auto_exposure, ae, 8);
    st.spd_atomic = make_buffer(16, WGPUBufferUsage_Storage);
    st.dummy = make_tex("FSR2 stand-in", WGPUTextureFormat_RGBA16Float, 1, 1, 1, false);
    hos_log("rx: FSR 2 ready (sharpness %.2f)", getenv("RX_FSR2_SHARPNESS") ? atof(getenv("RX_FSR2_SHARPNESS")) : 0.5);
    return true;
}

void free_sized() {
    for (Tex* t : {&st.prepared, &st.dil_mv[0], &st.dil_mv[1], &st.dil_depth, &st.lock[0], &st.lock[1], &st.lock_luma,
                   &st.new_locks, &st.up[0], &st.up[1], &st.lum, &st.luma_hist[0], &st.luma_hist[1], &st.dil_reactive,
                   &st.mv, &st.out})
        free_tex(*t);
    if (st.lum_mip4) wgpuTextureViewRelease(st.lum_mip4), st.lum_mip4 = nullptr;
    if (st.lum_mip5) wgpuTextureViewRelease(st.lum_mip5), st.lum_mip5 = nullptr;
    if (st.rec_depth) wgpuBufferRelease(st.rec_depth), st.rec_depth = nullptr;
}

void ensure_sized(uint32_t mw, uint32_t mh, uint32_t dw, uint32_t dh) {
    if (st.mw == mw && st.mh == mh && st.dw == dw && st.dh == dh && st.out.t) return;
    free_sized();
    st.mw = mw, st.mh = mh, st.dw = dw, st.dh = dh;
    st.prepared = make_tex("FSR2 prepared color", WGPUTextureFormat_RGBA16Float, mw, mh);
    for (int i = 0; i < 2; i++) {
        st.dil_mv[i] = make_tex("FSR2 dilated motion", WGPUTextureFormat_RG16Float, mw, mh);
        st.lock[i] = make_tex("FSR2 lock status", WGPUTextureFormat_RG16Float, dw, dh);
        st.up[i] = make_tex("FSR2 upscaled", WGPUTextureFormat_RGBA16Float, dw, dh);
        st.luma_hist[i] = make_tex("FSR2 luma history", WGPUTextureFormat_RGBA8Unorm, dw, dh);
    }
    st.dil_depth = make_tex("FSR2 dilated depth", WGPUTextureFormat_R16Float, mw, mh);
    st.lock_luma = make_tex("FSR2 lock luma", WGPUTextureFormat_R16Float, mw, mh);
    st.new_locks = make_tex("FSR2 new locks", WGPUTextureFormat_R8Unorm, dw, dh);
    st.dil_reactive = make_tex("FSR2 dilated reactive", WGPUTextureFormat_RG8Unorm, mw, mh);
    st.mv = make_tex("FSR2 motion vectors", WGPUTextureFormat_RG16Float, mw, mh);
    st.out = make_tex("FSR2 output", WGPUTextureFormat_RGBA16Float, dw, dh);
    const uint32_t lw = std::max(1u, mw / 2), lh = std::max(1u, mh / 2);
    uint32_t mips = 1;
    while ((std::max(lw, lh) >> mips) > 0) mips++;
    st.lum = make_tex("FSR2 scene luminance", WGPUTextureFormat_R16Float, lw, lh, mips);
    st.lum_mip4 = mip_view(st.lum, std::min(4u, mips - 1));
    st.lum_mip5 = mip_view(st.lum, std::min(5u, mips - 1));
    st.rec_depth = make_buffer((uint64_t)mw * mh * 4, WGPUBufferUsage_Storage);
    st.reset = true;
    for (WGPUBindGroup& g : st.blit_groups)
        if (g) wgpuBindGroupRelease(g), g = nullptr;
    hos_log("rx: FSR 2: scene up to %ux%u -> %ux%u", mw, mh, dw, dh);
}

bool invert4(const double m[16], double out[16]) {   // (row-major; Gauss-Jordan with partial pivoting)
    double a[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        if (p != c)
            for (int k = 0; k < 8; k++) std::swap(a[p][k], a[c][k]);
        const double inv = 1.0 / a[c][c];
        for (int k = 0; k < 8; k++) a[c][k] *= inv;
        for (int r = 0; r < 4; r++)
            if (r != c) {
                const double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
            }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[r * 4 + c] = a[r][c + 4];
    return true;
}

float halton(int index, int base) {
    float f = 1, r = 0;
    for (int i = index; i > 0; i /= base) {
        f /= (float)base;
        r += f * (float)(i % base);
    }
    return r;
}

WGPUBindGroupEntry tex_entry(uint32_t binding, WGPUTextureView v) {
    WGPUBindGroupEntry e{};
    e.binding = binding;
    e.textureView = v;
    return e;
}
WGPUBindGroupEntry buf_entry(uint32_t binding, WGPUBuffer b, uint64_t size) {
    WGPUBindGroupEntry e{};
    e.binding = binding;
    e.buffer = b;
    e.size = size;
    return e;
}
}  // namespace

extern SceneCamera scene_camera, scene_camera_prev;
extern WGPUTexture scene_depth;
void flush_clears_now();   // (rx_core.cpp)
WGPUTexture scene_color;   // the G-buffer's HDR colour target (a reference held: rx_draw.cpp)

/// The jitter for a draw into a w x h target with a depth buffer: in NDC, added to the vertex position's x and y
/// (times w); false when it gets none (FSR 2 off, or not the scene's size).
bool fsr2_jitter(uint32_t w, uint32_t h, float* ndc_x, float* ndc_y) {
    if (!fsr2_on() || frame_number - st.last_fsr_frame > 2) return false;
    static const bool off = getenv("RX_FSR2_JITTER") && *getenv("RX_FSR2_JITTER") == '0';
    if (off || !st.mw || w != st.mw || h != st.mh) return false;
    if (st.jitter_frame != frame_number) {
        st.jitter_frame = frame_number;
        st.jitter_index++;
        const int idx = st.jitter_index % std::max(1, st.phase_count) + 1;
        st.jx = halton(idx, 2) - 0.5f;
        st.jy = halton(idx, 3) - 0.5f;
    }
    static const float ysign = getenv("RX_FSR2_JITTER_SIGN") ? (float)atof(getenv("RX_FSR2_JITTER_SIGN")) : 1.0f;
    *ndc_x = 2.0f * st.jx / (float)w;
    *ndc_y = -2.0f * st.jy / (float)h * ysign;
    return true;
}

/// The game's scale-up of its scene (the draw about to be recorded): FSR 2 instead, into the same target. True when
/// done (the draw is then skipped); false to let the game's draw run.
bool fsr2_replace_draw(const Attachments& at, uint64_t fs_key, uint32_t vertex_count) {
    if (!fsr2_on() || st.init_failed) return false;
    static const uint64_t hook = getenv("RX_FSR2_HOOK_FS") ? strtoull(getenv("RX_FSR2_HOOK_FS"), nullptr, 16)
                                                          : 0x7102057103341499ull;
    if (fs_key != hook || vertex_count != 3 || !at.color[0] || !at.color[0]->tex) return false;
    const SceneCamera cur = scene_camera, prev = scene_camera_prev;
    if (!scene_color || !scene_depth || cur.frame != frame_number || prev.frame < 0) return false;
    if (!st.ready) {
        st.ready = init_pipelines();
        if (!st.ready) {
            st.init_failed = true;
            return false;
        }
    }
    const uint32_t mw = wgpuTextureGetWidth(scene_color), mh = wgpuTextureGetHeight(scene_color);
    const uint32_t dw = at.width, dh = at.height;
    const uint32_t rw = (uint32_t)cur.width, rh = (uint32_t)cur.height;
    if (!rw || !rh || rw > mw || rh > mh || wgpuTextureGetWidth(scene_depth) < rw || dw < rw) return false;
    ensure_sized(mw, mh, dw, dh);
    if (st.blit_format != at.color[0]->format) {   // (the blit into the game's target, its format)
        if (st.blit) wgpuRenderPipelineRelease(st.blit);
        WGPUShaderModule mod = module(kBlitWgsl, "rx FSR 2 blit");
        WGPUColorTargetState target{};
        target.format = at.color[0]->format;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState f{};
        f.module = mod;
        f.entryPoint = sv("fs");
        f.targetCount = 1;
        f.targets = &target;
        WGPUPipelineLayoutDescriptor pd{};
        pd.bindGroupLayoutCount = 1;
        pd.bindGroupLayouts = &st.blit_layout;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(dev.device, &pd);
        WGPURenderPipelineDescriptor rd{};
        rd.layout = pl;
        rd.vertex.module = mod;
        rd.vertex.entryPoint = sv("vs");
        rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rd.multisample.count = 1;
        rd.multisample.mask = 0xFFFFFFFF;
        rd.fragment = &f;
        st.blit = wgpuDeviceCreateRenderPipeline(dev.device, &rd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(mod);
        st.blit_format = at.color[0]->format;
    }

    const bool odd = st.dispatches & 1;
    const int slot = (int)(st.dispatches % kRing);
    const int64_t now = hos_now_ns();
    const float dt = st.last_ns ? std::min(1.0f, std::max(0.0f, (float)((now - st.last_ns) / 1e9))) : 1.0f / 60;
    st.last_ns = now;
    st.phase_count = std::max(1, (int)(8.0f * std::pow((float)dw / (float)rw, 2.0f)));

    // ---- constants ----
    const float near_ = cur.v[56], far_ = cur.v[57];
    const float q = far_ / (near_ - far_);
    Constants c{};
    c.render_size[0] = (int32_t)rw, c.render_size[1] = (int32_t)rh;
    c.max_render_size[0] = (int32_t)mw, c.max_render_size[1] = (int32_t)mh;
    c.display_size[0] = (int32_t)dw, c.display_size[1] = (int32_t)dh;
    c.input_color_dims[0] = (int32_t)mw, c.input_color_dims[1] = (int32_t)mh;
    c.luma_mip_level = 4;
    c.luma_mip_dims[0] = (int32_t)(mw / 32), c.luma_mip_dims[1] = (int32_t)(mh / 32);
    c.frame_index = st.reset ? 0 : (int32_t)st.dispatches;
    c.device_to_view_depth[0] = -q;   // (ffx_fsr2.cpp, not inverted, not infinite: d = -1 times c, then e)
    c.device_to_view_depth[1] = q * near_;
    c.device_to_view_depth[2] = cur.v[28] != 0 ? 1.0f / cur.v[28] : 1.0f;   // (1 / the projection's x and y scales)
    c.device_to_view_depth[3] = cur.v[33] != 0 ? 1.0f / cur.v[33] : 1.0f;
    const bool jittered = st.jitter_frame == frame_number;
    c.jitter[0] = jittered ? st.jx : 0, c.jitter[1] = jittered ? st.jy : 0;
    c.mv_scale[0] = c.mv_scale[1] = 1.0f;   // (the motion vectors are in UV units already)
    c.downscale[0] = (float)rw / dw, c.downscale[1] = (float)rh / dh;
    c.pre_exposure = 1, c.prev_pre_exposure = 1;
    c.tan_half_fov = c.device_to_view_depth[2];
    c.jitter_sequence_length = (float)st.phase_count;
    c.delta_time = dt;
    c.view_to_meters = 1;
    wgpuQueueWriteBuffer(dev.queue, st.cb[slot], 0, &c, sizeof c);
    SpdConstants spd{};
    {
        const uint32_t gx = (rw - 1) / 64 + 1, gy = (rh - 1) / 64 + 1;
        spd.num_work_groups = gx * gy;
        spd.mips = (uint32_t)std::min(std::floor(std::log2((double)std::max(rw, rh))), 12.0);
        spd.render_size[0] = rw, spd.render_size[1] = rh;
    }
    wgpuQueueWriteBuffer(dev.queue, st.cb_spd[slot], 0, &spd, sizeof spd);
    {
        static const float sharp = getenv("RX_FSR2_SHARPNESS") ? (float)atof(getenv("RX_FSR2_SHARPNESS")) : 0.5f;
        const float s = std::exp2(-((-2.0f * std::min(1.0f, std::max(0.0f, sharp))) + 2.0f));
        RcasConstants rc{};
        std::memcpy(&rc.config[0], &s, 4);
        rc.config[1] = (uint32_t)to_half(s) | (uint32_t)to_half(s) << 16;
        wgpuQueueWriteBuffer(dev.queue, st.cb_rcas[slot], 0, &rc, sizeof rc);
    }
    MvConstants mc{};
    {
        double vc[16], vp[16], ic[16];
        for (int i = 0; i < 16; i++) vc[i] = cur.v[12 + i], vp[i] = prev.v[12 + i];
        if (!invert4(vc, ic)) return false;
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++) mc.inv_cur[k * 4 + r] = (float)ic[r * 4 + k], mc.vp_prev[k * 4 + r] = (float)vp[r * 4 + k];
        mc.size[0] = (float)rw, mc.size[1] = (float)rh, mc.size[2] = c.jitter[0], mc.size[3] = c.jitter[1];
        mc.dz[0] = cur.depth_a != 0 ? cur.depth_a : 1, mc.dz[1] = cur.depth_b, mc.dz[3] = 1;
    }
    wgpuQueueWriteBuffer(dev.queue, st.cb_mv[slot], 0, &mc, sizeof mc);

    // ---- the inputs' views ----
    WGPUTextureViewDescriptor vd{};
    vd.format = wgpuTextureGetFormat(scene_depth);
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView depth_v = wgpuTextureCreateView(scene_depth, &vd);
    WGPUTextureViewDescriptor cv{};
    cv.format = wgpuTextureGetFormat(scene_color);
    cv.dimension = WGPUTextureViewDimension_2D;
    cv.mipLevelCount = 1;
    cv.arrayLayerCount = 1;
    cv.aspect = WGPUTextureAspect_All;
    WGPUTextureView color_v = wgpuTextureCreateView(scene_color, &cv);

    const int cur_i = odd ? 1 : 0, prev_i = odd ? 0 : 1;    // dilated motion vectors: this frame's, the last's
    const int lock_srv = odd ? 1 : 0, lock_uav = odd ? 0 : 1;
    const int up_srv = odd ? 1 : 0, up_uav = odd ? 0 : 1;
    const int luma_srv = odd ? 1 : 0, luma_uav = odd ? 0 : 1;
    auto view_for = [&](const char* n) -> WGPUTextureView {
        struct { const char* n; WGPUTextureView v; } m[] = {
            {"r_input_color_jittered", color_v}, {"r_input_depth", depth_v}, {"r_input_motion_vectors", st.mv.v},
            {"r_input_exposure", st.def_exposure.v}, {"r_auto_exposure", st.auto_exposure.v},
            {"rw_auto_exposure", st.auto_exposure.v}, {"r_reactive_mask", st.def_reactive.v},
            {"r_transparency_and_composition_mask", st.def_reactive.v}, {"r_dilated_reactive_masks", st.dil_reactive.v},
            {"rw_dilated_reactive_masks", st.dil_reactive.v}, {"r_prepared_input_color", st.prepared.v},
            {"rw_prepared_input_color", st.prepared.v}, {"r_dilated_motion_vectors", st.dil_mv[cur_i].v},
            {"rw_dilated_motion_vectors", st.dil_mv[cur_i].v}, {"r_previous_dilated_motion_vectors", st.dil_mv[prev_i].v},
            {"r_dilatedDepth", st.dil_depth.v}, {"rw_dilatedDepth", st.dil_depth.v}, {"r_lock_input_luma", st.lock_luma.v},
            {"rw_lock_input_luma", st.lock_luma.v}, {"rw_new_locks", st.new_locks.v}, {"r_lock_status", st.lock[lock_srv].v},
            {"rw_lock_status", st.lock[lock_uav].v}, {"r_internal_upscaled_color", st.up[up_srv].v},
            {"rw_internal_upscaled_color", st.up[up_uav].v}, {"r_rcas_input", st.up[up_uav].v},
            {"r_luma_history", st.luma_hist[luma_srv].v}, {"rw_luma_history", st.luma_hist[luma_uav].v},
            {"r_lanczos_lut", st.lanczos.v}, {"r_upsample_maximum_bias_lut", st.bias.v}, {"r_imgMips", st.lum.v},
            {"rw_img_mip_shading_change", st.lum_mip4}, {"rw_img_mip_5", st.lum_mip5}, {"rw_upscaled_output", st.out.v},
        };
        for (const auto& e : m)
            if (!std::strcmp(e.n, n)) return e.v;
        return nullptr;
    };

    // ---- the dispatches: the motion vectors, then FSR 2's six passes ----
    struct Job {
        WGPUComputePipeline pipe;
        WGPUBindGroupLayout layout0, layout1;
        std::vector<WGPUBindGroupEntry> e0, e1;
        uint32_t gx, gy;
    };
    std::vector<Job> jobs;
    jobs.push_back({st.mv_pipe, st.mv_layout, nullptr,
                    {tex_entry(0, depth_v), buf_entry(1, st.cb_mv[slot], sizeof(MvConstants)), tex_entry(2, st.mv.v)},
                    {}, (rw + 7) / 8, (rh + 7) / 8});
    std::vector<WGPUBindGroupEntry> samplers(2);
    samplers[0] = {};
    samplers[0].binding = 0;
    samplers[0].sampler = st.point;
    samplers[1] = {};
    samplers[1].binding = 1;
    samplers[1].sampler = st.linear;
    bool ok = true;
    for (int p = 0; p < 6 && ok; p++) {
        const Pass& ps = passes[p];
        std::vector<WGPUBindGroupEntry> ge;
        for (int i = 0; i < ps.nbindings; i++) {
            const Binding& b = ps.bindings[i];
            if (b.kind == kUniform) {
                const bool spd_cb = !std::strcmp(b.name, "cbSPD"), rcas_cb = !std::strcmp(b.name, "cbRCAS");
                ge.push_back(buf_entry(b.binding, spd_cb ? st.cb_spd[slot] : rcas_cb ? st.cb_rcas[slot] : st.cb[slot],
                                       spd_cb ? sizeof(SpdConstants) : rcas_cb ? sizeof(RcasConstants) : sizeof(Constants)));
            } else if (b.kind == kStorageRO || b.kind == kStorageRW) {
                const bool spd = !std::strncmp(b.name, "SpdAtomic", 9);
                ge.push_back(buf_entry(b.binding, spd ? st.spd_atomic : st.rec_depth, spd ? 16 : (uint64_t)mw * mh * 4));
            } else {
                WGPUTextureView v = view_for(b.name);
                if (!v) {
                    log_limited("fsr2_binding", 10, "rx: FSR 2: pass %s: nothing for %s", ps.name, b.name);
                    ok = false;
                    break;
                }
                ge.push_back(tex_entry(b.binding, v));
            }
        }
        if (!ok) break;
        // A texture both read and written in one dispatch is a conflict WebGPU refuses (depth_clip declares the
        // prepared colour it writes as an input too, unused): the read side gets a 1x1 stand-in
        for (int i = 0; i < ps.nbindings; i++) {
            if (ps.bindings[i].kind != kTexture) continue;
            for (WGPUBindGroupEntry& a : ge) {
                if (a.binding != ps.bindings[i].binding) continue;
                for (int k = 0; k < ps.nbindings; k++) {
                    if (ps.bindings[k].kind != kStorageTexture) continue;
                    for (const WGPUBindGroupEntry& w : ge)
                        if (w.binding == ps.bindings[k].binding && w.textureView == a.textureView) a.textureView = st.dummy.v;
                }
            }
        }
        uint32_t gx, gy;
        if (p == 0) gx = (rw - 1) / 64 + 1, gy = (rh - 1) / 64 + 1;                 // luminance pyramid (SPD)
        else if (p <= 3) gx = (rw + 7) / 8, gy = (rh + 7) / 8;                    // reconstruct, depth clip, lock
        else if (p == 4) gx = (dw + 7) / 8, gy = (dh + 7) / 8;                    // accumulate
        else gx = (dw + 15) / 16, gy = (dh + 15) / 16;                            // RCAS
        jobs.push_back({st.pass[p].pipe, st.samplers_layout, st.pass[p].layout, samplers, ge, gx, gy});
    }
    // Once: the whole sequence encoded on an encoder of its own (never submitted) under an error scope, so a
    // mistake (a group that does not fit, a texture read and written at once) turns FSR 2 off and the game's own
    // scale-up runs, instead of the frame's commands being refused every frame
    if (ok && !st.validated) {
        scope_push();
        WGPUCommandEncoder te = wgpuDeviceCreateCommandEncoder(dev.device, nullptr);
        WGPUComputePassEncoder tp = wgpuCommandEncoderBeginComputePass(te, nullptr);
        std::vector<WGPUBindGroup> groups;
        for (const Job& j : jobs) {
            wgpuComputePassEncoderSetPipeline(tp, j.pipe);
            for (int g = 0; g < 2; g++) {
                const auto& en = g ? j.e1 : j.e0;
                if (!(g ? j.layout1 : j.layout0)) continue;
                WGPUBindGroupDescriptor bd{};
                bd.layout = g ? j.layout1 : j.layout0;
                bd.entryCount = en.size();
                bd.entries = en.data();
                groups.push_back(wgpuDeviceCreateBindGroup(dev.device, &bd));
                wgpuComputePassEncoderSetBindGroup(tp, g, groups.back(), 0, nullptr);
            }
            wgpuComputePassEncoderDispatchWorkgroups(tp, j.gx, j.gy, 1);
        }
        wgpuComputePassEncoderEnd(tp);
        wgpuComputePassEncoderRelease(tp);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(te, nullptr);
        if (cb) wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(te);
        for (WGPUBindGroup g : groups)
            if (g) wgpuBindGroupRelease(g);
        const std::string err = scope_pop();
        if (!err.empty()) {
            hos_log("rx: FSR 2: its passes do not check out: off (the game's scale-up instead). %s", err.c_str());
            ok = false;
        } else {
            hos_log("rx: FSR 2: its passes check out");
            st.validated = true;
        }
    }
    wgpuTextureViewRelease(depth_v);
    wgpuTextureViewRelease(color_v);
    if (!ok) {
        st.init_failed = true;
        return false;
    }
    flush_clears_now();   // (a clear of the target still waiting would be folded into the pass after the blit)
    end_render_pass("fsr2");
    WGPUCommandEncoder e = encoder();
    WGPUComputePassEncoder cp = ce_begin_compute_pass(e);
    for (const Job& j : jobs) {
        cp_set_pipeline(cp, j.pipe);
        cp_set_bind_group_desc(cp, 0, j.layout0, j.e0.data(), j.e0.size());
        if (j.layout1) cp_set_bind_group_desc(cp, 1, j.layout1, j.e1.data(), j.e1.size());
        cp_dispatch(cp, j.gx, j.gy, 1);
    }
    cp_end(cp);
    // the result into the game's target, where its scale-up would have drawn
    {
        if (st.blit_groups[slot]) wgpuBindGroupRelease(st.blit_groups[slot]);
        WGPUBindGroupEntry be = tex_entry(0, st.out.v);
        WGPUBindGroupDescriptor bd{};
        bd.layout = st.blit_layout;
        bd.entryCount = 1;
        bd.entries = &be;
        st.blit_groups[slot] = wgpuDeviceCreateBindGroup(dev.device, &bd);
        WGPUTextureViewDescriptor tv{};
        tv.format = at.color[0]->format;
        tv.dimension = WGPUTextureViewDimension_2D;
        tv.mipLevelCount = 1;
        tv.arrayLayerCount = 1;
        tv.aspect = WGPUTextureAspect_All;
        WGPUTextureView target = wgpuTextureCreateView(at.color[0]->tex, &tv);
        WGPURenderPassColorAttachment ca{};
        ca.view = target;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load;
        ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor pd{};
        pd.colorAttachmentCount = 1;
        pd.colorAttachments = &ca;
        WGPURenderPassEncoder rp = ce_begin_render_pass(e, &pd);
        rp_set_viewport(rp, 0, 0, (float)dw, (float)dh, 0, 1);
        rp_set_pipeline(rp, st.blit);
        rp_set_bind_group(rp, 0, st.blit_groups[slot], 0, nullptr);
        rp_draw(rp, 3, 1, 0, 0);
        rp_end(rp);
        wgpuTextureViewRelease(target);
    }
    if (st.dispatches == 0)
        hos_log("rx: FSR 2 on: scene %ux%u (of %ux%u) -> %ux%u, jitter phases %d, near %g far %g", rw, rh, mw, mh, dw,
                dh, st.phase_count, near_, far_);
    st.dispatches++;
    st.reset = false;
    st.last_fsr_frame = frame_number;
    count("fsr2_frames");
    return true;
}

}  // namespace rx
