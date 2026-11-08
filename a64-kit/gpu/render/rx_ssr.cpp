// Water reflections (F1 > Graphics, the launcher, input.ini water_reflections; live): screen-space reflections on
// TOTK's water, drawn into its lit scene before anything reads it.
//
// TOTK's water at Zora's Domain (frame dump 2026-11-08) mirrors nothing: under a lit pillar it has a glow that keeps
// none of the pillar's shape. Here, after the game's deferred lighting has filled the scene's HDR colour target, one
// full-screen pass traces each water pixel's reflected ray through the depth buffer and mixes in the colour it hits,
// by Fresnel (Schlick, water's F0 0.02: little looking down, much at a glancing angle).
//   - Water: the G-buffer's first target (RGBA8, the object channel) holds 0.2 in red on water (51 of 255; Link and
//     what he holds are green over 0.4, the Depths' gloom blue), Zora's Domain frame dump 2026-11-08: red 51 on all
//     1.31 M pixels of the lake and nowhere else.
//   - The surface: its normal from the depth buffer (exact for the flat lake: world up), mixed with the G-buffer's
//     normal (its fourth target: a view-space normal as two 12-bit octahedral coordinates in RGB, x = R | G.hi << 8,
//     y = G.lo | B << 4, z negated; on that lake it matched the depth's normal to a median dot of 0.993) for the
//     ripples: Natural mixes in a quarter of it, Clear a twelfth.
//   - The ray: in screen space, 32 steps (denser near the start) from the pixel to where the ray leaves the scene or
//     comes too near the camera, perspective-correct depth (1/w), a hit where it passes behind the depth buffer by
//     less than a thickness, refined by 6 halvings. Rays that hit nothing leave the game's water as it was.
//   - When: after the first draw of the game's sun / ambient light (its fragment program 2c55abc5006f254b: in the
//     frame dumps of Zora's Domain, the sky islands and the surface), at the first draw into other targets (the
//     lighting pass has ended).
//   Only what is on screen can be reflected, and only from the front: Link's reflection shows his back, a reflection
//   fades out at the picture's edges, and what the camera does not see (behind it, above the picture) is not there.
// RX_SSR=0/1/2 overrides the setting; RX_SSR_DEBUG=1 paints the water mask magenta, 2 shows the reflections alone;
// RX_SSR_LIGHT_FS=<hex> another lighting program to follow; RX_SSR_STEPS=<n> (8..128).
#include "rx_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" volatile int hos_water_reflections;   // (hos/input_bind.c: Bindings.water_reflections)

namespace rx {

void bump_image_version(Image* im);   // (rx_core.cpp)
void flush_clears_now();              // (rx_core.cpp)
bool fsr2_wanted();               // (rx_fsr2.cpp)
WGPUTexture scene_normal;         // the G-buffer's normal target (a reference held: rx_draw.cpp)

namespace {

const char* kSsrWgsl = R"(
struct U {
  proj: mat4x4<f32>,       // view -> clip
  inv_proj: mat4x4<f32>,   // clip -> view
  size: vec4<f32>,         // the scene: width, height, 1 / width, 1 / height
  tex: vec4<f32>,          // 1 / the colour copy's width, 1 / its height, near plane, longest ray
  lin: vec4<f32>,          // projection [2][2], [2][3]; depth = a * NDC z + b: a, b
  p: vec4<f32>,            // steps, debug, noise offset, ripple
  q: vec4<f32>,            // strength, F0, edge fade, 0
};
@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var dep: texture_depth_2d;
@group(0) @binding(3) var objs: texture_2d<f32>;
@group(0) @binding(4) var nrm: texture_2d<f32>;
@group(0) @binding(5) var<uniform> u: U;

@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  let t = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));
  return vec4<f32>(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);
}
fn ndc_z(d: f32) -> f32 { return (d - u.lin.w) / u.lin.z; }
// the distance in front of the camera (view -z) of a depth buffer value
fn dist_of(d: f32) -> f32 { return u.lin.y / (ndc_z(d) + u.lin.x); }
fn clamp_px(q: vec2<i32>) -> vec2<i32> { return clamp(q, vec2<i32>(0), vec2<i32>(u.size.xy) - 1); }
fn depth_at(q: vec2<i32>) -> f32 { return textureLoad(dep, clamp_px(q), 0); }
fn view_at(px: vec2<f32>, d: f32) -> vec3<f32> {
  let h = u.inv_proj * vec4<f32>(px.x * u.size.z * 2.0 - 1.0, 1.0 - px.y * u.size.w * 2.0, ndc_z(d), 1.0);
  return h.xyz / h.w;
}
// pixel x, y and 1 / w (1 / the distance) of a view-space point
fn to_screen(v: vec3<f32>) -> vec3<f32> {
  let c = u.proj * vec4<f32>(v, 1.0);
  let iw = 1.0 / c.w;
  return vec3<f32>((c.x * iw * 0.5 + 0.5) * u.size.x, (0.5 - c.y * iw * 0.5) * u.size.y, iw);
}
fn gbuf_normal(c: vec4<f32>) -> vec3<f32> {
  let b = vec3<u32>(round(clamp(c.rgb, vec3<f32>(0.0), vec3<f32>(1.0)) * 255.0));
  let x = f32(b.x | ((b.y >> 4u) << 8u)) / 4095.0 * 2.0 - 1.0;
  let y = f32((b.y & 15u) | (b.z << 4u)) / 4095.0 * 2.0 - 1.0;
  let z = 1.0 - abs(x) - abs(y);
  var n = vec3<f32>(x, y, z);
  if (z < 0.0) {
    n = vec3<f32>((1.0 - abs(y)) * select(-1.0, 1.0, x >= 0.0), (1.0 - abs(x)) * select(-1.0, 1.0, y >= 0.0), z);
  }
  return normalize(vec3<f32>(n.x, n.y, -n.z));
}
fn ign(p: vec2<f32>) -> f32 { return fract(52.9829189 * fract(dot(p, vec2<f32>(0.06711056, 0.00583715)))); }

@fragment fn fs(@builtin(position) fp: vec4<f32>) -> @location(0) vec4<f32> {
  let q = vec2<i32>(fp.xy);
  let o = textureLoad(objs, q, 0);
  if (abs(o.r - 0.2) > 0.035 || o.g > 0.3 || o.b > 0.3) { discard; }
  let d = textureLoad(dep, q, 0);
  if (d >= 1.0) { discard; }
  if (u.p.y == 1.0) { return vec4<f32>(1.0, 0.0, 1.0, 0.5); }
  // the surface's normal: from the depth (the nearer neighbour each way), with the G-buffer's ripples
  let p = view_at(fp.xy, d);
  let pl = view_at(fp.xy - vec2<f32>(1.0, 0.0), depth_at(q - vec2<i32>(1, 0)));
  let pr = view_at(fp.xy + vec2<f32>(1.0, 0.0), depth_at(q + vec2<i32>(1, 0)));
  let pu = view_at(fp.xy - vec2<f32>(0.0, 1.0), depth_at(q - vec2<i32>(0, 1)));
  let pd = view_at(fp.xy + vec2<f32>(0.0, 1.0), depth_at(q + vec2<i32>(0, 1)));
  let dx = select(p - pl, pr - p, abs(pr.z - p.z) < abs(p.z - pl.z));
  let dy = select(p - pu, pd - p, abs(pd.z - p.z) < abs(p.z - pu.z));
  let gn = gbuf_normal(textureLoad(nrm, q, 0));
  let c = cross(dy, dx);
  let cl = length(c);
  var ng = gn;
  if (cl > 1e-12) { ng = c / cl; }
  if (dot(ng, -p) < 0.0) { ng = -ng; }
  let n = normalize(mix(ng, gn, u.p.w));
  let v = normalize(p);
  let r = reflect(v, n);
  let cosv = clamp(dot(-v, ng), 0.0, 1.0);
  let fres = u.q.y + (1.0 - u.q.y) * pow(1.0 - cosv, 5.0);
  // the ray in screen space: cut before the near plane, then where it leaves the scene
  var len = u.tex.w;
  if (r.z > 0.0) { len = min(len, (-u.tex.z * 1.5 - p.z) / r.z); }
  if (len <= 0.0) { discard; }
  let s0 = to_screen(p);
  var s1 = to_screen(p + r * len);
  let dl = s1.xy - s0.xy;
  var tc = 1.0;
  if (dl.x > 0.0) { tc = min(tc, (u.size.x - s0.x) / dl.x); } else if (dl.x < 0.0) { tc = min(tc, -s0.x / dl.x); }
  if (dl.y > 0.0) { tc = min(tc, (u.size.y - s0.y) / dl.y); } else if (dl.y < 0.0) { tc = min(tc, -s0.y / dl.y); }
  s1 = s0 + (s1 - s0) * max(tc, 0.0);   // (1 / w is linear in screen space)
  let steps = max(u.p.x, 1.0);
  let jit = fract(ign(fp.xy) + u.p.z);
  var hit = false;
  var t0 = 0.0;
  var t1 = 0.0;
  var prev_t = 0.0;
  var prev_d = 1.0 / s0.z;
  for (var i = 0u; i < u32(steps); i++) {
    var t = (f32(i) + jit) / steps;
    t = t * t;
    let s = s0 + (s1 - s0) * t;
    let rd = 1.0 / s.z;
    if (length(s.xy - s0.xy) > 2.5) {
      let sd = dist_of(depth_at(vec2<i32>(s.xy)));
      let thick = max(0.25, rd * 0.02) + max(rd - prev_d, 0.0);
      if (rd > sd * 1.002 && rd < sd + thick) {
        hit = true;
        t0 = prev_t;
        t1 = t;
        break;
      }
    }
    prev_t = t;
    prev_d = rd;
  }
  if (!hit) { discard; }
  for (var k = 0; k < 6; k++) {
    let tm = (t0 + t1) * 0.5;
    let s = s0 + (s1 - s0) * tm;
    if (1.0 / s.z > dist_of(depth_at(vec2<i32>(s.xy)))) { t1 = tm; } else { t0 = tm; }
  }
  let h = s0 + (s1 - s0) * t1;
  let hu = h.xy * u.size.zw;
  let edge = clamp(min(min(hu.x, 1.0 - hu.x), min(hu.y, 1.0 - hu.y)) * u.q.z, 0.0, 1.0);
  let col = textureSampleLevel(scene, smp, h.xy * u.tex.xy, 0.0).rgb;
  let facing = clamp(1.0 - r.z * 2.0, 0.0, 1.0);
  var w = clamp(fres * edge * facing * u.q.x, 0.0, 1.0);
  if (u.p.y == 2.0) { w = 1.0; }
  return vec4<f32>(col, w);
}
)";

struct Uniforms {
    float proj[16], inv_proj[16], size[4], tex[4], lin[4], p[4], q[4];
};
static_assert(sizeof(Uniforms) == 208, "the WGSL struct U");

constexpr int kRing = 8;   // (uniform buffers and bind groups in flight: recordings are replayed later)

struct State {
    bool failed = false, validated = false;
    WGPUBindGroupLayout layout = nullptr;
    WGPURenderPipeline pipe = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUSampler sampler = nullptr;
    WGPUBuffer ub[kRing] = {};
    WGPUBindGroup groups[kRing] = {};
    WGPUTexture copy = nullptr;   // the lit scene before the reflections (read while the target is drawn)
    WGPUTextureView copy_v = nullptr;
    uint32_t cw = 0, ch = 0;
    WGPUTextureFormat cf = WGPUTextureFormat_Undefined;
    uint64_t runs = 0;
    // the lighting pass of this frame: its targets
    int64_t light_frame = -1, done_frame = -1;
    Image* light_c0 = nullptr;
    Image* light_z = nullptr;
    int64_t last_note = 0;
} st;

int mode() {
    static const char* env = getenv("RX_SSR");
    if (env && *env) return std::clamp(atoi(env), 0, 2);
    return std::clamp((int)hos_water_reflections, 0, 2);
}

bool invert(const double m[16], double out[16]) {   // (row-major; Gauss-Jordan with partial pivoting)
    double a[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) {
        int piv = c;
        for (int r = c + 1; r < 4; r++)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-30) return false;
        if (piv != c)
            for (int k = 0; k < 8; k++) std::swap(a[c][k], a[piv][k]);
        const double s = 1.0 / a[c][c];
        for (int k = 0; k < 8; k++) a[c][k] *= s;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
        }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[r * 4 + c] = a[r][c + 4];
    return true;
}

bool build(WGPUTextureFormat format) {
    if (!st.layout) {
        WGPUBindGroupLayoutEntry e[6]{};
        for (int i = 0; i < 6; i++) e[i].binding = (uint32_t)i, e[i].visibility = WGPUShaderStage_Fragment;
        e[0].texture.sampleType = WGPUTextureSampleType_Float;
        e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        e[2].texture.sampleType = WGPUTextureSampleType_Depth;
        e[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[4].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        e[4].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[5].buffer.type = WGPUBufferBindingType_Uniform;
        e[5].buffer.minBindingSize = sizeof(Uniforms);
        WGPUBindGroupLayoutDescriptor bd{};
        bd.entryCount = 6;
        bd.entries = e;
        st.layout = wgpuDeviceCreateBindGroupLayout(dev.device, &bd);
        WGPUSamplerDescriptor sd{};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.lodMaxClamp = 1;
        sd.maxAnisotropy = 1;
        st.sampler = wgpuDeviceCreateSampler(dev.device, &sd);
        for (WGPUBuffer& b : st.ub) {
            WGPUBufferDescriptor d{};
            d.size = sizeof(Uniforms);
            d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            b = wgpuDeviceCreateBuffer(dev.device, &d);
        }
    }
    if (st.pipe && st.format == format) return true;
    if (st.pipe) wgpuRenderPipelineRelease(st.pipe);
    st.pipe = nullptr;
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = sv(kSsrWgsl);
    WGPUShaderModuleDescriptor md{};
    md.nextInChain = &src.chain;
    md.label = sv("rx water reflections");
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(dev.device, &md);
    WGPUPipelineLayoutDescriptor pd{};
    pd.bindGroupLayoutCount = 1;
    pd.bindGroupLayouts = &st.layout;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(dev.device, &pd);
    WGPUBlendState blend{};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_Zero;
    blend.alpha.dstFactor = WGPUBlendFactor_One;
    WGPUColorTargetState target{};
    target.format = format;
    target.blend = &blend;
    target.writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
    WGPUFragmentState f{};
    f.module = mod;
    f.entryPoint = sv("fs");
    f.targetCount = 1;
    f.targets = &target;
    WGPURenderPipelineDescriptor rd{};
    rd.label = sv("rx water reflections");
    rd.layout = pl;
    rd.vertex.module = mod;
    rd.vertex.entryPoint = sv("vs");
    rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rd.multisample.count = 1;
    rd.multisample.mask = 0xFFFFFFFF;
    rd.fragment = &f;
    st.pipe = wgpuDeviceCreateRenderPipeline(dev.device, &rd);
    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(mod);
    st.format = format;
    return st.pipe != nullptr;
}

WGPUTextureView view_of(WGPUTexture t, WGPUTextureAspect aspect) {
    WGPUTextureViewDescriptor vd{};
    vd.format = wgpuTextureGetFormat(t);
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = aspect;
    return wgpuTextureCreateView(t, &vd);
}

/// The reflections into the lit scene (target: the lighting pass's colour target)
void run(Image* target, int m) {
    const SceneCamera cur = scene_camera;
    auto skip = [](const char* why) {
        log_limited("ssr_skip", 20, "rx: water reflections skipped a frame: %s", why);
    };
    if (cur.frame != frame_number) return skip("no scene camera this frame");
    if (!scene_depth || !scene_mask || !scene_normal || !scene_color) return skip("the G-buffer is not known yet");
    if (!target || target->tex != scene_color) return skip("the lighting drew into another target than the scene's");
    const uint32_t rw = (uint32_t)cur.width, rh = (uint32_t)cur.height;
    const uint32_t tw = target->width, th = target->height;
    if (!rw || !rh || rw > tw || rh > th || wgpuTextureGetWidth(scene_depth) < rw ||
        wgpuTextureGetHeight(scene_depth) < rh || wgpuTextureGetWidth(scene_mask) < rw ||
        wgpuTextureGetWidth(scene_normal) < rw || wgpuTextureGetHeight(scene_normal) < rh)
        return skip("the scene's targets do not match its size");
    if (!st.validated) scope_push();
    if (!build(target->format)) {
        if (!st.validated) scope_pop();
        st.failed = true;
        hos_log("rx: water reflections: the pass did not build: off");
        return;
    }
    if (!st.copy || st.cw != tw || st.ch != th || st.cf != target->format) {
        if (st.copy_v) wgpuTextureViewRelease(st.copy_v);
        if (st.copy) wgpuTextureRelease(st.copy);
        WGPUTextureDescriptor d{};
        d.label = sv("rx water reflections: the lit scene");
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension = WGPUTextureDimension_2D;
        d.size = {tw, th, 1};
        d.format = target->format;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        st.copy = wgpuDeviceCreateTexture(dev.device, &d);
        st.copy_v = st.copy ? wgpuTextureCreateView(st.copy, nullptr) : nullptr;
        st.cw = tw, st.ch = th, st.cf = target->format;
    }
    if (!st.copy_v) {
        if (!st.validated) scope_pop();
        st.failed = true;
        hos_log("rx: water reflections: no texture for the scene's copy: off");
        return;
    }
    // ---- the constants ----
    Uniforms u{};
    {
        double pm[16], ip[16];
        for (int i = 0; i < 16; i++) pm[i] = cur.v[28 + i];   // (the projection, row-major: constants 28..43)
        if (!invert(pm, ip)) {
            if (!st.validated) scope_pop();
            return skip("the projection does not invert");
        }
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                u.proj[c * 4 + r] = (float)pm[r * 4 + c], u.inv_proj[c * 4 + r] = (float)ip[r * 4 + c];
        u.size[0] = (float)rw, u.size[1] = (float)rh, u.size[2] = 1.0f / rw, u.size[3] = 1.0f / rh;
        u.tex[0] = 1.0f / tw, u.tex[1] = 1.0f / th;
        u.tex[2] = cur.v[56] > 0 ? cur.v[56] : 0.1f;   // (near)
        u.tex[3] = 2000.0f;                            // (the longest ray, m: cut where it leaves the picture)
        u.lin[0] = (float)pm[10], u.lin[1] = (float)pm[11];
        u.lin[2] = cur.depth_a != 0 ? cur.depth_a : 1.0f, u.lin[3] = cur.depth_b;
        static const int steps = getenv("RX_SSR_STEPS") ? std::clamp(atoi(getenv("RX_SSR_STEPS")), 8, 128) : 32;
        static const int debug = getenv("RX_SSR_DEBUG") ? atoi(getenv("RX_SSR_DEBUG")) : 0;
        u.p[0] = (float)steps, u.p[1] = (float)debug;
        // (the start's noise moves each frame only under FSR 2 / DLSS, which average it; still without them)
        u.p[2] = fsr2_wanted() ? std::fmod((float)(frame_number % 64) * 0.61803398875f, 1.0f) : 0.0f;
        u.p[3] = m == 2 ? 0.08f : 0.25f;               // (ripples: Clear, Natural)
        u.q[0] = 1.0f, u.q[1] = 0.02f, u.q[2] = 12.0f;
    }
    const int slot = (int)(st.runs % kRing);
    wgpuQueueWriteBuffer(dev.queue, st.ub[slot], 0, &u, sizeof u);
    WGPUTextureView depth_v = view_of(scene_depth, WGPUTextureAspect_DepthOnly);
    WGPUTextureView mask_v = view_of(scene_mask, WGPUTextureAspect_All);
    WGPUTextureView normal_v = view_of(scene_normal, WGPUTextureAspect_All);
    WGPUBindGroupEntry be[6]{};
    for (int i = 0; i < 6; i++) be[i].binding = (uint32_t)i;
    be[0].textureView = st.copy_v;
    be[1].sampler = st.sampler;
    be[2].textureView = depth_v;
    be[3].textureView = mask_v;
    be[4].textureView = normal_v;
    be[5].buffer = st.ub[slot];
    be[5].size = sizeof(Uniforms);
    if (st.groups[slot]) wgpuBindGroupRelease(st.groups[slot]);
    WGPUBindGroupDescriptor bd{};
    bd.layout = st.layout;
    bd.entryCount = 6;
    bd.entries = be;
    st.groups[slot] = wgpuDeviceCreateBindGroup(dev.device, &bd);
    wgpuTextureViewRelease(depth_v);   // (the bind group holds them)
    wgpuTextureViewRelease(mask_v);
    wgpuTextureViewRelease(normal_v);
    WGPUTextureViewDescriptor tv{};
    tv.format = target->format;
    tv.dimension = WGPUTextureViewDimension_2D;
    tv.mipLevelCount = 1;
    tv.arrayLayerCount = 1;
    tv.aspect = WGPUTextureAspect_All;
    WGPUTextureView tview = wgpuTextureCreateView(target->tex, &tv);
    auto encode = [&](WGPUCommandEncoder e, bool raw) {
        WGPUTexelCopyTextureInfo src{}, dst{};
        src.texture = target->tex, src.aspect = WGPUTextureAspect_All;
        dst.texture = st.copy, dst.aspect = WGPUTextureAspect_All;
        const WGPUExtent3D ext{rw, rh, 1};
        WGPURenderPassColorAttachment ca{};
        ca.view = tview;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load;
        ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor pd{};
        pd.colorAttachmentCount = 1;
        pd.colorAttachments = &ca;
        if (raw) {   // (the check below: straight WebGPU on an encoder of its own)
            wgpuCommandEncoderCopyTextureToTexture(e, &src, &dst, &ext);
            WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(e, &pd);
            wgpuRenderPassEncoderSetViewport(rp, 0, 0, (float)rw, (float)rh, 0, 1);
            wgpuRenderPassEncoderSetPipeline(rp, st.pipe);
            wgpuRenderPassEncoderSetBindGroup(rp, 0, st.groups[slot], 0, nullptr);
            wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(rp);
            wgpuRenderPassEncoderRelease(rp);
            return;
        }
        ce_copy_texture_to_texture(e, &src, &dst, &ext);
        WGPURenderPassEncoder rp = ce_begin_render_pass(e, &pd);
        rp_set_viewport(rp, 0, 0, (float)rw, (float)rh, 0, 1);
        rp_set_pipeline(rp, st.pipe);
        rp_set_bind_group(rp, 0, st.groups[slot], 0, nullptr);
        rp_draw(rp, 3, 1, 0, 0);
        rp_end(rp);
    };
    // Once: the whole pass on an encoder of its own (never submitted) under an error scope, so a mistake turns the
    // reflections off instead of the frame's commands being refused every frame
    if (!st.validated) {
        WGPUCommandEncoder te = wgpuDeviceCreateCommandEncoder(dev.device, nullptr);
        encode(te, true);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(te, nullptr);
        if (cb) wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(te);
        const std::string err = scope_pop();
        if (!err.empty()) {
            hos_log("rx: water reflections: the pass does not check out: off. %s", err.c_str());
            wgpuTextureViewRelease(tview);
            st.failed = true;
            return;
        }
        st.validated = true;
        hos_log("rx: water reflections on (%s): scene %ux%u in %ux%u targets, %d steps", m == 2 ? "Clear" : "Natural",
                rw, rh, tw, th, (int)u.p[0]);
    }
    flush_clears_now();   // (a clear still waiting would be folded into the pass after ours)
    end_render_pass("ssr");
    encode(encoder(), false);
    wgpuTextureViewRelease(tview);   // (after the recording took its references)
    bump_image_version(target);   // (copies made of it before are stale)
    st.runs++;
    count("ssr_frames");
}

}  // namespace

/// Each draw (rx_draw.cpp, before it is recorded): notes the game's lighting pass and, at the first draw after it
/// into other targets, draws the reflections into the lit scene.
void ssr_on_draw(const Attachments& at, uint64_t vs_key, uint64_t fs_key) {
    (void)vs_key;
    if (st.failed) return;
    const int m = mode();
    if (!m) return;
    static const uint64_t light_fs =
        getenv("RX_SSR_LIGHT_FS") ? strtoull(getenv("RX_SSR_LIGHT_FS"), nullptr, 16) : 0x2c55abc5006f254bull;
    if (fs_key == light_fs && at.color[0]) {
        if (st.light_frame != frame_number)
            st.light_frame = frame_number, st.light_c0 = at.color[0], st.light_z = at.depth;
        return;
    }
    if (st.light_frame != frame_number) {
        // (a note when the setting is on and no lighting pass was seen for a while: another program lights the scene)
        if (frame_number - std::max(st.light_frame, st.last_note) > 600) {
            st.last_note = frame_number;
            log_limited("ssr_none", 10, "rx: water reflections: no lighting pass seen in 600 frames (frame %lld)",
                        (long long)frame_number);
        }
        return;
    }
    if (st.done_frame == frame_number) return;
    if (at.color[0] == st.light_c0 && at.depth == st.light_z) return;   // (still the lighting pass)
    st.done_frame = frame_number;
    run(st.light_c0, m);
}

}  // namespace rx
