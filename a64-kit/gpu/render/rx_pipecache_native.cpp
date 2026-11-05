// The native pipeline cache: every WGSL text the translator makes and every render pipeline made from them are
// appended to rxcache/pipelines.bin next to the exe; at the next start a thread reads the file and the render thread
// hands its pipelines to the pipeline compilers a few at a time (while the title screen shows), under the keys the
// draws will look them up by. Shader modules and pipelines (WGSL -> naga -> SPIR-V -> the driver, most of the
// pipeline compilers' time: 2.6 s of modules for 460 pipelines at one loading) are then ready before the game first
// draws with them; only the translation of the game's programs still happens on first use (on its workers).
// Compute programs ('C' records: their WGSL) get their shader modules made by the reading thread: the render thread
// made them in place at their first dispatch (WGSL parsing: ~270 ms of the boot's first frame).
// The file holds the game's shaders (made from its data): it stays on this machine, never in the repository.
// RX_NO_PIPELINE_CACHE=1: neither read nor written. Every pipeline in the file is made at a start, the most recently
// used first, until Windows' commit to spare falls to RX_PIPELINE_CACHE_FREE_MB (10240): each made pipeline holds
// ~300-400 KB of host memory (wgpu keeps its shader modules' IR, the driver its code; 3783 of them were 1.2 GB of the
// working set). RX_PIPELINE_CACHE_MAX=<n>: at most n (default 8000); RX_PIPELINE_CACHE_SESSIONS=<n>: only what the last n sessions
// drew with ('U' records: the keys each session used); RX_PIPELINE_CACHE_ALL=1: no memory check.
#if !RX_WEB
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "rx_internal.h"

namespace rx {
namespace {

constexpr uint32_t kMagic = 0x4E505852;   // "RXPN"
constexpr uint32_t kVersion = 1;

uint32_t signature() {   // (another wgpu header layout or record format: the file is started again)
    return (uint32_t)(sizeof(WGPUVertexAttribute) * 3 + sizeof(WGPUColorTargetState) * 5 + sizeof(WGPUBlendState) * 7 +
                      sizeof(WGPUPrimitiveState) * 11 + sizeof(WGPUDepthStencilState) * 13 + kVersion * 17);
}

struct Writer {
    std::string& out;
    template <typename T> void put(const T& v) { out.append(reinterpret_cast<const char*>(&v), sizeof v); }
    void bytes(const void* p, size_t n) { out.append(static_cast<const char*>(p), n); }
};
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <typename T> T get() {
        T v{};
        if (end - p < (ptrdiff_t)sizeof v) { ok = false; return v; }
        std::memcpy(&v, p, sizeof v);
        p += sizeof v;
        return v;
    }
};

std::string path_of_cache() {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, sizeof exe);
    while (n && exe[n - 1] != '\\' && exe[n - 1] != '/') n--;
    exe[n] = 0;
    std::string dir = std::string(exe) + "rxcache";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\pipelines.bin";
}

bool nc_on = true;
std::mutex nc_mu;   // (texts are recorded from the translation workers, pipelines from the render thread)
std::string nc_pending;
std::unordered_set<uint64_t> nc_known_texts, nc_known_pipelines;
std::string nc_path;
int64_t nc_last_save = 0;
std::mutex nc_file_mu;   // (the file: save()'s appends, and load()'s reading and compaction)

// what the loader read (the render thread takes it once `loaded` is set)
std::atomic<bool> nc_loaded{false};
std::unordered_map<uint64_t, std::string> nc_texts;
std::vector<PipelineDesc> nc_records;   // in file order (the newest last)
size_t nc_next_record = 0;
std::unordered_map<uint64_t, std::string> nc_compute_texts;          // (the reading thread)
std::unordered_map<uint64_t, WGPUShaderModule> nc_compute_modules;   // made from them (nc_mu)
std::unordered_set<uint64_t> nc_known_compute;
uint32_t nc_session = 0;                 // this session's number (one more than the file's last; set by the reader)
std::vector<uint64_t> nc_used_pending;   // pipeline keys this session used, not yet written (nc_mu)
int64_t nc_made = 0, nc_t_start = 0;
bool nc_reported = false;

void put_desc(Writer& w, const PipelineDesc& d) {
    w.put(d.key);
    w.put(d.vs_text);
    w.put(d.fs_text);
    w.put((uint32_t)d.nlayouts);
    for (int i = 0; i < d.nlayouts && i < 2; i++) {
        w.put((uint32_t)d.layout[i].size());
        w.bytes(d.layout[i].data(), d.layout[i].size() * 4);
    }
    w.put((uint32_t)d.buffers.size());
    for (size_t i = 0; i < d.buffers.size(); i++) {
        w.put((uint32_t)d.buffers[i].stepMode);
        w.put(d.buffers[i].arrayStride);
        const auto& attrs = d.attributes[i];
        w.put((uint32_t)attrs.size());
        for (const WGPUVertexAttribute& a : attrs) {
            w.put((uint32_t)a.format);
            w.put(a.offset);
            w.put(a.shaderLocation);
        }
    }
    w.put((uint32_t)d.targets.size());
    for (size_t i = 0; i < d.targets.size(); i++) {
        w.put((uint32_t)d.targets[i].format);
        w.put((uint64_t)d.targets[i].writeMask);
        w.put((uint8_t)(i < d.has_blend.size() ? d.has_blend[i] : 0));
        const WGPUBlendState& b = d.blends[i < 8 ? i : 7];
        for (const WGPUBlendComponent& c : {b.color, b.alpha}) {
            w.put((uint32_t)c.operation);
            w.put((uint32_t)c.srcFactor);
            w.put((uint32_t)c.dstFactor);
        }
    }
    const WGPUPrimitiveState& p = d.primitive;
    w.put((uint32_t)p.topology);
    w.put((uint32_t)p.stripIndexFormat);
    w.put((uint32_t)p.frontFace);
    w.put((uint32_t)p.cullMode);
    w.put((uint32_t)p.unclippedDepth);
    w.put((uint8_t)d.has_depth);
    if (d.has_depth) {
        const WGPUDepthStencilState& s = d.depth;
        w.put((uint32_t)s.format);
        w.put((uint32_t)s.depthWriteEnabled);
        w.put((uint32_t)s.depthCompare);
        for (const WGPUStencilFaceState& f : {s.stencilFront, s.stencilBack}) {
            w.put((uint32_t)f.compare);
            w.put((uint32_t)f.failOp);
            w.put((uint32_t)f.depthFailOp);
            w.put((uint32_t)f.passOp);
        }
        w.put(s.stencilReadMask);
        w.put(s.stencilWriteMask);
        w.put(s.depthBias);
        w.put(s.depthBiasSlopeScale);
        w.put(s.depthBiasClamp);
    }
}

bool get_desc(Reader& r, PipelineDesc& d) {
    d.key = r.get<uint64_t>();
    d.vs_text = r.get<uint64_t>();
    d.fs_text = r.get<uint64_t>();
    d.nlayouts = (int)r.get<uint32_t>();
    if (d.nlayouts < 1 || d.nlayouts > 2) return false;
    for (int i = 0; i < d.nlayouts; i++) {
        const uint32_t n = r.get<uint32_t>();
        if (n > 256) return false;
        d.layout[i].resize(n);
        for (uint32_t k = 0; k < n; k++) d.layout[i][k] = r.get<uint32_t>();
    }
    const uint32_t nb = r.get<uint32_t>();
    if (nb > 32) return false;
    d.buffers.resize(nb);
    d.attributes.resize(nb);
    for (uint32_t i = 0; i < nb; i++) {
        WGPUVertexBufferLayout& b = d.buffers[i];
        b = WGPUVertexBufferLayout{};
        b.stepMode = (WGPUVertexStepMode)r.get<uint32_t>();
        b.arrayStride = r.get<uint64_t>();
        const uint32_t na = r.get<uint32_t>();
        if (na > 32) return false;
        d.attributes[i].resize(na);
        for (WGPUVertexAttribute& a : d.attributes[i]) {
            a = WGPUVertexAttribute{};
            a.format = (WGPUVertexFormat)r.get<uint32_t>();
            a.offset = r.get<uint64_t>();
            a.shaderLocation = r.get<uint32_t>();
        }
    }
    const uint32_t nt = r.get<uint32_t>();
    if (nt > 8) return false;
    d.targets.resize(nt);
    d.has_blend.resize(nt);
    for (uint32_t i = 0; i < nt; i++) {
        WGPUColorTargetState& t = d.targets[i];
        t = WGPUColorTargetState{};
        t.format = (WGPUTextureFormat)r.get<uint32_t>();
        t.writeMask = (WGPUColorWriteMask)r.get<uint64_t>();
        d.has_blend[i] = r.get<uint8_t>();
        WGPUBlendState& b = d.blends[i];
        for (WGPUBlendComponent* c : {&b.color, &b.alpha}) {
            c->operation = (WGPUBlendOperation)r.get<uint32_t>();
            c->srcFactor = (WGPUBlendFactor)r.get<uint32_t>();
            c->dstFactor = (WGPUBlendFactor)r.get<uint32_t>();
        }
    }
    WGPUPrimitiveState& p = d.primitive;
    p = WGPUPrimitiveState{};
    p.topology = (WGPUPrimitiveTopology)r.get<uint32_t>();
    p.stripIndexFormat = (WGPUIndexFormat)r.get<uint32_t>();
    p.frontFace = (WGPUFrontFace)r.get<uint32_t>();
    p.cullMode = (WGPUCullMode)r.get<uint32_t>();
    p.unclippedDepth = (WGPUBool)r.get<uint32_t>();
    d.has_depth = r.get<uint8_t>() != 0;
    if (d.has_depth) {
        WGPUDepthStencilState& s = d.depth;
        s = WGPUDepthStencilState{};
        s.format = (WGPUTextureFormat)r.get<uint32_t>();
        s.depthWriteEnabled = (WGPUOptionalBool)r.get<uint32_t>();
        s.depthCompare = (WGPUCompareFunction)r.get<uint32_t>();
        for (WGPUStencilFaceState* f : {&s.stencilFront, &s.stencilBack}) {
            f->compare = (WGPUCompareFunction)r.get<uint32_t>();
            f->failOp = (WGPUStencilOperation)r.get<uint32_t>();
            f->depthFailOp = (WGPUStencilOperation)r.get<uint32_t>();
            f->passOp = (WGPUStencilOperation)r.get<uint32_t>();
        }
        s.stencilReadMask = r.get<uint32_t>();
        s.stencilWriteMask = r.get<uint32_t>();
        s.depthBias = r.get<int32_t>();
        s.depthBiasSlopeScale = r.get<float>();
        s.depthBiasClamp = r.get<float>();
    }
    return r.ok;
}

void add_record(char type, const std::string& payload) {   // (nc_mu held)
    Writer w{nc_pending};
    w.put(type);
    w.put((uint32_t)payload.size());
    w.bytes(payload.data(), payload.size());
}

/// Reads the file (a thread of its own): texts by hash and the pipeline records, for the render thread.
void load() {
    std::unique_lock file_lock(nc_file_mu);   // (save() skips its writes meanwhile: they wait in nc_pending)
    FILE* f = std::fopen(nc_path.c_str(), "rb");
    if (!f) {
        nc_loaded = true;
        return;
    }
    uint32_t head[3] = {};
    std::string payload;
    size_t n_texts = 0;
    int64_t bytes = 0;
    if (std::fread(head, 1, sizeof head, f) == sizeof head && head[0] == kMagic && head[1] == kVersion &&
        head[2] == signature()) {
        std::unordered_map<uint64_t, size_t> where;   // pipeline key -> its record (a later one replaces it)
        std::vector<PipelineDesc> list;
        std::unordered_map<uint64_t, uint32_t> last_session;   // pipeline key -> the last session that used it
        uint32_t sessions = 0;
        for (;;) {
            char type;
            uint32_t len;
            if (std::fread(&type, 1, 1, f) != 1 || std::fread(&len, 1, 4, f) != 4 || len > (64u << 20)) break;
            payload.resize(len);
            if (std::fread(payload.data(), 1, len, f) != len) break;
            bytes += 5 + len;
            Reader r{reinterpret_cast<const uint8_t*>(payload.data()), reinterpret_cast<const uint8_t*>(payload.data()) + len};
            if (type == 'T' && len >= 8) {
                const uint64_t h = r.get<uint64_t>();
                std::lock_guard lk(nc_mu);
                if (nc_known_texts.insert(h).second) {
                    nc_texts.emplace(h, payload.substr(8));
                    n_texts++;
                }
            } else if (type == 'U' && len >= 4) {
                const uint32_t session = r.get<uint32_t>();
                sessions = std::max(sessions, session);
                while (r.ok && r.p < r.end) {
                    const uint64_t key = r.get<uint64_t>();
                    if (!r.ok) break;
                    uint32_t& last = last_session[key];
                    last = std::max(last, session);
                }
            } else if (type == 'C' && len >= 8) {
                const uint64_t h = r.get<uint64_t>();
                std::lock_guard lk(nc_mu);
                if (nc_known_compute.insert(h).second) nc_compute_texts.emplace(h, payload.substr(8));
            } else if (type == 'P') {
                PipelineDesc d;
                if (!get_desc(r, d)) continue;
                {
                    std::lock_guard lk(nc_mu);
                    nc_known_pipelines.insert(d.key);
                }
                auto it = where.find(d.key);
                if (it != where.end()) list[it->second] = std::move(d);
                else {
                    where.emplace(d.key, list.size());
                    list.push_back(std::move(d));
                }
            }
        }
        // Which are made, and in what order: the most recently used first (the session that last drew with each,
        // then the newest in the file), and as many as Windows' commit allows (see native_cache_step): each made
        // pipeline holds ~300-400 KB of host memory (wgpu keeps its shader modules' IR, the driver its code). Before,
        // all-or-nothing by the machine's memory and at most the newest 6000 of them: on a 32 GB machine with a small
        // page file only the last 4 sessions' 3,988 of 17,869 were made, and places played before compiled their
        // pipelines again when they came back (drawn late: things blinking in). RX_PIPELINE_CACHE_SESSIONS=<n>: only
        // what the last n sessions used, as before.
        static const int window = getenv("RX_PIPELINE_CACHE_SESSIONS") ? atoi(getenv("RX_PIPELINE_CACHE_SESSIONS")) : 0;
        std::vector<std::pair<uint32_t, size_t>> order;   // (last session, file index)
        order.reserve(list.size());
        size_t stale = 0;
        for (size_t i = 0; i < list.size(); i++) {
            auto it = last_session.find(list[i].key);
            const uint32_t last = it == last_session.end() ? 0 : it->second;   // (0: recorded before 'U' records)
            if (window > 0 && (int64_t)last < (int64_t)sessions + 1 - window) {
                stale++;
                continue;
            }
            order.push_back({last, i});
        }
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second > b.second;
        });
        // Compaction: the file only grows (every pipeline any session made, and each session's 'U' record), 359 MB with
        // 314,701 pipelines in the user's log, and at 400 MB it was started again, every pipeline lost. Past
        // kCompactRecords pipelines or kCompactBytes it is written again with the most recently used kKeepRecords
        // (far more than are made at a start, RX_PIPELINE_CACHE_MAX), the texts they use, the compute programs and one
        // 'U' record per session for them.
        constexpr size_t kCompactRecords = 80000, kKeepRecords = 50000;
        constexpr int64_t kCompactBytes = 250ll << 20;
        if (order.size() > kCompactRecords || bytes > kCompactBytes) {
            std::fclose(f);
            f = nullptr;
            const size_t keep = std::min(order.size(), kKeepRecords);
            std::unordered_set<uint64_t> texts, keys;
            for (size_t k = 0; k < keep; k++) {
                const PipelineDesc& d = list[order[k].second];
                texts.insert(d.vs_text);
                if (d.fs_text) texts.insert(d.fs_text);
                keys.insert(d.key);
            }
            std::string out;
            Writer w{out};
            const uint32_t head2[3] = {kMagic, kVersion, signature()};
            w.bytes(head2, sizeof head2);
            auto record = [&](char type, const std::string& payload) {
                w.put(type);
                w.put((uint32_t)payload.size());
                w.bytes(payload.data(), payload.size());
            };
            std::string payload2;
            {
                std::lock_guard lk(nc_mu);
                for (uint64_t h : texts) {
                    auto it = nc_texts.find(h);
                    if (it == nc_texts.end()) continue;
                    payload2.clear();
                    Writer pw{payload2};
                    pw.put(h);
                    pw.bytes(it->second.data(), it->second.size());
                    record('T', payload2);
                }
                for (const auto& [h, text] : nc_compute_texts) {
                    payload2.clear();
                    Writer pw{payload2};
                    pw.put(h);
                    pw.bytes(text.data(), text.size());
                    record('C', payload2);
                }
            }
            for (size_t k = keep; k-- > 0;) {   // (the oldest first: the file's order breaks ties as before)
                payload2.clear();
                Writer pw{payload2};
                put_desc(pw, list[order[k].second]);
                record('P', payload2);
            }
            std::unordered_map<uint32_t, std::vector<uint64_t>> by_session;
            for (size_t k = 0; k < keep; k++)
                if (order[k].first) by_session[order[k].first].push_back(list[order[k].second].key);
            for (auto& [session, ks] : by_session) {
                payload2.clear();
                Writer pw{payload2};
                pw.put(session);
                pw.bytes(ks.data(), ks.size() * sizeof(uint64_t));
                record('U', payload2);
            }
            const std::string tmp = nc_path + ".new";
            FILE* o = std::fopen(tmp.c_str(), "wb");
            const bool written = o && std::fwrite(out.data(), 1, out.size(), o) == out.size();
            if (o) std::fclose(o);
            if (written && MoveFileExA(tmp.c_str(), nc_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                hos_log("rx: pipeline cache: compacted %lld MB with %zu pipelines to %lld MB with the %zu most recently "
                        "used (%zu shader texts)", (long long)(bytes >> 20), order.size(),
                        (long long)(out.size() >> 20), keep, texts.size());
                bytes = (int64_t)out.size();
                order.resize(keep);
                std::lock_guard lk(nc_mu);   // (what was left out is recorded again when a session makes it)
                for (auto it = nc_texts.begin(); it != nc_texts.end();)
                    if (!texts.count(it->first)) nc_known_texts.erase(it->first), it = nc_texts.erase(it);
                    else ++it;
                n_texts = nc_texts.size();
                for (const PipelineDesc& d : list)
                    if (!keys.count(d.key)) nc_known_pipelines.erase(d.key);
            } else {
                std::remove(tmp.c_str());
                hos_log("rx: pipeline cache: could not compact %s", nc_path.c_str());
            }
        }
        nc_records.reserve(order.size());
        for (const auto& [last, i] : order) nc_records.push_back(std::move(list[i]));
        nc_session = sessions + 1;
        if (stale)
            hos_log("rx: pipeline cache: %zu pipelines no draw used in the last %d sessions are not made", stale, window);
    } else {
        std::fclose(f);
        f = nullptr;
        std::remove(nc_path.c_str());
        hos_log("rx: pipeline cache %s is from another build or damaged: started again", nc_path.c_str());
    }
    if (f) std::fclose(f);
    if (bytes > (600ll << 20)) {   // (grown past use even after compaction: the next session starts it again)
        std::remove(nc_path.c_str());
        std::lock_guard lk(nc_mu);
        nc_known_texts.clear();
        nc_known_pipelines.clear();
        hos_log("rx: pipeline cache %s: %lld MB, started again", nc_path.c_str(), (long long)(bytes >> 20));
    }
    hos_log("rx: pipeline cache: %zu shader texts, %zu pipelines to make in the background, %zu compute programs "
            "(%lld MB)", n_texts, nc_records.size(), nc_compute_texts.size(), (long long)(bytes >> 20));
    file_lock.unlock();
    nc_loaded = true;
    // the compute programs' modules, here (WGSL parsing and validation; they compiled when they were recorded)
    const int64_t t0 = hos_now_ns();
    size_t made = 0;
    for (auto& [h, text] : nc_compute_texts) {
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = WGPUStringView{text.data(), text.size()};
        WGPUShaderModuleDescriptor d{};
        d.nextInChain = &src.chain;
        if (WGPUShaderModule m = program_module(&d)) {
            std::lock_guard lk(nc_mu);
            nc_compute_modules.emplace(h, m);
            made++;
        }
    }
    nc_compute_texts.clear();
    if (made) hos_log("rx: pipeline cache: %zu compute modules made in %.0f ms", made, (hos_now_ns() - t0) / 1e6);
}

void save() {
    std::string out;
    {
        std::lock_guard lk(nc_mu);
        if (nc_loaded && nc_session && !nc_used_pending.empty()) {   // (the session's number is known once read)
            std::string payload;
            Writer w{payload};
            w.put(nc_session);
            w.bytes(nc_used_pending.data(), nc_used_pending.size() * sizeof(uint64_t));
            add_record('U', payload);
            nc_used_pending.clear();
        }
        if (nc_pending.empty()) return;
        out.swap(nc_pending);
    }
    std::unique_lock file_lock(nc_file_mu, std::try_to_lock);
    if (!file_lock.owns_lock()) {   // (load() is reading or compacting the file: the next save writes these)
        std::lock_guard lk(nc_mu);
        nc_pending.insert(0, out);
        return;
    }
    FILE* f = std::fopen(nc_path.c_str(), "ab");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);   // (in append mode the position reads 0 until the first write)
    if (std::ftell(f) == 0) {
        const uint32_t head[3] = {kMagic, kVersion, signature()};
        std::fwrite(head, 1, sizeof head, f);
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
}

// ---- the SPIR-V cache (rxcache/spirv.bin): the SPIR-V wgpu's Vulkan backend wrote for each pipeline stage. A later
// session makes the stage's module from it (wgpuDeviceCreateShaderModuleSpirV, passthrough), so neither the WGSL's
// parsing and validation (the user's logs: "modules 6283 ms" of 8.6 s for 1,108 pipelines at a start; 36 s of 42 at
// the Great Sky Island spawn) nor the SPIR-V writing per pipeline happen again. A stage's SPIR-V depends on its WGSL,
// its stage and the binding numbers of its bind group layout (wgpu packs them: binding 5 alone is Vulkan's binding 0),
// so it is kept under those three. Needs the patched wgpu_native.dll (wgpuTotkCreateRenderPipelineCapture; the
// launcher builds it) and passthrough on the device (Vulkan); otherwise off. The file is tied to the wgpu_native.dll
// it came from (its size and time: another naga may write other SPIR-V): another one starts it again. Records: u64
// key, u64 the WGSL's hash, u32 stage, u32 words, the words; appended as pipelines are made, read when used. The
// game's shaders (made from its data): it stays on this machine, never in the repository. RX_NO_SPIRV_CACHE=1: off.
constexpr uint32_t kSpvMagic = 0x56535852;   // "RXSV"
constexpr uint32_t kSpvVersion = 1;
constexpr int64_t kSpvMaxBytes = 1024ll << 20;   // (past that at a start: started again; nothing added past it)

using CaptureFn = WGPURenderPipeline (*)(WGPUDevice, const WGPURenderPipelineDescriptor*, uint32_t**, size_t*,
                                         uint32_t**, size_t*);
using FreeFn = void (*)(uint32_t*, size_t);
using PassthroughFn = bool (*)(WGPUDevice);

struct SpvEntry {
    uint64_t text;
    uint32_t stage, words;
    int64_t offset;                    // of the words
    WGPUShaderModule module = nullptr;   // (made from them once; kept, as the WGSL modules are)
    bool bad = false;                    // (a pipeline with it reported an error: WGSL from now on)
};
std::mutex sv_mu;
std::once_flag sv_once;
bool sv_on = false;
HANDLE sv_file = INVALID_HANDLE_VALUE;
int64_t sv_end = 0;
std::unordered_map<uint64_t, SpvEntry> sv_index;
CaptureFn sv_capture = nullptr;
FreeFn sv_free = nullptr;
std::atomic<int64_t> sv_hits{0}, sv_kept{0};

uint64_t fnv(const void* p, size_t n, uint64_t h) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/// the key of a stage's SPIR-V: its WGSL, its stage and its layout's binding numbers (in the layout's order)
uint64_t sv_key(uint64_t text, const std::vector<uint32_t>& layout, uint32_t stage) {
    uint64_t h = fnv(&text, 8, 0xcbf29ce484222325ull);
    h = fnv(&stage, 4, h);
    for (uint32_t v : layout) {
        const uint32_t binding = v & 1023;   // (pack_layout: the binding in the low 10 bits)
        h = fnv(&binding, 4, h);
    }
    return fnv(&kSpvVersion, 4, h ^ layout.size());
}

bool sv_io(bool write, int64_t off, void* p, DWORD n) {   // (sv_mu held)
    OVERLAPPED o{};
    o.Offset = (DWORD)off;
    o.OffsetHigh = (DWORD)(off >> 32);
    DWORD done = 0;
    const BOOL ok = write ? WriteFile(sv_file, p, n, &done, &o) : ReadFile(sv_file, p, n, &done, &o);
    return ok && done == n;
}

/// wgpu_native.dll's size and time: the SPIR-V is what that build's naga wrote
uint64_t sv_stamp() {
    HMODULE m = GetModuleHandleA("wgpu_native.dll");
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!m || !GetModuleFileNameA(m, path, sizeof path) || !GetFileAttributesExA(path, GetFileExInfoStandard, &a))
        return 0;
    uint64_t h = fnv(&a.nFileSizeLow, 4, 0xcbf29ce484222325ull);
    h = fnv(&a.nFileSizeHigh, 4, h);
    return fnv(&a.ftLastWriteTime, sizeof a.ftLastWriteTime, h);
}

void sv_open() {   // (sv_mu held)
    if (!nc_on || getenv("RX_NO_SPIRV_CACHE") || getenv("RX_SHADER_CHECKS")) return;
    HMODULE m = GetModuleHandleA("wgpu_native.dll");
    if (!m) return;
    sv_capture = (CaptureFn)(void*)GetProcAddress(m, "wgpuTotkCreateRenderPipelineCapture");
    sv_free = (FreeFn)(void*)GetProcAddress(m, "wgpuTotkFreeSpirv");
    auto passthrough = (PassthroughFn)(void*)GetProcAddress(m, "wgpuTotkPassthroughShaders");
    if (!sv_capture || !sv_free || !passthrough) {
        hos_log("rx: SPIR-V cache: off (this wgpu_native.dll is from before it: the launcher's build makes a new one)");
        return;
    }
    if (!passthrough(dev.device)) {
        hos_log("rx: SPIR-V cache: off (the device has no SPIR-V passthrough)");
        return;
    }
    const uint64_t stamp = sv_stamp();
    std::string path = nc_path;
    path.replace(path.size() - std::strlen("pipelines.bin"), std::string::npos, "spirv.bin");
    sv_file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sv_file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(sv_file, &size);
    uint32_t head[4] = {};
    const char* why = nullptr;
    if (size.QuadPart == 0) why = "";
    else if (size.QuadPart < (int64_t)sizeof head || !sv_io(false, 0, head, sizeof head) || head[0] != kSpvMagic ||
             head[1] != kSpvVersion) why = "from another build or damaged";
    else if ((((uint64_t)head[3] << 32) | head[2]) != stamp) why = "made by another wgpu_native.dll";
    else if (size.QuadPart > kSpvMaxBytes) why = "grown past use";
    int64_t off = sizeof head;
    if (!why) {   // the index: each record's key and where its words are (the words are read when used)
        struct Rec {
            uint64_t key, text;
            uint32_t stage, words;
        } r;
        static_assert(sizeof(Rec) == 24);
        while (off + (int64_t)sizeof r <= size.QuadPart && sv_io(false, off, &r, sizeof r) && r.words &&
               r.words < (16u << 20) && off + (int64_t)sizeof r + (int64_t)r.words * 4 <= size.QuadPart) {
            SpvEntry& e = sv_index[r.key];
            e.text = r.text, e.stage = r.stage, e.words = r.words, e.offset = off + (int64_t)sizeof r;
            off += sizeof r + (int64_t)r.words * 4;
        }
        if (off != size.QuadPart) {   // (a record cut short by an abrupt exit: written over from there)
            LARGE_INTEGER at{};
            at.QuadPart = off;
            SetFilePointerEx(sv_file, at, nullptr, FILE_BEGIN);
            SetEndOfFile(sv_file);
        }
    } else {
        if (*why) hos_log("rx: SPIR-V cache %s: %s, started again", path.c_str(), why);
        LARGE_INTEGER zero{};
        SetFilePointerEx(sv_file, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(sv_file);
        uint32_t h[4] = {kSpvMagic, kSpvVersion, (uint32_t)stamp, (uint32_t)(stamp >> 32)};
        sv_io(true, 0, h, sizeof h);
        off = sizeof h;
    }
    sv_end = off;
    sv_on = true;
    hos_log("rx: SPIR-V cache: %zu pipeline stages kept (%lld MB)", sv_index.size(), (long long)(off >> 20));
}

} // namespace

bool spirv_cache_on() {
    std::call_once(sv_once, [] {
        std::lock_guard lk(sv_mu);
        sv_open();
    });
    return sv_on;
}

WGPUShaderModule spirv_stage_module(uint64_t text, const std::vector<uint32_t>& layout, uint32_t stage) {
    if (!text || !spirv_cache_on()) return nullptr;
    const uint64_t key = sv_key(text, layout, stage);
    std::vector<uint32_t> words;
    {
        std::lock_guard lk(sv_mu);
        auto it = sv_index.find(key);
        if (it == sv_index.end() || it->second.bad || it->second.text != text || it->second.stage != stage)
            return nullptr;
        if (it->second.module) {
            sv_hits++;
            return it->second.module;
        }
        words.resize(it->second.words);
        if (!sv_io(false, it->second.offset, words.data(), (DWORD)(words.size() * 4))) return nullptr;
    }
    WGPUShaderModuleDescriptorSpirV d{};
    d.sourceSize = (uint32_t)words.size();
    d.source = words.data();
    WGPUShaderModule m = wgpuDeviceCreateShaderModuleSpirV(dev.device, &d);   // (passthrough: no parsing)
    if (!m) return nullptr;
    std::lock_guard lk(sv_mu);
    SpvEntry& e = sv_index[key];
    if (e.module) {   // (another compiler made it meanwhile)
        wgpuShaderModuleRelease(m);
        m = e.module;
    }
    e.module = m;
    sv_hits++;
    return m;
}

void spirv_stage_bad(uint64_t text, const std::vector<uint32_t>& layout, uint32_t stage) {
    std::lock_guard lk(sv_mu);
    auto it = sv_index.find(sv_key(text, layout, stage));
    if (it != sv_index.end()) it->second.bad = true;
}

WGPURenderPipeline spirv_create_pipeline(const WGPURenderPipelineDescriptor* d, SpirvCapture& c) {
    c = SpirvCapture{};
    if (!spirv_cache_on()) return wgpuDeviceCreateRenderPipeline(dev.device, d);
    return sv_capture(dev.device, d, &c.vs, &c.vs_count, &c.fs, &c.fs_count);
}

void spirv_keep(SpirvCapture& c, bool ok, uint64_t vs_text, const std::vector<uint32_t>& vs_layout, uint64_t fs_text,
                const std::vector<uint32_t>& fs_layout) {
    struct Stage {
        uint32_t*& words;
        size_t& count;
        uint64_t text;
        const std::vector<uint32_t>& layout;
        uint32_t stage;
    };
    for (Stage s : {Stage{c.vs, c.vs_count, vs_text, vs_layout, 0}, Stage{c.fs, c.fs_count, fs_text, fs_layout, 1}}) {
        if (!s.words) continue;
        if (ok && s.text && s.count) {
            const uint64_t key = sv_key(s.text, s.layout, s.stage);
            std::string rec;
            Writer w{rec};
            w.put(key);
            w.put(s.text);
            w.put(s.stage);
            w.put((uint32_t)s.count);
            w.bytes(s.words, s.count * 4);
            std::lock_guard lk(sv_mu);
            if (sv_on && !sv_index.count(key) && sv_end + (int64_t)rec.size() <= kSpvMaxBytes &&
                sv_io(true, sv_end, rec.data(), (DWORD)rec.size())) {
                SpvEntry& e = sv_index[key];
                e.text = s.text, e.stage = s.stage, e.words = (uint32_t)s.count, e.offset = sv_end + 24;
                sv_end += rec.size();
                sv_kept++;
            }
        }
        sv_free(s.words, s.count);
        s.words = nullptr;
        s.count = 0;
    }
}

void spirv_stats(int64_t* hits, int64_t* kept) {
    *hits = sv_hits.load();
    *kept = sv_kept.load();
}

bool native_cache_on() { return nc_on; }

void native_cache_start() {
    nc_on = getenv("RX_NO_PIPELINE_CACHE") == nullptr;
    if (!nc_on) return;
    nc_path = path_of_cache();
    nc_t_start = hos_now_ns();
    std::thread([] {
        background_thread();
        load();
    }).detach();
}

void native_cache_text(uint64_t h, const std::string& text) {
    if (!nc_on) return;
    std::lock_guard lk(nc_mu);
    if (!nc_known_texts.insert(h).second) return;
    std::string payload;
    Writer w{payload};
    w.put(h);
    w.bytes(text.data(), text.size());
    add_record('T', payload);
}

void native_cache_used(uint64_t key) {
    if (!nc_on) return;
    std::lock_guard lk(nc_mu);
    nc_used_pending.push_back(key);
}

void native_cache_compute_text(uint64_t h, const std::string& text) {
    if (!nc_on) return;
    std::lock_guard lk(nc_mu);
    if (!nc_known_compute.insert(h).second) return;
    std::string payload;
    Writer w{payload};
    w.put(h);
    w.bytes(text.data(), text.size());
    add_record('C', payload);
}

WGPUShaderModule native_cache_compute_module(uint64_t h) {
    if (!nc_on) return nullptr;
    std::lock_guard lk(nc_mu);
    auto it = nc_compute_modules.find(h);
    if (it == nc_compute_modules.end()) return nullptr;
    WGPUShaderModule m = it->second;   // (the caller's now)
    nc_compute_modules.erase(it);
    return m;
}

void native_cache_pipeline(const PipelineDesc& d) {
    if (!nc_on) return;
    std::lock_guard lk(nc_mu);
    if (!nc_known_pipelines.insert(d.key).second) return;
    std::string payload;
    Writer w{payload};
    put_desc(w, d);
    add_record('P', payload);
}

void native_cache_step() {
    if (!nc_on) return;
    const int64_t now = hos_now_ns();
    if (now - nc_last_save > 5000000000LL) {   // (new records every 5 s: an abrupt exit loses at most those)
        nc_last_save = now;
        save();
    }
    if (!nc_loaded.load(std::memory_order_acquire) || nc_next_record >= nc_records.size()) {
        if (nc_loaded && !nc_reported && nc_next_record && nc_next_record >= nc_records.size() && prewarm_in_flight() == 0) {
            nc_reported = true;
            uint64_t priv = 0, ws = 0;
            hos_process_memory(&priv, &ws);
            hos_log("rx: pipeline cache: %lld pipelines made in the background in %.1f s (the process: %llu MB private "
                    "now)", (long long)nc_made, (hos_now_ns() - nc_t_start) / 1e9, (unsigned long long)(priv >> 20));
            nc_records.clear();
            nc_records.shrink_to_fit();
            std::lock_guard lk(nc_mu);
            nc_texts.clear();
        }
        return;
    }
    // RX_PIPELINE_CACHE_MAX=<n>: at most n (the most recently used first); otherwise until Windows' commit to spare
    // falls to RX_PIPELINE_CACHE_FREE_MB (10240: the game grows by several GB as it plays, and running out of commit
    // crashes it, see take_stream_memory), checked every 32. RX_PIPELINE_CACHE_ALL=1: no memory check.
    // (default 8000: run_mem.log made 22517 of the file's 222093 in 60 s at start and the game grew 7 GB for them,
    // ~310 KB each, held all session: with the rest of the game that left Windows' commit 372 MB from the crash in
    // run_vram.log. 8000, the most recently used, cover what the last sessions drew; the others are made when drawn)
    static const size_t cap = getenv("RX_PIPELINE_CACHE_MAX") ? (size_t)atoi(getenv("RX_PIPELINE_CACHE_MAX")) : 8000;
    static const uint64_t keep_free = (getenv("RX_PIPELINE_CACHE_FREE_MB") ? (uint64_t)atoll(getenv("RX_PIPELINE_CACHE_FREE_MB"))
                                                                            : 10240ull) << 20;
    static const bool no_check = getenv("RX_PIPELINE_CACHE_ALL") && std::strcmp(getenv("RX_PIPELINE_CACHE_ALL"), "0") != 0;
    static uint64_t private_at_start;
    if (nc_next_record == 0) {
        uint64_t ws = 0;
        hos_process_memory(&private_at_start, &ws);
    }
    if (nc_next_record >= cap && nc_next_record < nc_records.size()) {
        hos_log("rx: pipeline cache: stopped at %zu of %zu (RX_PIPELINE_CACHE_MAX)", nc_next_record, nc_records.size());
        nc_next_record = nc_records.size();
        return;
    }
    static size_t next_check = 32;
    if (!no_check && nc_next_record >= next_check) {
        next_check = nc_next_record + 32;
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof ms;
        if (GlobalMemoryStatusEx(&ms) && ms.ullAvailPageFile < keep_free) {
            uint64_t priv = 0, ws = 0;
            hos_process_memory(&priv, &ws);
            hos_log("rx: pipeline cache: stopped at %zu of %zu: Windows has %llu MB of commit free (keeps %llu MB; a "
                    "bigger page file makes more); the game grew %lld MB meanwhile", nc_next_record, nc_records.size(),
                    (unsigned long long)(ms.ullAvailPageFile >> 20), (unsigned long long)(keep_free >> 20),
                    (long long)((int64_t)(priv - private_at_start) >> 20));
            nc_next_record = nc_records.size();
            return;
        }
    }
    // a few at a time, while the compilers keep up: the game's own new pipelines go first
    for (int budget = 4; budget > 0 && nc_next_record < nc_records.size() && prewarm_in_flight() < 4; budget--) {
        const PipelineDesc& d = nc_records[nc_next_record++];
        std::string vs, fs;
        {
            std::lock_guard lk(nc_mu);
            auto v = nc_texts.find(d.vs_text);
            if (v == nc_texts.end()) continue;
            vs = v->second;
            if (d.fs_text) {
                auto f = nc_texts.find(d.fs_text);
                if (f == nc_texts.end()) continue;
                fs = f->second;
            }
        }
        ShaderModule* vm = prewarm_stage_module(d.vs_text, vs, "vertex");
        ShaderModule* fm = d.fs_text ? prewarm_stage_module(d.fs_text, fs, "pixel") : nullptr;
        if (!vm || (d.fs_text && !fm)) continue;
        if (prewarm_pipeline(d, vm, fm)) nc_made++;
    }
}

} // namespace rx

#else
#include "rx_internal.h"
namespace rx {
bool native_cache_on() { return false; }
void native_cache_start() {}
void native_cache_step() {}
void native_cache_text(uint64_t, const std::string&) {}
void native_cache_pipeline(const PipelineDesc&) {}
void native_cache_compute_text(uint64_t, const std::string&) {}
void native_cache_used(uint64_t) {}
WGPUShaderModule native_cache_compute_module(uint64_t) { return nullptr; }
bool spirv_cache_on() { return false; }
WGPUShaderModule spirv_stage_module(uint64_t, const std::vector<uint32_t>&, uint32_t) { return nullptr; }
void spirv_stage_bad(uint64_t, const std::vector<uint32_t>&, uint32_t) {}
WGPURenderPipeline spirv_create_pipeline(const WGPURenderPipelineDescriptor* d, SpirvCapture& c) {
    c = SpirvCapture{};
    return wgpuDeviceCreateRenderPipeline(dev.device, d);
}
void spirv_keep(SpirvCapture&, bool, uint64_t, const std::vector<uint32_t>&, uint64_t, const std::vector<uint32_t>&) {}
void spirv_stats(int64_t* hits, int64_t* kept) { *hits = *kept = 0; }
} // namespace rx
#endif
