// Per-object motion (motion blur and the motion vectors under FSR 2 / DLSS): how each pixel of Link, what he holds
// and the other characters moved since the last frame, from their vertices.
//
// The camera's motion vectors (rx_fsr2.cpp) come from the depth buffer and the camera, exact for the world but not for
// anything that moves by itself: a sword swing was not blurred at all (its pixels were left out as "reactive"). Image
// matching cannot follow one: between frame dumps 1870 and 1871 (2026-11-08, a slash at 1704x1064) the blade's tip
// went 243 pixels, from (1170, 803) to (981, 650), turning as it went; frame generation's search reaches 32.
// So, as engines with per-object motion blur do, the moving draws are drawn a second time into a velocity target,
// their vertex program run twice: on last frame's constants, then on this frame's.
//   - Which draws: every draw into the scene's G-buffer (the camera capture's targets: 4+ colour targets, the RGBA8
//     object channel first, an RG11B10 one, a depth buffer), its vertex stage's inputs kept each frame (the uniform
//     block, the constant buffers bound as storage, the storage buffers up to 256 KB), matched to last frame's by the
//     draw (programs, index list, its range, its vertex streams, its turn among equal ones). It moved when they differ,
//     leaving out the camera's constants (constant buffer 4, the 2368 bytes scene_camera reads) and the block's first
//     4 rows (rx's viewport transform and FSR's jitter): the camera's own motion is the depth's, already right.
//     In the log TOTK's characters get their bones that way: a storage buffer through constant buffer 0 (0x8e40
//     bytes at a new address each frame for Link's body, another for his clothes), the per-frame constants each at
//     a new address too (the camera alternates between two).
//   - The vertex program's variant (objmv_vs_variant): its body copied with the uniform block, constant and storage
//     buffers renamed to copies bound in group 1, last frame's bytes uploaded there (the block's first 4 rows this
//     frame's, so both runs place vertices the same way but for the motion).
//   - The pixel stage keeps a pixel only where its depth is the depth buffer's (the surface the game drew there:
//     not what is behind, not what an alpha test cut away), and writes last - now in scene UV, as the camera's motion
//     vectors are, with 1 - depth (to check against the final depth: something drawn later in front).
//   - Drawn at the end of each G-buffer pass that had moving draws (the batch's uploads they use are still there).
// RX_OBJ_MV=0: off; =1: on whatever the motion blur setting; RX_OBJ_MV_STATS=1: what it followed, each 5 s.
#include "rx_internal.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" volatile int hos_motion_blur;   // (hos/input_bind.c: Bindings.motion_blur)

namespace rx {

bool fsr2_wanted();   // (rx_fsr2.cpp)

/// The velocity pass's vertex program, from a translated vertex program (the WGSL backend's layout, emit_wgsl.cpp
/// EntryPoint): its body runs twice, first on last frame's constants (shader_main_p: vs_ub, vs_cbufN and vs_ssboN
/// renamed to their _p copies, bound in group 1, and every helper function that reads them copied the same way), then
/// on this frame's; the output is the position as the game's draw rasterizes it and both clip positions (before the
/// viewport transform, x and y signed as the viewport flips them) for the pixel stage to divide. Empty (and `why`)
/// when the program does not have that layout or stores to memory.
std::string objmv_vs_variant(const std::string& src, const char** why) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return std::string();
    };
    auto ident = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    // the renamed bindings: group 0's uniform block, constant buffers and storage buffers
    std::vector<std::string> globals, decls;
    for (size_t at = 0; (at = src.find("@group(0) @binding(", at)) != std::string::npos; at++) {
        if (at && src[at - 1] != '\n') continue;
        const size_t eol = src.find('\n', at);
        const std::string line = src.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
        const size_t var = line.find(" var");
        const size_t colon = var == std::string::npos ? std::string::npos : line.find(':', var);
        if (colon == std::string::npos) continue;
        size_t nb = colon;
        while (nb > 0 && line[nb - 1] == ' ') nb--;
        size_t na = nb;
        while (na > 0 && ident(line[na - 1])) na--;
        const std::string name = line.substr(na, nb - na);
        if (name != "vs_ub" && name.rfind("vs_cbuf", 0) != 0 && name.rfind("vs_ssbo", 0) != 0) continue;
        if (line.find("read_write") != std::string::npos || line.find("atomic") != std::string::npos)
            return fail("stores to memory");
        globals.push_back(name);
        std::string d = line;
        d.replace(0, std::strlen("@group(0)"), "@group(1)");
        d.replace(na, name.size(), name + "_p");
        decls.push_back(d);
    }
    if (globals.empty()) return fail("no uniform block");
    // module-scope functions: name and [start, end) (from "fn" at a line's start to its closing brace's line end)
    struct Fn {
        std::string name;
        size_t start, end;
    };
    std::vector<Fn> fns;
    for (size_t at = 0; (at = src.find("fn ", at)) != std::string::npos;) {
        if (at && src[at - 1] != '\n') {
            at += 3;
            continue;
        }
        size_t n0 = at + 3, n1 = n0;
        while (n1 < src.size() && ident(src[n1])) n1++;
        const size_t open = src.find('{', n1);
        if (open == std::string::npos) return fail("a function without a body");
        int depth = 0;
        size_t i = open;
        for (; i < src.size(); i++) {
            if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
                i = src.find('\n', i);
                if (i == std::string::npos) break;
                continue;
            }
            if (src[i] == '{') depth++;
            else if (src[i] == '}' && --depth == 0) break;
        }
        if (i == std::string::npos || i >= src.size()) return fail("unbalanced braces");
        size_t end = src.find('\n', i);
        end = end == std::string::npos ? src.size() : end + 1;
        fns.push_back({src.substr(n0, n1 - n0), at, end});
        at = end;
    }
    std::unordered_set<std::string> renamed(globals.begin(), globals.end());
    auto mentions = [&](const Fn& f) {   // (a renamed name in the function, other than its own)
        for (size_t i = f.start; i < f.end;) {
            if (!ident(src[i]) || (i > f.start && ident(src[i - 1]))) {
                i++;
                continue;
            }
            size_t j = i;
            while (j < f.end && ident(src[j])) j++;
            const std::string t = src.substr(i, j - i);
            if (t != f.name && renamed.count(t)) return true;
            i = j;
        }
        return false;
    };
    const Fn* main_fn = nullptr;
    const Fn* body_fn = nullptr;
    std::vector<const Fn*> copied;   // (the helpers that read them, directly or through another such helper)
    for (bool grew = true; grew;) {
        grew = false;
        for (const Fn& f : fns) {
            if (f.name == "main") { main_fn = &f; continue; }
            if (f.name == "shader_main") { body_fn = &f; continue; }
            if (renamed.count(f.name) || !mentions(f)) continue;
            renamed.insert(f.name);
            copied.push_back(&f);
            grew = true;
        }
    }
    if (!main_fn || !body_fn) return fail("no shader_main / main");
    auto rename = [&](size_t a, size_t b) {
        std::string out;
        out.reserve(b - a + 256);
        for (size_t i = a; i < b;) {
            if (!ident(src[i]) || (i > a && ident(src[i - 1]))) {
                out += src[i++];
                continue;
            }
            size_t j = i;
            while (j < b && ident(src[j])) j++;
            const std::string t = src.substr(i, j - i);
            out += t;
            if (renamed.count(t) || t == "shader_main") out += "_p";
            i = j;
        }
        return out;
    };
    // the entry point: both bodies, the clip positions out; the program's own outputs dropped (the velocity pass's
    // pixel stage reads none of them)
    std::string out = src;
    const std::string head = "@vertex\nfn main(input: VsIn) -> VsOut {\n";
    const std::string run = "  gl_Position = vec4<f32>(0.0f, 0.0f, 0.0f, 1.0f);\n  shader_main();\n  var output: VsOut;\n";
    const std::string ret = "  return output;\n}\n";
    const size_t h = out.find(head);
    if (h == std::string::npos || out.find(run, h) == std::string::npos || out.rfind(ret) == std::string::npos ||
        out.rfind(ret) < h)
        return fail("not the expected entry point");
    out.replace(h, head.size(),
                "struct RxVelOut {\n  @builtin(position) position: vec4<f32>,\n  @location(0) rx_c: vec4<f32>,\n"
                "  @location(1) rx_p: vec4<f32>,\n};\n@vertex\nfn main(input: VsIn) -> RxVelOut {\n");
    const size_t r0 = out.find(run, h);
    out.replace(r0, run.size(),
                "  gl_Position = vec4<f32>(0.0f, 0.0f, 0.0f, 1.0f);\n  shader_main_p();\n  let rx_prev = gl_Position;\n"
                "  gl_Position = vec4<f32>(0.0f, 0.0f, 0.0f, 1.0f);\n  shader_main();\n  var output: RxVelOut;\n");
    for (size_t at = r0; (at = out.find("  output.o", at)) != std::string::npos;) {
        const size_t eol = out.find('\n', at);
        if (eol == std::string::npos) break;
        if (std::isdigit((unsigned char)out[at + 10])) out.erase(at, eol + 1 - at);
        else at = eol;
    }
    const size_t r = out.rfind(ret);
    out.replace(r, ret.size(),
                "  let rx_s = vec4<f32>(select(-1.0, 1.0, t.x >= 0.0), select(-1.0, 1.0, t.z >= 0.0), 1.0, 1.0);\n"
                "  output.rx_c = p * rx_s;\n  output.rx_p = rx_prev * rx_s;\n" + ret);
    out += "// ---- the velocity pass: last frame's constants ----\n";
    for (const std::string& d : decls) out += d + "\n";
    for (const Fn* f : copied) out += rename(f->start, f->end);
    out += rename(body_fn->start, body_fn->end);
    return out;
}

namespace {

// The velocity pass's pixel stage: the pixel's depth against the depth buffer's (relative to the distance: for a
// standard depth range far beyond near, 1 - depth goes as near / distance, so 0.4 % of it is 0.4 % of the distance);
// last - now in scene UV, 1 - depth, 1 (written).
const char* kVelFsWgsl = R"(
@group(2) @binding(0) var rx_depth: texture_depth_2d;
@fragment fn main(@builtin(position) fp: vec4<f32>, @location(0) c: vec4<f32>, @location(1) p: vec4<f32>) -> @location(0) vec4<f32> {
  let zs = textureLoad(rx_depth, vec2<i32>(fp.xy), 0);
  if (abs(fp.z - zs) > (1.0 - zs) * 0.004 + 1e-7) { discard; }
  if (p.w <= 1e-5 || c.w <= 1e-5) { discard; }
  let v = (p.xy / p.w - c.xy / c.w) * vec2<f32>(0.5, -0.5);
  return vec4<f32>(v, 1.0 - fp.z, 1.0);
}
)";

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

struct Snap {
    std::vector<ObjMvBinding> bind;
    std::vector<uint8_t> ub;
    uint32_t cam_row = 0, cam_rows = 0;
};

struct VelDraw {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup g0 = nullptr, g1 = nullptr;
    std::vector<uint32_t> off0, off1;
    std::vector<ObjMvVertexBuffer> vertex;
    WGPUBuffer index = nullptr;
    WGPUIndexFormat index_format = WGPUIndexFormat_Undefined;
    bool indexed = false;
    uint32_t count = 0, instances = 1, first = 0, base_instance = 0;
    int32_t base_vertex = 0;
    uint32_t scissor[4]{};
    Image* depth = nullptr;
    uint32_t w = 0, h = 0;
};

/// A followed draw: this frame's and last frame's inputs, alternating by the frame's parity (kept from frame to frame:
/// nothing allocated per draw once its vectors have grown)
struct Entry {
    Snap s[2];
    int64_t f[2] = {-9, -9};
};

/// Per vertex program: how many of its draws changed each uniform block row (and each constant buffer bound as
/// storage) this frame, and the rows that changed in most of them last frame. Those change for the whole scene (the
/// camera's or the time's constants: in the first test 85 % of the scene's draws "moved" by their bytes, 512 drawn
/// again each frame, 15-30 fps); a draw moved when it changes something else (bones, its own transform).
constexpr uint32_t kRows = 256, kSlots = kRows + 16;
struct VsRows {
    int64_t frame = -1;
    uint32_t draws = 0, last_draws = 0;
    uint16_t changed[kSlots]{};
    std::bitset<kSlots> global;
};

struct State {
    int64_t frame = -1;
    std::unordered_map<uint64_t, Entry> draws;
    std::unordered_map<uint64_t, VsRows> rows;
    std::unordered_map<uint64_t, uint32_t> turns;   // (equal draws this frame: their turn)
    std::unordered_map<uint64_t, Bytes> ranges;     // (guest ranges read this frame)
    std::vector<VelDraw> queue;
    // last frame's bytes uploaded in this batch (draws of one character share its bones)
    std::unordered_map<const void*, uint64_t> uploaded;
    int64_t uploaded_batch = -1;
    WGPUTexture vel = nullptr;
    WGPUTextureView vel_v = nullptr;
    uint32_t vw = 0, vh = 0;
    int64_t vel_frame = -1;   // (the frame whose motion it holds)
    std::unordered_map<uint64_t, std::unique_ptr<ShaderModule>> variants;   // (by the vertex text's hash; null: none)
    std::unordered_map<WGPURenderPipeline, bool> checked;   // (velocity pipelines whose first draw was checked: ok)
    ShaderModule fs;
    WGPUBindGroupLayout fs_layout = nullptr;
    bool failed = false;
    // (RX_OBJ_MV_STATS)
    int64_t n_draws = 0, n_new = 0, n_moved = 0, n_queued = 0, n_no_pipe = 0, n_no_room = 0, n_mismatch = 0, n_passes = 0,
            n_capped = 0, n_bones = 0, n_scene = 0, n_matched = 0;
    int frame_queued = 0;   // (this frame's velocity draws: at most RX_OBJ_MV_MAX, 256)
    int64_t groups_made = 0;
    int64_t stats_t0 = 0;
} st;

int env_mode() {
    static const int m = getenv("RX_OBJ_MV") && *getenv("RX_OBJ_MV") ? atoi(getenv("RX_OBJ_MV")) : -1;
    return m;
}

void new_frame() {
    if (st.frame == frame_number) return;
    st.frame = frame_number;
    st.turns.clear();
    if (frame_number % 600 == 0)   // (draws not seen for 2 s let go)
        for (auto it = st.draws.begin(); it != st.draws.end();)
            it = std::max(it->second.f[0], it->second.f[1]) < frame_number - 120 ? st.draws.erase(it) : std::next(it);
    st.ranges.clear();
    st.uploaded.clear();   // (last frame's bytes are gone: an address could come back)
    st.uploaded_batch = -1;
    st.frame_queued = 0;
}

uint64_t align_up(uint64_t v, uint64_t a) { return a ? (v + a - 1) / a * a : v; }

void stats() {
    static const bool on = getenv("RX_OBJ_MV_STATS") && *getenv("RX_OBJ_MV_STATS") != '0';
    const int64_t now = hos_now_ns();
    if (!st.stats_t0) st.stats_t0 = now;
    if (now - st.stats_t0 < 5000000000ll) return;
    if (on || st.n_queued)
        log_limited("objmv_stats", on ? 100000 : 30,
                    "rx: object motion in the last %.1f s: %lld scene draws followed (%lld matched to last frame's), "
                    "%lld without a last frame, %lld "
                    "moved (%lld by their bones; %lld changed only what the whole scene changes, not drawn), %lld redrawn "
                    "in %lld passes (%lld waiting for a pipeline, %lld no room, %lld changed shape, %lld over the "
                    "frame's limit)",
                    (now - st.stats_t0) / 1e9, (long long)st.n_draws, (long long)st.n_matched, (long long)st.n_new,
                    (long long)st.n_moved,
                    (long long)st.n_bones, (long long)st.n_scene, (long long)st.n_queued, (long long)st.n_passes,
                    (long long)st.n_no_pipe, (long long)st.n_no_room, (long long)st.n_mismatch, (long long)st.n_capped);
    st.n_draws = st.n_new = st.n_moved = st.n_queued = st.n_no_pipe = st.n_no_room = st.n_mismatch = st.n_passes = 0;
    st.n_capped = st.n_bones = st.n_scene = st.n_matched = 0;
    st.stats_t0 = now;
}

/// The velocity target, at least the scene targets' size: only ever larger (made again when they grow). Made for
/// each size, it was made twice a frame in the first test (a pass into half-size targets each frame), each old one
/// held by its retired view until the next trim: VRAM over 7.9 GB and the game out of video memory.
bool ensure_target(uint32_t w, uint32_t h) {
    if (st.vel && st.vw >= w && st.vh >= h) return st.vel_v != nullptr;
    w = std::max(w, st.vw), h = std::max(h, st.vh);
    if (st.vel_v) retire_view(st.vel_v);
    if (st.vel) wgpuTextureRelease(st.vel);
    WGPUTextureDescriptor td{};
    td.label = sv("rx object motion");
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {w, h, 1};
    td.format = WGPUTextureFormat_RGBA16Float;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    st.vel = wgpuDeviceCreateTexture(dev.device, &td);
    st.vel_v = st.vel ? wgpuTextureCreateView(st.vel, nullptr) : nullptr;
    st.vw = w, st.vh = h;
    st.vel_frame = -1;
    hos_log("rx: object motion: velocity target %ux%u", w, h);
    return st.vel_v != nullptr;
}

/// The pixel stage's group: the depth buffer's depth
WGPUBindGroup depth_group(Image* im) {
    if (!im || !im->tex) return nullptr;
    WGPUTextureViewDescriptor vd{};
    vd.format = im->format == WGPUTextureFormat_Depth32FloatStencil8 ? WGPUTextureFormat_Depth32Float
                : im->format == WGPUTextureFormat_Depth24PlusStencil8 ? WGPUTextureFormat_Depth24Plus
                                                                       : im->format;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView v = wgpuTextureCreateView(im->tex, &vd);
    if (!v) return nullptr;
    WGPUBindGroupEntry e{};
    e.binding = 0;
    e.textureView = v;
    WGPUBindGroupDescriptor bd{};
    bd.layout = objmv_fs_layout();
    bd.entryCount = 1;
    bd.entries = &e;
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(dev.device, &bd);
    wgpuTextureViewRelease(v);   // (the group holds it)
    return g;
}

/// A velocity draw into a render pass (rp_* or, raw, straight WebGPU)
void record(WGPURenderPassEncoder rp, const VelDraw& v, WGPUBindGroup g2, bool raw = false) {
    if (raw) {
        wgpuRenderPassEncoderSetPipeline(rp, v.pipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, v.g0, v.off0.size(), v.off0.data());
        wgpuRenderPassEncoderSetBindGroup(rp, 1, v.g1, v.off1.size(), v.off1.data());
        wgpuRenderPassEncoderSetBindGroup(rp, 2, g2, 0, nullptr);
        for (uint32_t i = 0; i < v.vertex.size(); i++)
            if (v.vertex[i].buffer)
                wgpuRenderPassEncoderSetVertexBuffer(rp, i, v.vertex[i].buffer, v.vertex[i].offset, v.vertex[i].size);
        wgpuRenderPassEncoderSetScissorRect(rp, v.scissor[0], v.scissor[1], v.scissor[2], v.scissor[3]);
        if (v.indexed) {
            wgpuRenderPassEncoderSetIndexBuffer(rp, v.index, v.index_format, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(rp, v.count, v.instances, v.first, v.base_vertex, v.base_instance);
        } else {
            wgpuRenderPassEncoderDraw(rp, v.count, v.instances, v.first, v.base_instance);
        }
        return;
    }
    rp_set_pipeline(rp, v.pipeline);
    rp_set_bind_group(rp, 0, v.g0, v.off0.size(), v.off0.data());
    rp_set_bind_group(rp, 1, v.g1, v.off1.size(), v.off1.data());
    rp_set_bind_group(rp, 2, g2, 0, nullptr);
    for (uint32_t i = 0; i < v.vertex.size(); i++)
        if (v.vertex[i].buffer) rp_set_vertex_buffer(rp, i, v.vertex[i].buffer, v.vertex[i].offset, v.vertex[i].size);
    rp_set_scissor(rp, v.scissor[0], v.scissor[1], v.scissor[2], v.scissor[3]);
    if (v.indexed) {
        rp_set_index_buffer(rp, v.index, v.index_format, 0, WGPU_WHOLE_SIZE);
        rp_draw_indexed(rp, v.count, v.instances, v.first, v.base_vertex, v.base_instance);
    } else {
        rp_draw(rp, v.count, v.instances, v.first, v.base_instance);
    }
}

/// A pipeline's first draw on an encoder of its own (never submitted) under an error scope: false (and the pipeline
/// is not used again) when WebGPU refuses it, instead of the batch it would be recorded in
bool check_draw(const VelDraw& v) {
    WGPUBindGroup g2 = depth_group(v.depth);
    if (!g2) return false;
    scope_push();
    WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev.device, nullptr);
    WGPURenderPassColorAttachment ca{};
    ca.view = st.vel_v;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pd{};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &ca;
    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(e, &pd);
    wgpuRenderPassEncoderSetViewport(rp, 0, 0, (float)v.w, (float)v.h, 0, 1);
    record(rp, v, g2, true);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, nullptr);
    if (cb) wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(e);
    wgpuBindGroupRelease(g2);
    const std::string err = scope_pop();
    if (!err.empty()) {
        log_limited("objmv_check", 20, "rx: object motion: a velocity pipeline's draw does not check out (not used): %s",
                    err.c_str());
        return false;
    }
    static int ok_lines;
    if (ok_lines++ < 5) hos_log("rx: object motion: a velocity pipeline checks out");
    return true;
}

}  // namespace

/// Whether draws without bones are followed too (RX_OBJ_MV_RIGID=1). Not by default: in the profile of 2026-11-08
/// 20:33 the render thread was the frame's limit (its draws 15-21 ms of a 22 ms frame, the game's main thread waiting
/// 2-7 ms a frame for it), objmv_moved its 4th hottest function, ~400 draws a frame still "moved" by rows the
/// per-program filter did not catch and ~220 a frame drawn again. Draws with bones are a few dozen.
bool objmv_rigid() {
    static const bool on = getenv("RX_OBJ_MV_RIGID") && *getenv("RX_OBJ_MV_RIGID") != '0';
    return on;
}

bool objmv_on() {
    if (st.failed) return false;
    const int m = env_mode();
    if (m == 0) return false;
    if (m >= 1) return true;
    static const char* blur_env = getenv("RX_MOTION_BLUR");
    const int blur = blur_env && *blur_env ? atoi(blur_env) : (int)hos_motion_blur;
    return blur > 0 && fsr2_wanted();
}

Bytes objmv_bytes(uint64_t va, uint64_t size) {
    new_frame();
    const uint64_t key = va * 0x9E3779B97F4A7C15ull ^ size;
    auto it = st.ranges.find(key);
    if (it != st.ranges.end()) return it->second;
    auto v = std::make_shared<std::vector<uint8_t>>((size_t)size);
    if (!gpu_read(va, v->data(), size)) v.reset();
    st.ranges.emplace(key, v);
    return v;
}

ShaderModule* objmv_vs_module(const ShaderModule& vs) {
    const uint64_t key = vs.text_hash ? vs.text_hash : (uint64_t)(uintptr_t)&vs;
    auto it = st.variants.find(key);
    if (it != st.variants.end()) return it->second.get();
    std::unique_ptr<ShaderModule>& slot = st.variants[key];
    const std::string& text = vs.source();
    if (text.empty()) {
        log_limited("objmv_no_text", 10, "rx: object motion: vertex program %016llx: its text is gone",
                    (unsigned long long)vs.key);
        return nullptr;
    }
    const char* why = "";
    std::string v = objmv_vs_variant(text, &why);
    if (v.empty()) {
        log_limited("objmv_no_variant", 20, "rx: object motion: vertex program %016llx has no variant: %s",
                    (unsigned long long)vs.key, why);
        return nullptr;
    }
    slot = std::make_unique<ShaderModule>();
    slot->wgsl = std::move(v);
    slot->info = vs.info;
    slot->what = "velocity vertex";
    slot->key = vs.key;
    return slot.get();
}

ShaderModule* objmv_fs_module() {
    if (st.fs.wgsl.empty()) {
        st.fs.wgsl = kVelFsWgsl;
        st.fs.what = "velocity pixel";
        st.fs.key = 0x0b1e00f5ull;
    }
    return &st.fs;
}

WGPUBindGroupLayout objmv_fs_layout() {
    if (!st.fs_layout) {
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0;
        e.visibility = WGPUShaderStage_Fragment;
        e.texture.sampleType = WGPUTextureSampleType_Depth;
        e.texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor d{};
        d.entryCount = 1;
        d.entries = &e;
        st.fs_layout = wgpuDeviceCreateBindGroupLayout(dev.device, &d);
    }
    return st.fs_layout;
}

bool objmv_moved(uint64_t identity, uint64_t vs_key, ObjMvCapture& cap, uint64_t* turn_identity) {
    new_frame();
    stats();
    const uint64_t id = identity ^ (uint64_t)(st.turns[identity]++) * 0xD6E8FEB86659FD93ull;
    *turn_identity = id;
    st.n_draws++;
    Entry& e = st.draws[id];
    const int slot = (int)(frame_number & 1);
    Snap& s = e.s[slot];
    s.bind.swap(cap.bind);   // (the vectors trade places: their capacity is kept)
    s.ub.swap(cap.ub);
    s.cam_row = cap.cam_row, s.cam_rows = cap.cam_rows;
    e.f[slot] = frame_number;
    cap.clear();
    if (e.f[slot ^ 1] != frame_number - 1) {   // (none last frame, or after a gap: not 2 frames' motion)
        st.n_new++;
        return false;
    }
    const Snap& p = e.s[slot ^ 1];
    if (p.bind.size() != s.bind.size() || p.ub.size() != s.ub.size()) {
        st.n_mismatch++;
        return false;
    }
    st.n_matched++;
    // what changed: storage buffers (bones), the block's rows from 4 and the constant buffers bound as storage (the
    // camera's left out)
    bool bones = false;
    uint16_t changed[kSlots];   // (the slots that changed)
    uint32_t nchanged = 0;
    for (size_t i = 0; i < s.bind.size(); i++) {
        const ObjMvBinding &a = s.bind[i], &b = p.bind[i];
        if (a.kind != b.kind || a.dyn != b.dyn) {
            st.n_mismatch++;
            return false;
        }
        if (a.kind < 2 || a.camera || a.bytes == b.bytes) continue;
        if (!a.bytes || !b.bytes) {   // (unread: not comparable, not drawn)
            st.n_mismatch++;
            return false;
        }
        const bool same =
            a.bytes->size() == b.bytes->size() && !std::memcmp(a.bytes->data(), b.bytes->data(), a.bytes->size());
        if (same) continue;
        if (a.kind == 3) bones = true;
        else if (i < 16) changed[nchanged++] = (uint16_t)(kRows + i);
    }
    const size_t n = s.ub.size() / 16;
    for (size_t row = 4; row < n; row++) {
        if (row >= s.cam_row && row < (size_t)s.cam_row + s.cam_rows) continue;
        if (row < kRows && std::memcmp(s.ub.data() + row * 16, p.ub.data() + row * 16, 16)) changed[nchanged++] = (uint16_t)row;
    }
    if (!objmv_rigid()) {   // (bones only)
        if (!bones) return false;
        st.n_moved++, st.n_bones++;
        return true;
    }
    // this program's draws this frame (for next frame's "whole scene" rows)
    VsRows& r = st.rows[vs_key];
    if (r.frame != frame_number) {
        r.global.reset();
        if (r.frame == frame_number - 1 && r.draws >= 4)
            for (uint32_t k = 0; k < kSlots; k++)
                if (r.changed[k] * 2 >= r.draws) r.global.set(k);
        r.last_draws = r.frame == frame_number - 1 ? r.draws : 0;
        r.draws = 0;
        std::memset(r.changed, 0, sizeof r.changed);
        r.frame = frame_number;
    }
    r.draws++;
    static const bool all = getenv("RX_OBJ_MV_ALL") && *getenv("RX_OBJ_MV_ALL") != '0';
    bool own = bones;
    for (uint32_t k = 0; k < nchanged; k++) {
        if (r.changed[changed[k]] < 0xFFFF) r.changed[changed[k]]++;
        own |= all || !r.global[changed[k]];
    }
    if (!own) {
        if (nchanged) st.n_scene++;
        return false;
    }
    st.n_moved++;
    if (bones) st.n_bones++;
    return true;
}

void objmv_queue(uint64_t id, const ObjMvDraw& d) {
    if (!d.pipeline) {
        st.n_no_pipe++;
        return;
    }
    static const int max_draws = getenv("RX_OBJ_MV_MAX") ? atoi(getenv("RX_OBJ_MV_MAX")) : 256;
    if (st.frame_queued >= max_draws) {   // (a frame where everything changes: not the whole scene twice)
        st.n_capped++;
        return;
    }
    auto ei = st.draws.find(id);
    if (ei == st.draws.end()) return;
    const int slot = (int)(frame_number & 1);
    if (ei->second.f[slot] != frame_number || ei->second.f[slot ^ 1] != frame_number - 1) return;
    const Snap &s = ei->second.s[slot], &p = ei->second.s[slot ^ 1];
    if (d.entries->size() != s.bind.size()) {
        st.n_mismatch++;
        return;
    }
    if (st.uploaded_batch != batch_number()) st.uploaded.clear(), st.uploaded_batch = batch_number();
    std::vector<WGPUBindGroupEntry> e1 = *d.entries;
    std::vector<uint32_t> off1 = *d.offsets;
    bool room = true;
    for (size_t i = 0; i < s.bind.size() && room; i++) {
        const ObjMvBinding& b = p.bind[i];
        if (b.kind == 0) continue;
        uint64_t off = UINT64_MAX, size = 0;
        if (b.kind == 1) {   // (last frame's block, its first 4 rows this frame's)
            size = p.ub.size();
            off = stream_ro.alloc(size, dev.limits.minUniformBufferOffsetAlignment);
            if (off == UINT64_MAX) {
                room = false;
                break;
            }
            std::memcpy(stream_ro.at(off), p.ub.data(), size);
            std::memcpy(stream_ro.at(off), s.ub.data(), std::min<size_t>(64, size));
        } else {
            if (!b.bytes) {
                st.n_mismatch++;
                return;
            }
            size = align_up(b.bytes->size(), 4);
            auto u = st.uploaded.find(b.bytes.get());
            if (u != st.uploaded.end()) {
                off = u->second;
            } else {
                off = stream_ro.alloc(size, dev.limits.minStorageBufferOffsetAlignment);
                if (off == UINT64_MAX) {
                    room = false;
                    break;
                }
                std::memcpy(stream_ro.at(off), b.bytes->data(), b.bytes->size());
                if (size > b.bytes->size()) std::memset(stream_ro.at(off) + b.bytes->size(), 0, size - b.bytes->size());
                st.uploaded.emplace(b.bytes.get(), off);
            }
        }
        WGPUBindGroupEntry& e = e1[i];
        e.buffer = stream_ro.buffer;
        e.size = size;
        if (b.dyn >= 0 && (size_t)b.dyn < off1.size()) e.offset = 0, off1[b.dyn] = (uint32_t)off;
        else e.offset = off;
    }
    if (!room) {   // (no room left in this batch's uploads: not drawn, not flushed for it)
        st.n_no_room++;
        return;
    }
    WGPUBindGroupDescriptor bd{};
    bd.layout = d.layout0;
    bd.entryCount = e1.size();
    bd.entries = e1.data();
    VelDraw v;
    // (an invalid group would fail the whole batch it is recorded in: the first 500 made under an error scope; the
    // rest are made the same way)
    const bool scoped = st.groups_made++ < 500;
    if (scoped) scope_push();
    v.g1 = wgpuDeviceCreateBindGroup(dev.device, &bd);
    const std::string err = scoped ? scope_pop() : std::string();
    if (!err.empty() || !v.g1) {
        log_limited("objmv_group", 10, "rx: object motion: last frame's group did not take: %s", err.c_str());
        if (v.g1) wgpuBindGroupRelease(v.g1);
        st.n_mismatch++;
        return;
    }
    v.pipeline = d.pipeline;
    v.g0 = d.g0;
    v.off0 = *d.offsets;
    v.off1 = std::move(off1);
    v.vertex = d.vertex;
    v.index = d.index;
    v.index_format = d.index_format;
    v.indexed = d.indexed;
    v.count = d.count, v.instances = d.instances, v.first = d.first, v.base_instance = d.base_instance;
    v.base_vertex = d.base_vertex;
    std::memcpy(v.scissor, d.scissor, sizeof v.scissor);
    v.depth = d.depth;
    v.w = d.w, v.h = d.h;
    // each pipeline's first draw, once: the draw on an encoder of its own (never submitted) under an error scope
    auto checked = st.checked.find(d.pipeline);
    if (checked == st.checked.end()) {
        const bool ok = ensure_target(v.w, v.h) && check_draw(v);
        checked = st.checked.emplace(d.pipeline, ok).first;
    }
    if (!checked->second) {
        wgpuBindGroupRelease(v.g1);
        st.n_mismatch++;
        return;
    }
    wgpuBindGroupAddRef(v.g0);
    for (const ObjMvVertexBuffer& vb : v.vertex)
        if (vb.buffer) wgpuBufferAddRef(vb.buffer);
    if (v.index) wgpuBufferAddRef(v.index);
    st.queue.push_back(std::move(v));
    st.n_queued++;
    st.frame_queued++;
}

void objmv_pass_end() {
    if (st.queue.empty()) return;
    std::vector<VelDraw> q;
    q.swap(st.queue);
    const uint32_t w = q.front().w, h = q.front().h;
    std::vector<std::pair<Image*, WGPUBindGroup>> depth_groups;
    auto group2 = [&](Image* im) -> WGPUBindGroup {
        for (auto& g : depth_groups)
            if (g.first == im) return g.second;
        WGPUBindGroup g = depth_group(im);
        depth_groups.push_back({im, g});
        return g;
    };
    if (ensure_target(w, h)) {
        WGPURenderPassColorAttachment ca{};
        ca.view = st.vel_v;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = st.vel_frame == frame_number ? WGPULoadOp_Load : WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {0, 0, 0, 0};
        WGPURenderPassDescriptor pd{};
        pd.colorAttachmentCount = 1;
        pd.colorAttachments = &ca;
        WGPURenderPassEncoder rp = ce_begin_render_pass(encoder(), &pd);
        rp_set_viewport(rp, 0, 0, (float)w, (float)h, 0, 1);
        for (const VelDraw& v : q) {
            WGPUBindGroup g2 = group2(v.depth);
            if (v.w != w || v.h != h || !g2) continue;
            record(rp, v, g2);
        }
        rp_end(rp);
        st.vel_frame = frame_number;
        st.n_passes++;
        count("objmv_passes");
    }
    for (VelDraw& v : q) {   // (after the batch's commands ran)
        release_bind_group_later(v.g0);
        release_bind_group_later(v.g1);
        for (const ObjMvVertexBuffer& vb : v.vertex)
            if (vb.buffer) release_buffer_later(vb.buffer);
        if (v.index) release_buffer_later(v.index);
    }
    for (auto& g : depth_groups)
        if (g.second) release_bind_group_later(g.second);
}

WGPUTextureView objmv_view() {
    return st.vel_frame == frame_number ? st.vel_v : nullptr;
}

}  // namespace rx
