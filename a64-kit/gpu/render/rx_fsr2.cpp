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

extern "C" volatile float hos_render_scale;   // (hos/totk_drs.c: the 3D resolution setting; 0 the game's own)
extern "C" unsigned hos_out_w, hos_out_h;      // (hos/totk_res.c: the game's output size)
extern "C" volatile int hos_motion_blur;       // (hos/input_bind.c: Bindings.motion_blur)
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
/// motion blur: 0 off, 1 low, 2 medium, 3 high (RX_MOTION_BLUR overrides the setting)
int motion_blur_mode() {
    static const char* env = getenv("RX_MOTION_BLUR");
    if (env && *env) return std::clamp(atoi(env), 0, 3);
    return std::clamp((int)hos_motion_blur, 0, 3);
}
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
    float ov[4];   // (per-object motion: 1 when the velocity target holds this frame's)
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
    uint32_t last_rw = 0, last_rh = 0;   // (the last frame's render size: logged when it changes)
    uint32_t cw = 0, ch = 0;             // the scene's targets' size last frame (draws into them get the jitter)
    Tex prepared, dil_mv[2], dil_depth, lock[2], lock_luma, new_locks, up[2], lum, luma_hist[2], dil_reactive, mv, out,
        reactive, depth_copy;   // (depth_copy: the scene's depth as a colour image, for DLSS)
    Tex color_copy;             // (DLSS's colour input: a copy of the game's scene colour, rx_fsr2 below)
    WGPUTextureView lum_mip4 = nullptr, lum_mip5 = nullptr;
    Tex lanczos, bias, def_reactive, def_exposure, auto_exposure, dummy;
    WGPUBuffer rec_depth = nullptr, spd_atomic = nullptr;
    WGPUBuffer cb[kRing] = {}, cb_spd[kRing] = {}, cb_rcas[kRing] = {}, cb_mv[kRing] = {};
    int64_t dispatches = 0;   // (FSR frames: the ping-pong side and the ring slot)
    bool reset = true;
    bool last_dlss = false;
    float pre_exposure = 1;
    // jitter: this frame's (pixels), set at the first jittered draw of a frame
    int64_t jitter_frame = -1;
    int jitter_index = 0, phase_count = 8;
    float jx = 0, jy = 0;
    int64_t last_fsr_frame = -10;   // the frame FSR last ran (jitter only while it runs)
    int64_t last_ns = 0;
    // motion blur (rx below: the blit with the camera's motion)
    WGPURenderPipeline blur = nullptr;
    WGPUTextureFormat blur_format = WGPUTextureFormat_Undefined;
    WGPUBindGroupLayout blur_layout = nullptr;
    WGPUSampler blur_sampler = nullptr;
    WGPUBuffer cb_blur[kRing] = {};
    int blur_logged = -1;
    // per-object motion (rx_objmv.cpp): where the motion vectors took an object's motion; the fastest of it per tile
    Tex objok, tiles;
    WGPUComputePipeline tile_pipe = nullptr;
    WGPUBindGroupLayout tile_layout = nullptr;
    WGPUBuffer cb_tile[kRing] = {};
    bool tile_checked = false;
    int objmv_logged = -1;
} st;

const char* kMvWgsl = R"(
struct M { inv_cur: mat4x4<f32>, vp_prev: mat4x4<f32>, size: vec4<f32>, dz: vec4<f32>, ov: vec4<f32> };
@group(0) @binding(0) var dep: texture_depth_2d;
@group(0) @binding(1) var<uniform> m: M;
@group(0) @binding(2) var mv: texture_storage_2d<rg16float, write>;
@group(0) @binding(3) var objects: texture_2d<f32>;
@group(0) @binding(4) var reactive: texture_storage_2d<r8unorm, write>;
@group(0) @binding(5) var depth_out: texture_storage_2d<r32float, write>;
@group(0) @binding(6) var objv: texture_2d<f32>;
@group(0) @binding(7) var objok: texture_storage_2d<r8unorm, write>;
@compute @workgroup_size(8, 8, 1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (f32(id.x) >= m.size.x || f32(id.y) >= m.size.y) { return; }
  let q = vec2<i32>(id.xy);
  // (the G-buffer's object channel: Link and what he holds marked over 100 of 255 in green; the camera's motion is
  // not theirs, so FSR keeps less of their history there. The Depths' gloom in blue (255, its red 32): its glowing
  // streaks move by themselves too. With DLSS the gloom was a flat red sheet that flickered, without an upscaler it
  // was right (2026-11-07): taken for its streaks' history averaged, not confirmed yet)
  let o = textureLoad(objects, q, 0);
  textureStore(reactive, q, vec4<f32>(select(0.0, m.dz.z, o.g > 0.4 || o.b > 0.4), 0.0, 0.0, 0.0));
  // (the pixel's unjittered position: the jitter moved the image by size.zw pixels)
  let uv = (vec2<f32>(id.xy) + 0.5 - m.size.zw) / m.size.xy;
  let d = textureLoad(dep, q, 0);
  textureStore(depth_out, q, vec4<f32>(d, 0.0, 0.0, 0.0));   // (DLSS's depth input: a colour image of it)
  let fy = m.dz.w;
  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, fy * (1.0 - uv.y * 2.0), (d - m.dz.y) / m.dz.x, 1.0);
  var w = m.inv_cur * ndc;
  w = w / w.w;
  let c = m.vp_prev * w;
  let pn = c.xy / c.w;
  let puv = vec2<f32>(pn.x * 0.5 + 0.5, 0.5 - fy * pn.y * 0.5);
  // (per-object motion, rx_objmv.cpp: where the velocity pass drew the surface that is there in the end (its 1 - depth
  // that of the depth buffer, within 2 %), that pixel's own motion; a quarter of the picture or more in one frame is
  // taken for a mismatch)
  var v = puv - uv;
  var own = 0.0;
  if (m.ov.x > 0.5 && all(q < vec2<i32>(textureDimensions(objv)))) {
    let o = textureLoad(objv, q, 0);
    let r = 1.0 - d;
    if (o.w > 0.5 && abs(o.z - r) <= r * 0.02 + 1e-6 && dot(o.xy, o.xy) < 0.0625) {
      v = o.xy;
      own = 1.0;
    }
  }
  textureStore(mv, q, vec4<f32>(v, 0.0, 0.0));
  textureStore(objok, q, vec4<f32>(own, 0.0, 0.0, 0.0));
}
)";
// The fastest object motion (rx_objmv.cpp's pixels only) in each 40 x 40 tile of the motion vectors: how far the
// motion blur looks around a pixel for something that sweeps over it
const char* kTileWgsl = R"(
@group(0) @binding(0) var mvt: texture_2d<f32>;
@group(0) @binding(1) var okt: texture_2d<f32>;
@group(0) @binding(2) var tiles: texture_storage_2d<rg16float, write>;
@group(0) @binding(3) var<uniform> t: vec4<f32>;   // the scene's width and height in the motion vectors
var<workgroup> best: array<vec4<f32>, 64>;
@compute @workgroup_size(8, 8, 1) fn main(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) lid: vec3<u32>,
                                          @builtin(local_invocation_index) li: u32) {
  let lim = vec2<i32>(t.xy);
  var b = vec4<f32>(0.0);
  let base = vec2<i32>(wg.xy) * 40 + vec2<i32>(lid.xy) * 5;
  for (var y = 0; y < 5; y++) {
    for (var x = 0; x < 5; x++) {
      let q = base + vec2<i32>(x, y);
      if (q.x >= lim.x || q.y >= lim.y) { continue; }
      if (textureLoad(okt, q, 0).r < 0.5) { continue; }
      let v = textureLoad(mvt, q, 0).xy;
      let l = dot(v * t.xy, v * t.xy);
      if (l > b.x) { b = vec4<f32>(l, v, 0.0); }
    }
  }
  best[li] = b;
  workgroupBarrier();
  for (var s = 32u; s > 0u; s = s >> 1u) {
    if (li < s && best[li + s].x > best[li].x) { best[li] = best[li + s]; }
    workgroupBarrier();
  }
  if (li == 0u) { textureStore(tiles, vec2<i32>(wg.xy), vec4<f32>(best[0].yz, 0.0, 0.0)); }
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

// Motion blur (F1 > Graphics, the launcher, input.ini motion_blur; live): the blit of the upscaled scene into the game's
// target, each pixel averaged along its motion (the motion vectors made above, per scene pixel). Before the HUD, which
// the game draws over that target afterwards. The length: the motion in one frame x the shutter (Low 0.25, Medium 0.5,
// High 1.0), at most max_px; more than a quarter of the picture in one frame (a cut, a teleport): none.
//   - The world: averaged along the camera's motion there (exact for it). What moves by itself without its own motion
//     (the reactive mask without per-object motion: the camera's motion is not theirs) is left sharp, and not smeared
//     into what is behind it.
//   - Link, what he holds, the characters (per-object motion, rx_objmv.cpp: their own motion, where the motion vectors
//     took it): a reconstruction filter after McGuire, Hennessy, Bukowski and Osman, "A Reconstruction Filter for
//     Plausible Motion Blur" (I3D 2012), so a swing smears over what is behind it instead of only inside its own
//     outline. Around each pixel, the fastest object motion of the 3 x 3 tiles of 40 motion vector texels (kTileWgsl);
//     samples along it, each counted when it is in front and its own motion reaches this pixel (it sweeps over it),
//     or behind and this pixel's motion reaches it (seen through this pixel's smear), or both are moving through
//     each other; depth compared softly (3 % of the distance). Their smear reaches 1.5 x the camera's at most (72
//     pixels at 1080p on High), with 12 more samples (the blade's edges came out as stripes with 16).
// RX_OBJ_MV_DEBUG=1: the object motion as colour (red right, green down, brighter faster) instead of the blur.
const char* kBlurWgsl = R"(
struct B { scale: vec4<f32>, p: vec4<f32>, q: vec4<f32> };
// scale: the motion vectors' UV scale (render / their size), the output's size; p: shutter, max, taps, noise;
// q: debug, objects (1: per-object motion this frame), tile size (motion vector texels), objects' longest smear
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var mvt: texture_2d<f32>;
@group(0) @binding(3) var react: texture_2d<f32>;
@group(0) @binding(4) var<uniform> b: B;
@group(0) @binding(5) var okt: texture_2d<f32>;
@group(0) @binding(6) var tiles: texture_2d<f32>;
@group(0) @binding(7) var dep: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  let uv = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));
  return vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
fn scene_px(px: vec2<f32>) -> vec2<i32> {   // (the motion vectors' texel under an output pixel)
  let d = vec2<f32>(textureDimensions(mvt));
  return clamp(vec2<i32>(px / b.scale.zw * b.scale.xy * d), vec2<i32>(0), vec2<i32>(d) - 1);
}
fn inside(s: vec2<f32>) -> bool { return s.x >= 0.0 && s.y >= 0.0 && s.x < b.scale.z && s.y < b.scale.w; }
fn has_own(q: vec2<i32>) -> bool { return b.q.y > 0.5 && textureLoad(okt, q, 0).r > 0.5; }
fn stays(q: vec2<i32>) -> bool { return textureLoad(react, q, 0).r > 0.0 && !has_own(q); }
// (an object's motion vector as now - last in output pixels x the shutter, at most the objects' longest)
fn to_px(mv: vec2<f32>) -> vec2<f32> {
  var v = -mv * b.scale.zw * b.p.x;
  let l = length(v);
  if (l > b.q.w) { v = v * (b.q.w / l); }
  return v;
}
fn own_v(q: vec2<i32>) -> vec2<f32> {   // (an object pixel's motion; 0 for the rest)
  if (!has_own(q)) { return vec2<f32>(0.0); }
  return to_px(textureLoad(mvt, q, 0).xy);
}
fn dist_of(q: vec2<i32>) -> f32 { return 1.0 / max(1.0 - textureLoad(dep, q, 0).r, 1e-7); }   // (distance / near)
fn front(a: f32, c: f32) -> f32 { return clamp(1.0 - (a - c) / max(0.03 * min(a, c), 1e-3), 0.0, 1.0); }   // (a in front of c)
fn cone(d: f32, r: f32) -> f32 { return clamp(1.0 - d / max(r, 1e-3), 0.0, 1.0); }
fn cyl(d: f32, r: f32) -> f32 { return 1.0 - smoothstep(0.95 * r, 1.05 * r + 1e-3, d); }
fn camera_blur(p: vec2<f32>, center: vec3<f32>, q: vec2<i32>, jit: f32) -> vec3<f32> {
  var v = -textureLoad(mvt, q, 0).xy * b.scale.zw;
  if (length(v) > 0.25 * max(b.scale.z, b.scale.w)) { return center; }
  v = v * b.p.x;
  let len = length(v);
  if (len < 0.75) { return center; }
  if (len > b.p.y) { v = v * (b.p.y / len); }
  let n = i32(b.p.z);
  var sum = center;
  var w = 1.0;
  for (var i = 0; i < n; i++) {
    let s = p + v * ((f32(i) + jit) / f32(n) - 0.5);
    if (!inside(s)) { continue; }
    let qs = scene_px(s);
    if (textureLoad(react, qs, 0).r > 0.0) { continue; }   // (nothing of Link and the rest smeared by the camera)
    sum += textureSampleLevel(src, smp, s / b.scale.zw, 0.0).rgb;
    w += 1.0;
  }
  return sum / w;
}
@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
  let center = textureLoad(src, vec2<i32>(p.xy), 0).rgb;
  let q = scene_px(p.xy);
  let jit = fract(52.9829189 * fract(dot(p.xy, vec2<f32>(0.06711056, 0.00583715))) + b.p.w);
  let own = has_own(q);
  var col = center;
  if (!own && textureLoad(react, q, 0).r <= 0.0) { col = camera_blur(p.xy, center, q, jit); }
  if (b.q.y < 0.5) { return vec4<f32>(col, 1.0); }
  // the fastest object motion around (the tile under a point jittered by up to a quarter tile: tiles' edges turn to
  // noise instead of steps; Guertin, Bukowski and McGuire, "A Fast and Stable Feature-Aware Motion Blur Filter", 2014)
  let td = vec2<i32>(textureDimensions(tiles));
  let tj = vec2<f32>(jit, fract(jit * 7.31 + 0.37)) - 0.5;
  let tq = vec2<i32>(floor((vec2<f32>(q) + tj * 0.5 * b.q.z) / b.q.z));
  var vn = vec2<f32>(0.0);
  for (var dy = -1; dy <= 1; dy++) {
    for (var dx = -1; dx <= 1; dx++) {
      let t = clamp(tq + vec2<i32>(dx, dy), vec2<i32>(0), td - 1);
      let v = to_px(textureLoad(tiles, t, 0).xy);
      if (dot(v, v) > dot(vn, vn)) { vn = v; }
    }
  }
  if (b.q.x > 0.5) {   // (RX_OBJ_MV_DEBUG)
    if (own) {
      let v = own_v(q) / max(b.q.w, 1.0);
      return vec4<f32>(clamp(0.5 + v.x, 0.0, 1.0), clamp(0.5 + v.y, 0.0, 1.0), 0.2 + 0.8 * min(length(v), 1.0), 1.0);
    }
    return vec4<f32>(center * select(0.35, 0.6, dot(vn, vn) >= 1.0), 1.0);
  }
  if (dot(vn, vn) < 1.0) { return vec4<f32>(col, 1.0); }
  let vx = own_v(q);
  let rx = 0.5 * length(vx);   // (the smear's reach either side: half the motion)
  let zx = dist_of(q);
  var w = 1.0 / max(rx, 1.0);
  var sum = col * w;
  let n = i32(b.p.z) + 12;
  // (a moving pixel samples along its own motion every other time: its smear keeps its direction where the tile's
  // fastest goes another way, as along a turning blade)
  let along_own = dot(vx, vx) > 2.25;
  for (var i = 0; i < n; i++) {
    let dir = select(vn, vx, along_own && (i & 1) == 1);
    let off = dir * (((f32(i) + jit) / f32(n)) - 0.5);
    let s = p.xy + off;
    if (!inside(s)) { continue; }
    let qs = scene_px(s);
    if (stays(qs)) { continue; }
    let d = length(off);
    let rs = 0.5 * length(own_v(qs));
    let zs = dist_of(qs);
    let ws = front(zs, zx) * cone(d, rs) + front(zx, zs) * cone(d, rx) + cyl(d, rs) * cyl(d, rx) * 2.0;
    sum += textureSampleLevel(src, smp, s / b.scale.zw, 0.0).rgb * ws;
    w += ws;
  }
  return vec4<f32>(sum / w, 1.0);
}
)";
struct BlurConstants {
    float scale[4], p[4], q[4];
};

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
        WGPUBindGroupLayoutEntry e[8]{};
        e[5].binding = 5;
        e[5].visibility = WGPUShaderStage_Compute;
        e[5].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        e[5].storageTexture.format = WGPUTextureFormat_R32Float;
        e[5].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].binding = 3;
        e[3].visibility = WGPUShaderStage_Compute;
        e[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[4].binding = 4;
        e[4].visibility = WGPUShaderStage_Compute;
        e[4].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        e[4].storageTexture.format = WGPUTextureFormat_R8Unorm;
        e[4].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
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
        e[6].binding = 6;   // (per-object motion: the velocity target, and where it was taken)
        e[6].visibility = WGPUShaderStage_Compute;
        e[6].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[6].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[7].binding = 7;
        e[7].visibility = WGPUShaderStage_Compute;
        e[7].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        e[7].storageTexture.format = WGPUTextureFormat_R8Unorm;
        e[7].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 8;
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
    {   // (per-object motion's tiles: the motion vectors, where they are an object's, the tiles, the scene's size)
        WGPUBindGroupLayoutEntry e[4]{};
        for (int i = 0; i < 4; i++) e[i].binding = (uint32_t)i, e[i].visibility = WGPUShaderStage_Compute;
        e[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        e[2].storageTexture.format = WGPUTextureFormat_RG16Float;
        e[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].buffer.type = WGPUBufferBindingType_Uniform;
        e[3].buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 4;
        bd.entries = e;
        st.tile_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
        scope_push();
        WGPUShaderModule mod = module(kTileWgsl, "rx object motion tiles");
        st.tile_pipe = compute_pipeline(mod, {st.tile_layout});
        wgpuShaderModuleRelease(mod);
        const std::string err = scope_pop();
        if (!err.empty() || !st.tile_pipe) {   // (motion blur keeps the camera's, objects stay sharp)
            hos_log("rx: object motion: the tile pass did not build: %s", err.c_str());
            if (st.tile_pipe) wgpuComputePipelineRelease(st.tile_pipe);
            st.tile_pipe = nullptr;
        }
        for (WGPUBuffer& b : st.cb_tile) b = make_buffer(16, WGPUBufferUsage_Uniform);
    }
    {   // (motion blur: the output, a sampler, the motion vectors, the reactive mask, its constants; per-object motion:
        // where the motion vectors are an object's, the tiles, the depth)
        WGPUBindGroupLayoutEntry e[8]{};
        for (int i = 0; i < 8; i++) e[i].binding = (uint32_t)i, e[i].visibility = WGPUShaderStage_Fragment;
        for (int i : {5, 6, 7}) {
            e[i].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            e[i].texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        e[0].texture.sampleType = WGPUTextureSampleType_Float;
        e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        e[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[4].buffer.type = WGPUBufferBindingType_Uniform;
        e[4].buffer.minBindingSize = sizeof(BlurConstants);
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 8;
        bd.entries = e;
        st.blur_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
        WGPUSamplerDescriptor sd{};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.lodMaxClamp = 1;
        sd.maxAnisotropy = 1;
        st.blur_sampler = wgpuDeviceCreateSampler(dev.device, &sd);
        for (WGPUBuffer& b : st.cb_blur) b = make_buffer(sizeof(BlurConstants), WGPUBufferUsage_Uniform);
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

// the latest frame's motion vectors for frame interpolation (scene_motion): FSR 2's / DLSS's own textures
std::mutex motion_mu;
SceneMotion motion;   // (views not referenced here: st's; cleared before they are freed)
uint64_t motion_seq = 0;

void free_sized() {
    {
        std::lock_guard lk(motion_mu);
        motion = SceneMotion{};
    }
    for (Tex* t : {&st.prepared, &st.dil_mv[0], &st.dil_mv[1], &st.dil_depth, &st.lock[0], &st.lock[1], &st.lock_luma,
                   &st.new_locks, &st.up[0], &st.up[1], &st.lum, &st.luma_hist[0], &st.luma_hist[1], &st.dil_reactive,
                   &st.mv, &st.out, &st.reactive, &st.depth_copy, &st.color_copy, &st.objok, &st.tiles})
        free_tex(*t);
    if (st.lum_mip4) wgpuTextureViewRelease(st.lum_mip4), st.lum_mip4 = nullptr;
    if (st.lum_mip5) wgpuTextureViewRelease(st.lum_mip5), st.lum_mip5 = nullptr;
    if (st.rec_depth) wgpuBufferRelease(st.rec_depth), st.rec_depth = nullptr;
}

void ensure_sized(uint32_t mw, uint32_t mh, uint32_t dw, uint32_t dh) {
    // (only ever larger: the game draws each resolution level into targets of its own size, and the level can change
    // every few frames; FSR 2's own resources at the largest render size seen take any size up to it)
    if (st.out.t && st.dw == dw && st.dh == dh) mw = std::max(mw, st.mw), mh = std::max(mh, st.mh);
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
    st.objok = make_tex("rx object motion taken", WGPUTextureFormat_R8Unorm, mw, mh);
    st.tiles = make_tex("rx object motion tiles", WGPUTextureFormat_RG16Float, (mw + 39) / 40, (mh + 39) / 40);
    st.reactive = make_tex("FSR2 reactive", WGPUTextureFormat_R8Unorm, mw, mh);
    st.depth_copy = make_tex("FSR2 depth copy", WGPUTextureFormat_R32Float, mw, mh);
    st.out = make_tex("FSR2 output", WGPUTextureFormat_RGBA16Float, dw, dh);
    const uint32_t lw = std::max(1u, mw / 2), lh = std::max(1u, mh / 2);
    uint32_t mips = 1;
    while ((std::max(lw, lh) >> mips) > 0) mips++;
    st.lum = make_tex("FSR2 scene luminance", WGPUTextureFormat_R16Float, lw, lh, mips);
    st.lum_mip4 = mip_view(st.lum, std::min(4u, mips - 1));
    st.lum_mip5 = mip_view(st.lum, std::min(5u, mips - 1));
    st.rec_depth = make_buffer((uint64_t)mw * mh * 4, WGPUBufferUsage_Storage);
    st.reset = true;
    // (the blit groups stay: recorded passes not yet replayed use them, and rp_set_bind_group does not hold them; the
    // ring replaces each after kRing frames, and a group keeps its own reference to the old output)
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
WGPUTexture scene_mask;    // its first target (RGBA8): green marks Link and what he holds

/// The jitter for a draw into a w x h target with a depth buffer: in NDC, added to the vertex position's x and y
/// (times w); false when it gets none (FSR 2 off, or not the scene's size).
bool fsr2_jitter(uint32_t w, uint32_t h, float* ndc_x, float* ndc_y) {
    if (!fsr2_on() || frame_number - st.last_fsr_frame > 2) return false;
    static const bool off = getenv("RX_FSR2_JITTER") && *getenv("RX_FSR2_JITTER") == '0';
    if (off || !st.cw || w != st.cw || h != st.ch) return false;
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
    // (TOTK's two scale-ups: its levels with EnableSuperResolution use 7102057103341499, the others 7f5070f0c3f67c3e,
    // whose viewport follows the scene view's box: left to it under a fixed 3D resolution, it drew the scene into a
    // corner of the screen)
    static const uint64_t hook = getenv("RX_FSR2_HOOK_FS") ? strtoull(getenv("RX_FSR2_HOOK_FS"), nullptr, 16)
                                                          : 0x7102057103341499ull;
    static const uint64_t hook2 = getenv("RX_FSR2_HOOK_FS") ? hook : 0x7f5070f0c3f67c3eull;
    if ((fs_key != hook && fs_key != hook2) || vertex_count != 3 || !at.color[0] || !at.color[0]->tex) return false;
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
    st.cw = mw, st.ch = mh;
    const uint32_t rw = (uint32_t)cur.width, rh = (uint32_t)cur.height;
    // (only a scale-up: the second shader also copies the scene at its own size, into targets the scene's size,
    // and FSR 2 there made it resize its resources twice a frame: 250,000 times in a 70-minute run, until the GPU
    // ran out of memory; run_quality.log 2026-11-03 20:44)
    // Except DLAA (Native (DLAA) with DLSS: the scene is the output's size and DLSS runs at 1:1 for its
    // anti-aliasing): only DLSS, only a draw of the output's size from a scene of that size, and once a frame (one
    // size and one evaluation a frame). Without this Native (DLAA) never ran DLSS: "DLSS: ready" and no feature after
    // it in run_launcher.log 2026-11-07 05:00.
    const bool dlaa = dw == mw && dh == mh && dw == hos_out_w && dh == hos_out_h && dlss_ready() &&
                      st.last_fsr_frame != frame_number;
    if (dw <= mw && dh <= mh && !dlaa) return false;
    // (and the second shader only into the game's final 1920x1080 image: it also scales 800x448 into 1600x896 on the
    // title screen, and FSR 2 resizing between the two crashed at startup: run_quality.log 2026-11-03 22:08)
    if (fs_key == hook2 && hook2 != hook && (dw < hos_out_w || dh < hos_out_h)) return false;
    if (!rw || !rh || rw > mw || rh > mh || wgpuTextureGetWidth(scene_depth) < rw || dw < rw) {
        static int lines;
        if (lines++ < 50)
            hos_log("rx: FSR 2 skipped a frame: scene %ux%u, its colour %ux%u, depth %u wide, output %ux%u", rw, rh,
                    mw, mh, wgpuTextureGetWidth(scene_depth), dw, dh);
        return false;
    }
    if (rw != st.last_rw || rh != st.last_rh) {
        static int lines;
        if (lines++ < 200) hos_log("rx: FSR 2: rendering at %ux%u -> %ux%u", rw, rh, dw, dh);
        st.last_rw = rw, st.last_rh = rh;
    }
    // (a safety net: an output size that changes several times in 10 s means two different draws are being replaced;
    // then the current output size stays, and the other draws are left to the game, for 10 s)
    if (st.out.t && (dw != st.dw || dh != st.dh)) {
        static int changes;
        static int64_t window;
        const int64_t now = hos_now_ns();
        if (now - window > 10000000000ll) window = now, changes = 0;
        if (++changes > 4) {
            static int lines;
            if (changes == 5 && lines++ < 20)
                hos_log("rx: FSR 2: the output size keeps changing (%ux%u, now %ux%u): staying at %ux%u", st.dw, st.dh, dw,
                        dh, st.dw, st.dh);
            return false;
        }
    }
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
    const int blur_mode = motion_blur_mode();
    if (blur_mode && st.blur_layout && st.blur_format != at.color[0]->format) {   // (the blit with motion blur)
        if (st.blur) wgpuRenderPipelineRelease(st.blur);
        scope_push();
        WGPUShaderModule mod = module(kBlurWgsl, "rx motion blur");
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
        pd.bindGroupLayouts = &st.blur_layout;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(dev.device, &pd);
        WGPURenderPipelineDescriptor rd{};
        rd.label = sv("rx motion blur");
        rd.layout = pl;
        rd.vertex.module = mod;
        rd.vertex.entryPoint = sv("vs");
        rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rd.multisample.count = 1;
        rd.multisample.mask = 0xFFFFFFFF;
        rd.fragment = &f;
        st.blur = wgpuDeviceCreateRenderPipeline(dev.device, &rd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(mod);
        const std::string err = scope_pop();
        if (!err.empty() || !st.blur) {
            hos_log("rx: motion blur: the pass did not build: off. %s", err.c_str());
            if (st.blur) wgpuRenderPipelineRelease(st.blur);
            st.blur = nullptr;
            wgpuBindGroupLayoutRelease(st.blur_layout);
            st.blur_layout = nullptr;   // (not tried again)
        }
        st.blur_format = at.color[0]->format;
    }

    const bool odd = st.dispatches & 1;
    const int slot = (int)(st.dispatches % kRing);
    const int64_t now = hos_now_ns();
    const float dt = st.last_ns ? std::min(1.0f, std::max(0.0f, (float)((now - st.last_ns) / 1e9))) : 1.0f / 60;
    st.last_ns = now;
    st.phase_count = std::max(1, (int)(8.0f * std::pow((float)dw / (float)rw, 2.0f)));

    // DLSS in FSR 2's place (rx_dlss.cpp: the motion vectors, then DLSS), unless it is not there or turned itself
    // off: then FSR 2, from a fresh history (its own was not kept while DLSS ran)
    // (RX_DLSS_SKIP_EVAL=1, checking: DLSS made ready but never evaluated, FSR 2 scales instead)
    static const bool skip_eval = env_flag("RX_DLSS_SKIP_EVAL");
    const bool use_dlss = dlss_ready() && !skip_eval;
    if (use_dlss != st.last_dlss) st.reset = true;
    st.last_dlss = use_dlss;
    // ---- constants ----
    const float near_ = cur.v[56], far_ = cur.v[57];
    const float q = far_ / (near_ - far_);
    Constants c{};
    c.render_size[0] = (int32_t)rw, c.render_size[1] = (int32_t)rh;
    c.max_render_size[0] = (int32_t)st.mw, c.max_render_size[1] = (int32_t)st.mh;   // (its own resources' size)
    c.display_size[0] = (int32_t)dw, c.display_size[1] = (int32_t)dh;
    c.input_color_dims[0] = (int32_t)mw, c.input_color_dims[1] = (int32_t)mh;
    c.luma_mip_level = 4;
    c.luma_mip_dims[0] = (int32_t)(st.mw / 32), c.luma_mip_dims[1] = (int32_t)(st.mh / 32);
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
        static const float reactive = getenv("RX_FSR2_REACTIVE") ? (float)atof(getenv("RX_FSR2_REACTIVE")) : 0.9f;
        mc.dz[0] = cur.depth_a != 0 ? cur.depth_a : 1, mc.dz[1] = cur.depth_b, mc.dz[2] = scene_mask ? reactive : 0, mc.dz[3] = 1;
    }
    // per-object motion (rx_objmv.cpp): its velocity target, when it holds this frame's and is the scene's size
    WGPUTextureView objv = objmv_view();
    if (objv && st.objok.t == nullptr) objv = nullptr;
    mc.ov[0] = objv ? 1.0f : 0.0f;
    if ((int)(objv != nullptr) != st.objmv_logged) {
        log_limited("objmv_on", 20, "rx: object motion %s", objv ? "in the motion vectors" : "not this frame");
        st.objmv_logged = objv != nullptr;
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
    WGPUTextureView mask_v = nullptr;
    if (scene_mask) {
        cv.format = wgpuTextureGetFormat(scene_mask);
        mask_v = wgpuTextureCreateView(scene_mask, &cv);
    }

    const int cur_i = odd ? 1 : 0, prev_i = odd ? 0 : 1;    // dilated motion vectors: this frame's, the last's
    const int lock_srv = odd ? 1 : 0, lock_uav = odd ? 0 : 1;
    const int up_srv = odd ? 1 : 0, up_uav = odd ? 0 : 1;
    const int luma_srv = odd ? 1 : 0, luma_uav = odd ? 0 : 1;
    auto view_for = [&](const char* n) -> WGPUTextureView {
        struct { const char* n; WGPUTextureView v; } m[] = {
            {"r_input_color_jittered", color_v}, {"r_input_depth", depth_v}, {"r_input_motion_vectors", st.mv.v},
            {"r_input_exposure", st.def_exposure.v}, {"r_auto_exposure", st.auto_exposure.v},
            {"rw_auto_exposure", st.auto_exposure.v}, {"r_reactive_mask", st.reactive.v},
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
                    {tex_entry(0, depth_v), buf_entry(1, st.cb_mv[slot], sizeof(MvConstants)), tex_entry(2, st.mv.v),
                     tex_entry(3, mask_v ? mask_v : st.dummy.v), tex_entry(4, st.reactive.v), tex_entry(5, st.depth_copy.v),
                     tex_entry(6, objv ? objv : st.dummy.v), tex_entry(7, st.objok.v)},
                    {}, (rw + 7) / 8, (rh + 7) / 8});
    // (the tiles of object motion, for the motion blur's reach: checked once on an encoder of its own)
    bool tiles = blur_mode && objv && st.tile_pipe && st.blur;
    if (tiles) {
        const float tsz[4] = {(float)rw, (float)rh, 0, 0};
        wgpuQueueWriteBuffer(dev.queue, st.cb_tile[slot], 0, tsz, sizeof tsz);
        const Job tj{st.tile_pipe, st.tile_layout, nullptr,
                     {tex_entry(0, st.mv.v), tex_entry(1, st.objok.v), tex_entry(2, st.tiles.v), buf_entry(3, st.cb_tile[slot], 16)},
                     {}, (rw + 39) / 40, (rh + 39) / 40};
        if (!st.tile_checked) {
            scope_push();
            WGPUCommandEncoder te = wgpuDeviceCreateCommandEncoder(dev.device, nullptr);
            WGPUComputePassEncoder tp = wgpuCommandEncoderBeginComputePass(te, nullptr);
            WGPUBindGroupDescriptor bd{};
            bd.layout = tj.layout0;
            bd.entryCount = tj.e0.size();
            bd.entries = tj.e0.data();
            WGPUBindGroup g = wgpuDeviceCreateBindGroup(dev.device, &bd);
            wgpuComputePassEncoderSetPipeline(tp, tj.pipe);
            wgpuComputePassEncoderSetBindGroup(tp, 0, g, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(tp, 1, 1, 1);
            wgpuComputePassEncoderEnd(tp);
            wgpuComputePassEncoderRelease(tp);
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(te, nullptr);
            if (cb) wgpuCommandBufferRelease(cb);
            wgpuCommandEncoderRelease(te);
            if (g) wgpuBindGroupRelease(g);
            const std::string err = scope_pop();
            st.tile_checked = true;
            if (!err.empty()) {
                hos_log("rx: object motion: the tile pass does not check out: objects stay sharp. %s", err.c_str());
                wgpuComputePipelineRelease(st.tile_pipe);
                st.tile_pipe = nullptr;
                tiles = false;
            }
        }
        if (tiles) jobs.push_back(tj);
    }
    std::vector<WGPUBindGroupEntry> samplers(2);
    samplers[0] = {};
    samplers[0].binding = 0;
    samplers[0].sampler = st.point;
    samplers[1] = {};
    samplers[1].binding = 1;
    samplers[1].sampler = st.linear;
    bool ok = true;
    for (int p = 0; p < 6 && ok && !use_dlss; p++) {
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
                ge.push_back(buf_entry(b.binding, spd ? st.spd_atomic : st.rec_depth, spd ? 16 : (uint64_t)st.mw * st.mh * 4));
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
    if (ok && !st.validated && !use_dlss) {
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
    if (!ok) {
        wgpuTextureViewRelease(depth_v);
        wgpuTextureViewRelease(color_v);
        if (mask_v) wgpuTextureViewRelease(mask_v);
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
    if (use_dlss) {
        // (the mode from the 3D resolution setting: Quality 1.5x, Balanced 1.7x, Performance 2x, Ultra Performance
        // 3x; the game's own up to 1600x896: Quality; the scene at the output's size: DLAA, NGX's
        // NVSDK_NGX_PerfQuality_Value_DLAA = 5)
        const float rs = hos_render_scale;
        DlssInputs in{};
        in.color = scene_color, in.color_v = color_v;
        // DLSS reads a copy of the scene's colour, not the game's target. Given the target itself, the Depths' gloom
        // was drawn wrong with DLSS (a flat red sheet that flickered; with FSR 2, which samples the same target through
        // WebGPU, and without an upscaler it was right; 2026-11-07): NGX works on the raw Vulkan image, and the game
        // draws the gloom's glow into that target next frame. Taken for what NGX leaves on that image, not confirmed.
        // RX_DLSS_COLOR_COPY=0: the target itself, as before.
        static const bool color_copy = !getenv("RX_DLSS_COLOR_COPY") || *getenv("RX_DLSS_COLOR_COPY") != '0';
        if (color_copy) {
            const WGPUTextureFormat cf = wgpuTextureGetFormat(scene_color);
            if (!st.color_copy.t || st.color_copy.f != cf || st.color_copy.w != st.mw || st.color_copy.h != st.mh) {
                free_tex(st.color_copy);
                st.color_copy = make_tex("DLSS colour copy", cf, st.mw, st.mh, 1, false);
            }
            const uint32_t cw = std::min({rw, wgpuTextureGetWidth(scene_color), st.color_copy.w});
            const uint32_t ch = std::min({rh, wgpuTextureGetHeight(scene_color), st.color_copy.h});
            if (st.color_copy.t && cw && ch) {
                WGPUTexelCopyTextureInfo src{}, dst{};
                src.texture = scene_color, src.aspect = WGPUTextureAspect_All;
                dst.texture = st.color_copy.t, dst.aspect = WGPUTextureAspect_All;
                const WGPUExtent3D ext{cw, ch, 1};
                ce_copy_texture_to_texture(e, &src, &dst, &ext);
                in.color = st.color_copy.t, in.color_v = st.color_copy.v;
            }
        }
        in.depth = st.depth_copy.t, in.depth_v = st.depth_copy.v;
        in.mv = st.mv.t, in.mv_v = st.mv.v;
        in.mask = scene_mask ? st.reactive.t : nullptr, in.mask_v = scene_mask ? st.reactive.v : nullptr;
        in.out = st.out.t, in.out_v = st.out.v;
        in.rw = rw, in.rh = rh, in.mw = st.mw, in.mh = st.mh, in.dw = dw, in.dh = dh;
        in.jx = c.jitter[0], in.jy = c.jitter[1];
        in.dt_ms = dt * 1000.0f;
        in.reset = st.reset;
        // (NGX's numbers: 0 Performance, 1 Balanced, 2 Quality, 3 Ultra Performance, 5 DLAA; Quality's 2/3 used to
        // come out as Balanced and Balanced's 1/1.7 as Performance, and NVIDIA's default model follows the mode)
        in.quality = (rw >= dw && rh >= dh) ? 5 : rs <= 0 ? 2 : rs >= 0.62f ? 2 : rs >= 0.55f ? 1 : rs >= 0.42f ? 0 : 3;
        if (!dlss_record(e, in)) {
            static int lines;
            if (lines++ < 5) hos_log("rx: DLSS: not recorded this frame (FSR 2 takes over from the next)");
        }
    }
    wgpuTextureViewRelease(depth_v);   // (after the recording took its references, not before: it uses them)
    wgpuTextureViewRelease(color_v);
    if (mask_v) wgpuTextureViewRelease(mask_v);
    // the result into the game's target, where its scale-up would have drawn
    {
        // (with motion blur: the blit averages each pixel along the camera's motion; not on a history reset)
        const bool blur = blur_mode && st.blur && st.blur_format == at.color[0]->format && !st.reset;
        if (st.blit_groups[slot]) wgpuBindGroupRelease(st.blit_groups[slot]);
        if (blur) {
            static const float kShutter[4] = {0, 0.25f, 0.5f, 1.0f};
            BlurConstants bc{};
            bc.scale[0] = (float)rw / (float)st.mw, bc.scale[1] = (float)rh / (float)st.mh;
            bc.scale[2] = (float)dw, bc.scale[3] = (float)dh;
            bc.p[0] = kShutter[blur_mode];
            bc.p[1] = 48.0f * (float)dh / 1080.0f;   // (the longest blur: 48 pixels at 1080p)
            bc.p[2] = blur_mode == 3 ? 16.0f : 12.0f;
            bc.p[3] = (float)((st.dispatches * 7) % 16) / 16.0f;   // (the taps' offset moves each frame)
            static const bool objmv_debug = env_flag("RX_OBJ_MV_DEBUG");
            bc.q[0] = objmv_debug ? 1.0f : 0.0f;
            bc.q[1] = tiles ? 1.0f : 0.0f;   // (objects: their own motion, the reconstruction)
            bc.q[2] = 40.0f;
            bc.q[3] = 1.5f * bc.p[1];
            wgpuQueueWriteBuffer(dev.queue, st.cb_blur[slot], 0, &bc, sizeof bc);
            WGPUBindGroupEntry be[8] = {tex_entry(0, st.out.v), {}, tex_entry(2, st.mv.v), tex_entry(3, st.reactive.v),
                                        buf_entry(4, st.cb_blur[slot], sizeof(BlurConstants)), tex_entry(5, st.objok.v),
                                        tex_entry(6, st.tiles.v), tex_entry(7, st.depth_copy.v)};
            be[1].binding = 1;
            be[1].sampler = st.blur_sampler;
            WGPUBindGroupDescriptor bd{};
            bd.layout = st.blur_layout;
            bd.entryCount = 8;
            bd.entries = be;
            st.blit_groups[slot] = wgpuDeviceCreateBindGroup(dev.device, &bd);
        } else {
            WGPUBindGroupEntry be = tex_entry(0, st.out.v);
            WGPUBindGroupDescriptor bd{};
            bd.layout = st.blit_layout;
            bd.entryCount = 1;
            bd.entries = &be;
            st.blit_groups[slot] = wgpuDeviceCreateBindGroup(dev.device, &bd);
        }
        if ((int)blur != st.blur_logged) {
            static const char* const kNames[4] = {"off", "Low", "Medium", "High"};
            if (blur || st.blur_logged > 0)
                log_limited("motion_blur", 20, "rx: motion blur %s", blur ? kNames[blur_mode] : "off");
            st.blur_logged = blur;
        }
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
        rp_set_pipeline(rp, blur ? st.blur : st.blit);
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
    {
        std::lock_guard lk(motion_mu);
        motion.mv = st.mv.v, motion.mask = st.reactive.v;
        motion.sx = (float)rw / (float)st.mw, motion.sy = (float)rh / (float)st.mh;
        motion.seq = ++motion_seq;
    }
    count("fsr2_frames");
    return true;
}

/// (the settings menu: whether FSR 2 runs this session, RX_FSR2)
bool fsr2_wanted() { return fsr2_on(); }

SceneMotion scene_motion() {
    std::lock_guard lk(motion_mu);
    SceneMotion m = motion;
    if (m.mv) wgpuTextureViewAddRef(m.mv);
    if (m.mask) wgpuTextureViewAddRef(m.mask);
    return m;
}

}  // namespace rx
