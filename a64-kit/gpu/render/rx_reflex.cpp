// NVIDIA Reflex (Low Latency mode, and its Boost) through Vulkan's VK_NV_low_latency2, on wgpu's swapchain
// (third_party/wgpu-native/totk-src: wgpu-hal makes the swapchain with low latency mode enabled and adds present IDs;
// wgpu-native's wgpuTotkReflex* make the calls). Vulkan only, NVIDIA only (the driver has the extension or not).
//
// The setting: Off / On / On + Boost (hos/input_bind.c: nvidia_reflex in input.ini, the F1 menu). With it on, the
// game's frame thread sleeps at the start of each frame for as long as the driver says (vkLatencySleepNV): the frame
// starts as late as it can and still be ready when the GPU is, instead of queuing frames ahead, so what is shown is
// closer to the input it came from. Boost keeps the GPU's clocks up.
//
// A frame here: the game simulates and records its NVN commands (simulation: from the sleep to its present call),
// the NVN replay thread translates and submits them (render submit: its first job of the frame to the frame's
// present job), the submit thread presents (present). Each stage counts frames on its own, in order, so frame n is
// the n-th present at every stage, and that number is the frame's present ID (the markers name frames by it).
#include "rx_internal.h"

#if RX_REFLEX && !RX_WEB

#include <atomic>

extern "C" {
void wgpuTotkAddVulkanExtensions(const char* const* instance_extensions, size_t instance_count,
                                 const char* const* device_extensions, size_t device_count);
void wgpuTotkReflexWanted(bool wanted);
bool wgpuTotkReflexAvailable(WGPUDevice device);
void wgpuTotkReflexSetMode(bool on, bool boost, uint32_t minimum_interval_us);
bool wgpuTotkReflexSleep(WGPUDevice device, uint64_t timeout_ns);
void wgpuTotkReflexMarker(uint32_t marker, uint64_t present_id);
void wgpuTotkSetNextPresentId(uint64_t present_id);
extern volatile int hos_reflex_mode;   // hos/input_bind.c: 0 off, 1 on, 2 on + boost
}

namespace rx {
namespace {

enum : uint32_t {   // VkLatencyMarkerNV
    kSimulationStart = 0, kSimulationEnd = 1, kRenderSubmitStart = 2, kRenderSubmitEnd = 3, kPresentStart = 4,
    kPresentEnd = 5
};

std::atomic<bool> available{false};
std::atomic<int> applied{-1};   // the mode set on the driver (-1: none yet)
uint64_t sim_frame = 1, submit_frame = 1;      // (the game's frame thread; the NVN replay thread)
std::atomic<uint64_t> present_frame{1};        // (the submit thread)
bool submit_in_frame = false;
std::atomic<int64_t> sleep_ns{0};              // (RX_STATS: the game's present thread in vkLatencySleepNV)

int mode() {
    const int m = hos_reflex_mode;
    return m < 0 || m > 2 ? 0 : m;
}

}  // namespace

// (RX_NO_REFLEX=1: none of it: the extensions not asked for, swapchains as wgpu makes them)
void reflex_before_device() {
    if (getenv("RX_NO_REFLEX")) return;
    static const char* const ext[2] = {"VK_NV_low_latency2", "VK_KHR_present_id"};
    wgpuTotkAddVulkanExtensions(nullptr, 0, ext, 2);
    wgpuTotkReflexWanted(true);
}

void reflex_after_device() {
    if (getenv("RX_NO_REFLEX")) return;
    available = wgpuTotkReflexAvailable(dev.device);
    hos_log("rx: NVIDIA Reflex: %s", available ? "available (VK_NV_low_latency2)"
                                               : "not available here (needs an NVIDIA GPU, its driver, and Vulkan)");
}

bool reflex_available() { return available; }
int64_t reflex_take_sleep_ns() { return sleep_ns.exchange(0); }

// The game's frame thread, at its present: the frame's simulation ends; then the next one's starts, after the sleep.
void reflex_game_present_begin() {
    if (!available || !mode()) return;
    wgpuTotkReflexMarker(kSimulationEnd, sim_frame);
}

void reflex_game_present_end() {
    sim_frame++;
    if (!available) return;
    const int m = mode();
    if (m != applied.load()) {
        wgpuTotkReflexSetMode(m != 0, m == 2, 0);
        applied = m;
        hos_log("rx: NVIDIA Reflex: %s", m == 0 ? "off" : m == 1 ? "on" : "on + boost");
    }
    if (!m) return;
    // (the driver signals when this frame should start; 100 ms at most: a swapchain made again meanwhile, a window
    // that is not shown)
    static int timeouts;
    const int64_t t0 = hos_now_ns();
    if (!wgpuTotkReflexSleep(dev.device, 100000000ull) && timeouts++ < 5)
        hos_log("rx: NVIDIA Reflex: no sleep this frame (no swapchain yet, or the wait timed out)");
    sleep_ns += hos_now_ns() - t0;
    wgpuTotkReflexMarker(kSimulationStart, sim_frame);
}

// The NVN replay thread: a job taken (the first of a frame starts its render submit), and a frame's present job.
void reflex_replay_job(bool present) {
    if (!available || !mode()) {
        if (present) submit_frame++, submit_in_frame = false;
        return;
    }
    if (!submit_in_frame) {
        wgpuTotkReflexMarker(kRenderSubmitStart, submit_frame);
        submit_in_frame = true;
    }
    if (present) {
        wgpuTotkReflexMarker(kRenderSubmitEnd, submit_frame);
        submit_frame++;
        submit_in_frame = false;
    }
}

// The submit thread, around the swapchain present (the frame's present ID goes with it).
uint64_t reflex_present_begin() {
    const uint64_t id = present_frame++;
    if (!available) return id;
    wgpuTotkSetNextPresentId(id);
    if (mode()) wgpuTotkReflexMarker(kPresentStart, id);
    return id;
}

void reflex_present_end(uint64_t id) {
    if (available && mode()) wgpuTotkReflexMarker(kPresentEnd, id);
}

}  // namespace rx

#else   // (no Reflex in this build: the wgpu_native.dll has no wgpuTotkReflex*, or the web build)

namespace rx {
void reflex_before_device() {}
void reflex_after_device() {}
bool reflex_available() { return false; }
int64_t reflex_take_sleep_ns() { return 0; }
void reflex_game_present_begin() {}
void reflex_game_present_end() {}
void reflex_replay_job(bool) {}
uint64_t reflex_present_begin() { return 0; }
void reflex_present_end(uint64_t) {}
}  // namespace rx

#endif
