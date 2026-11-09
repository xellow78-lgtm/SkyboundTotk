# SkyboundTOTK

**An experimental native Windows PC port of *The Legend of Zelda: Tears of the Kingdom*, made by static
recompilation.** The game's Switch (AArch64) code is translated ahead of time into C and compiled on your PC into a
normal `.exe`. A runtime provides the Switch services the game calls, and a renderer runs its graphics through WebGPU
(wgpu-native on Vulkan). It is a port of this one game, not a Switch emulator. It is not affiliated with Nintendo.

This branch (`update-1.4.3`) targets **TOTK 1.4.3** (the base game plus the 1.4.3 update). The original 1.0.0 build
still exists; most PC features below are 1.4.3-only.

> **Status: playable, with known bugs.** The game boots, loads saves, plays in the open world, shrines, the sky and
> the Depths, with sound and cutscene videos. Graphics bugs remain (see [Known issues](#known-issues)), and a full
> playthrough has not been verified yet. Expect rough edges.

No game files are included. You need your own dump of the game (see [Game files](#game-files-and-legal)).

---

## Contents

- [Status at a glance](#status-at-a-glance)
- [What works](#what-works)
- [Known issues](#known-issues)
- [Not done yet / roadmap](#not-done-yet--roadmap)
- [Download and play](#download-and-play)
- [Keys and settings](#keys-and-settings)
- [Mods](#mods)
- [Reporting a bug](#reporting-a-bug)
- [Building from source](#building-from-source)
- [Game files and legal](#game-files-and-legal)
- [Source and licenses](#source-and-licenses)

---

## Status at a glance

| Area | State |
| --- | --- |
| Boot, menus, saves | Working |
| Open world, sky islands, Depths, shrines | Playable; graphics bugs in some places |
| Full playthrough start to credits | **Not verified yet** |
| Audio | Working (fades to silence above 200 fps, so the frame rate is capped at 200) |
| Cutscene videos | Working (Windows Media Foundation) |
| Graphics | Mostly right; open bugs listed below |
| Upscaling | FSR 1, FSR 2.2, NVIDIA DLSS Super Resolution (DLSS needs an RTX card) |
| Resolution and aspect ratio | Output up to 4K; 16:9, 16:10, 21:9, 32:9 with the HUD laid out for the shape |
| Frame rate | 30 to 200 fps; above ~63 fps is beta |
| Frame generation | Own interpolation, 2x / 3x / 4x, any GPU (experimental) |
| Keyboard and mouse | Working, rebindable, native mouse camera |
| Controllers | XInput and SDL (Xbox, PlayStation, Switch Pro and others), matching button prompts |
| Gyro / motion controls | Not implemented |
| Game language | English (US) only for now |
| amiibo | Not implemented |
| Mods | RomFS mods (TKMM output or emulator-style folders); code patches need a rebuild |
| Launcher and installer | Working: builds from your dump, updates itself from GitHub |
| Windows | Supported (10/11 x64) |
| Linux / Steam Deck | Not supported (separate effort) |

---

## What works

### Running the game

- **Static recompilation of TOTK 1.4.3.** The whole game executable is translated to C once and compiled with
  clang, so the game's own code runs natively.
- **Switch services** the game needs, written for Windows: file system (RomFS, saves, cache storage), threads and
  synchronization, memory (an 8 GB guest address window, regions above 4 GB committed as the game makes them),
  audio renderer, input, video playback (Media Foundation), display and timing.
- **The game's graphics API (NVN) natively**, translated to WebGPU. The game's shaders are translated (Maxwell to
  WGSL) as it uses them.
- **Saves** in the install's `user/save`, or in a folder you choose (see the launcher's [Save folder](#the-launcher)).

### Graphics and upscaling

- **FSR 2.2** (AMD's passes converted to WGSL) in place of the game's own scale-up: motion vectors from depth and
  the game's camera, jitter, and a reactive mask from the G-buffer so Link doesn't ghost.
- **NVIDIA DLSS Super Resolution** in FSR 2's place on RTX cards (needs the "DLSS library" build step). You can
  pick the DLSS model: NVIDIA's default, K, M, L or J.
- **FSR 1** (EASU + RCAS) when the final picture is enlarged to the window.
- **3D resolution modes:** Game's own, Quality, Balanced, Performance, Ultra Performance, Native (DLAA). The game's
  own dynamic resolution is turned off so the 3D resolution stays fixed.
- **Output resolution** above the Switch's 1920x1080: 2560x1440, 3840x2160 or the display's size. The game's
  texture heap and memory grow to fit.
- **Aspect ratios:** 16:9 (the game's own), the display's shape, 16:10, 21:9, 32:9. The game's cameras take the
  shape, so wider screens see more of the world, and the HUD layouts are rebuilt for the shape (HUD in the corners,
  or centered).
- **Anisotropic filtering** 2x to 16x.
- **Shadow resolution and distance** (new, 1.4.3, at the next start): the sun's shadow map at a fixed 1024, 1536 or
  2048 instead of the game's own (up to 960, and smaller near the ground, which makes leaf shadows blocky and jumpy),
  and the sharp shadow cascades reaching x1.5, x2 or x3 as far as the game's 20 / 40 / 80 / 160 m. Adapted from
  totk-pc-recomp (MIT).
- **Water reflections** (new, live): the world mirrored on the water. The game's own water shows a glow under a lit
  pillar but no mirror image of it (Zora's Domain). Screen-space reflections drawn into the lit scene after the
  game's lighting: each water pixel's reflected ray is traced through the depth buffer and mixed in by Fresnel.
  Natural keeps the water's ripples in the reflection, Clear fewer of them. Seen working at Zora's Domain and on a
  river. Only what is on screen can be reflected: reflections fade at the picture's edges, and Link's reflection
  shows his back.
- **Motion blur** (new, live, with FSR 2 or DLSS): the camera's motion blur, from the per-pixel motion vectors made
  for FSR 2 / DLSS, applied as the upscaled scene goes into the game's frame, so the HUD stays sharp. Low / Medium /
  High (a quarter, half or all of a frame's motion); none on a camera cut or teleport.
  - **Per-object** (new): Link, his sword swings and the other characters blur with their own motion. Each frame the
    draws that moved are drawn a second time with last frame's bones and transforms, so every pixel knows where it
    was. (The first build drew most of the world a second time and leaked video memory: 15-30 fps and a crash after
    7 minutes. Fixed in the next commit; not confirmed in game yet.) A reconstruction filter (McGuire et al. 2012) lets a swing smear over what is behind it
    instead of only inside its outline. The same motion goes into DLSS / FSR 2's motion vectors. Characters the game
    animates some other way stay sharp, as before.
- **Surface reflections** (new, live): the same on smooth surfaces (polished stone, ice, metal), picked by the
  smoothness the game keeps in its G-buffer. Soft: blurrier on rougher surfaces. Sharp: mirrors on every smooth
  surface. Characters are left out.
- **Colour textures compressed on the GPU:** ASTC textures are decoded on the GPU and re-encoded to BC7, a quarter
  of the video memory of RGBA8.
- **VRAM management:** unused textures and render targets are given back near Windows' video memory budget (this
  fixed the fps drop after menus).

### Frame rate, pacing and latency

- **Frame rate limit** 30 to 200 fps, or the display's refresh rate. The game steps by real frame time, so it
  runs at the right speed at any frame rate. Fixes for fades, hair and cloth, the equipment-switch cooldown and
  trampolines above 30 fps.
- **Frame timing:** Smooth (the step averages the last 5 frames) or Responsive (the last frame; better with
  G-Sync / FreeSync).
- **VSync:** On, Off (no tearing), Off (tearing). With VSync off the game can go past a 60 Hz screen's rate. If the
  window keeps going out of date with tearing (seen with a system-wide ReShade), it falls back to no tearing by
  itself.
- **NVIDIA Reflex** On / On + Boost (VK_NV_low_latency2).
- **Frame generation (frame interpolation):** the project's own, on any GPU. 2x, 3x or 4x. Each made pixel uses
  block matching on the images, the camera's motion vectors (with FSR 2 or DLSS on) or no motion (the HUD). With
  VSync on, made frames are paced to the display; with VSync off, the frames shown can go above the display's
  refresh rate. It does not lower input lag. It adds about one game frame of delay, which is noticeable when the
  base frame rate is low. It costs GPU time, so the game's own frame rate drops a little when it is on (the motion
  search keeps its search area in workgroup memory since 2026-11-08: the same result for far fewer texture reads).
- **FPS counter:** FPS, or FPS + frame time and the longest gap. With frame generation: the frames shown and the
  game's own, e.g. "82 FPS (41 base)".

### Shader stutter

- **Pipeline cache** (`rxcache/pipelines.bin`): every pipeline the game used is remembered and rebuilt while the
  title screen shows, the most recently used first (up to 8000 by default). It is compacted instead of deleted when
  it grows large.
- **SPIR-V cache** (`rxcache/spirv.bin`): the compiled form of each shader stage is kept, so later launches skip the
  WGSL compile (needs the "DLSS library" build step's WebGPU library).
- A shader seen for the first time is still translated during play, so new areas can hitch the first time.

### Controls

- **Keyboard and mouse** with every action rebindable (F1), mouse sensitivity, invert Y, walk speed.
- **Native mouse camera:** the mouse drives the game's own camera input. Ultrahand holding, aiming cameras and the
  ability wheel are handled.
- **Controllers** through XInput and SDL (Xbox, DualSense, DualShock 4, Switch Pro and more), mapped by button
  position.
- **Button prompts that match your device:** keyboard (showing your own bindings), Xbox, PlayStation or Switch,
  automatically or fixed.

### Mods

- **RomFS mods:** point the launcher at TKMM's output folder, or a folder of emulator-style mods (each with a
  `romfs/`). Several mods load in name order; a later folder wins.
- `.pchtxt` / `.ips` **data** patches apply. Patches to the game's **code** need the code translated again with
  the mods (see [Mods](#mods)).

### The launcher

`SkyboundTOTK Launcher.exe` does everything from one window:

- **Build** from your own dump: the base game and 1.4.3 update `.nsp` files (or gamecard `.xci`) with your own
  `prod.keys`, or already-extracted ExeFS and RomFS folders. Steps already done are skipped, and a stopped build
  carries on where it left off.
- **Installs what the build needs, without admin:** Python (if the PC has none), clang (emsdk's LLVM), Microsoft's C++
  Build Tools (after you accept Microsoft's license in the launcher) and, for DLSS, Rust. Everything stays in the
  install folder.
- **Updates itself:** it checks this branch on GitHub at start. **Update and build** downloads the new source and
  rebuilds only what changed. Your game files, saves and mods are not touched.
- **Settings:** upscaler, sharpness, display mode, window size, VSync, output resolution, aspect ratio, 3D
  resolution, DLSS model, frame rate limit, frame timing, FPS counter, anisotropic filtering, shadow resolution and
  distance, water and surface reflections, motion blur, Reflex, button icons,
  mouse camera and sensitivity, mods folder, save folder, log file.
- **Save folder:** pick a folder that OneDrive or Google Drive syncs to share saves between PCs. The first time, the
  install's saves are copied into it (the originals stay), unless it already has saves, which are then used.
  Settings stay per PC. Don't play on two PCs at once with the same save folder.
- `launcher.ini` next to it holds the launcher's settings. `env.NAME = value` lines there pass environment variables
  to the game (debugging options).

### Debugging tools (for bug hunting)

- **F12 frame dump:** every render target of the next frame as PNG, and every draw in the log.
- **Pixel probe** (`RX_PIXEL`): which draw wrote a pixel.
- **Draw capture** (`RX_DUMP_ATTRS` + `RX_CHECK_ATTRS`): a draw's constants, vertex data and index list, checked
  against the game's memory and saved to a file so its vertex shader can be replayed off the PC.
- Memory, VRAM, stall and pipeline logs; hardware data breakpoints on game addresses; a sampling profiler (F5).

---

## Known issues

Tested mainly on an RTX 5080 (1080p, 75 Hz) and an RTX 3060 Ti (2560x1600, 165 Hz). Other GPUs and AMD / Intel cards
are less tested.

| Issue | Status | Details |
| --- | --- | --- |
| Orange sky / giant orange triangles over the screen | **Fixed (Nov 5); needs testing** | A mesh drawn with a base vertex that puts every vertex past its vertex data. The Switch reads zeros there and draws nothing; we read the memory after it (the mesh's normals and texture coordinates as positions). Draws like that are now copied with zeros past the end. Confirmed by replaying the captured draw. The "randomly stretched terrain" reports may be the same bug; retest. |
| Depths gloom | **Fixed** (a small brightness flicker remains) | It was a flat sheet: the game draws its noise volumes once at boot, before the renderer had started, and that work was dropped; the game's first GPU work now waits for the renderer. It also vanished for whole frames: the gloom pass marks the screen tiles that have gloom and the composite draws only those, but the renderer copied the pass's marks back before the pass had written them (a draw's storage writes were tied to whichever render pass was open while its resources were built). They now belong to the pass the draw is recorded in. Left: the gloom's brightness still pulses a little from frame to frame. |
| Fuse mode streaks | **Fix not ported yet** | Upstream (1.0.0) saw long green lines from Link's hand in Fuse mode and fixed them in its wgpu build (the shaders declare that they need infinity and NaN preserved). Its published wgpu patch uses a naga flag (`WriterFlags::PRESERVE_INF_NAN`) that naga 29 does not have, so this branch's DLSS wgpu build cannot take it as is; the plain `wgpu_native.dll` from upstream has it. Not checked on 1.4.3. |
| Sky flicker | **Open** | Reported, not investigated yet. |
| Dark ground patch (Hateno) | **Open** | Captured with F12, not investigated yet. |
| Incorrect terrain textures | **Reported** | Some areas show wrong terrain textures. |
| Shadows | **Reported** | Shadow shader problems reported. Three shader-decoder assertions appear at startup; their effect is unknown. |
| Weapon and shield wheel background | **Reported** | Can be black instead of blurred. |
| Zonai Portable Pot particles | **Workaround** | Disappearing-effect spikes greatly reduced; short stray lines remain. |
| Fused-weapon inventory icons | **Workaround** | Icons rendered transparent; a cache workaround redraws them. |
| Over the VRAM budget | **Open** | On an 8 GB card at 2560x1600 with frame generation, reflections and higher shadows, a busy place (Kakariko Village) went over Windows' budget for the game (~6.5 GB of 8 GB with other programs open): textures moved to system memory and the game's GPU time went from ~7 to 31-38 ms a frame (13-21 fps). Unused textures are given back near the budget, but there was little left to give. Close GPU-heavy programs (browsers, recorders), or lower the 3D resolution or turn frame generation off. |
| Crashes | **Rare, open** | One crash on a loading screen (no log). Two GPU device losses, both with frame generation on and VSync off: after ~16 minutes at 3x, and on a loading screen at 2x while the window's surface kept going out of date (2026-11-08). Cause unknown; later runs with VSync off did not crash (2026-11-08). |
| Above ~63 fps | **Beta** | As in other ports: the camera can turn faster and Link may fail to climb steps. |
| Above 200 fps | **Capped** | The game's own audio mix fades to silence at 230+ fps; the cause in the game isn't found yet, so 200 is the limit. |
| Frame generation latency | **By design** | Adds about one game frame of delay. On slower GPUs (low base fps) it feels laggy. |
| Startup "Not Responding" | **Minor** | Windows can mark the window not responding for a few seconds while the pipeline cache warms up. Wait it out. |
| Code mods | **Limitation** | `.ips` / exefs code patches are left out unless the code is translated again with the mods. |

---

## Not done yet / roadmap

**Needs doing (in this project):**

- [ ] Verify a full playthrough on 1.4.3, start to credits.
- [ ] Fix the open graphics issues above (sky flicker, terrain textures, shadows, the wheel
      background).
- [ ] Find why audio fades above 200 fps, and fix climbing steps at high frame rates.
- [ ] Port the remaining 1.0.0-only optimizations to 1.4.3: native frustum culling (~5% of the busiest frames on
      1.0.0) and the job-pool core count.
- [ ] Game language selection (fixed to English (US)).
- [ ] BC5 / better compression for normal maps (the GPU BC7 encoder is colour-only, so normal maps stay RGBA8).
- [ ] Cache shader translations across launches (first-time shaders are still translated during play).
- [ ] A frame replay tool: record a frame's GPU work and replay it off the PC, to test graphics fixes without the
      game.
- [ ] amiibo.

**Left to others / separate efforts:**

- [ ] Linux and Steam Deck builds.
- [ ] Gyro and motion controls.
- [ ] Textures prepared at build time.
- [ ] Other game languages' testing.

---

## Download and play

1. Get the zip from [Releases](https://github.com/xellow78-lgtm/SkyboundTotk/releases). It holds the launcher, its
   build script and a readme: nothing of the game.
2. Extract it to a drive with **at least 45 GB free** (an SSD loads much faster and stutters less while exploring).
3. Start `SkyboundTOTK Launcher.exe`. On the **Build** page, choose your base game and 1.4.3 update (`.nsp` or
   `.xci`) and your `prod.keys`, or your extracted ExeFS and RomFS folders. Tick **DLSS library** if you have an
   RTX card (it also enables Reflex and the SPIR-V cache).
4. Click **Build**. The first build downloads its tools first; it took about 15 minutes on a fast PC and takes
   longer on slower ones.
5. Set things up on **Settings**, then **Play**.

To update later: click **Update and build** when the launcher says an update is available.

---

## Keys and settings

| Key | Action |
| --- | --- |
| F1 | Settings menu in the game (graphics, controls, bindings) |
| F4 | Cycle 3D resolution modes |
| F7 | Frame generation on / off |
| F8 | FSR 1 on / off |
| F9 | FSR sharpening |
| F10 | Keep the picture's shape / stretch to the window |
| F11 | Fullscreen / windowed |
| F12 | Frame dump of the next frame (for bug reports) |
| Esc | Free the mouse (click the window to capture it again) |

**Default keyboard controls:** WASD move, IJKL camera (or the mouse), Space jump, E action, Shift or Q run / cancel,
F or left click attack, Z / R = L / R, C or right click = ZL, V = ZR, Ctrl crouch, middle click = right-stick click,
Alt walk, Enter = Plus, Tab = Minus, arrow keys = D-pad. `-` / `=` change mouse sensitivity. Everything can be
rebound with F1.

The game's own settings are saved in `user/input.ini`; display toggles in `user/present.txt`. Some useful
environment variables (in `launcher.ini` as `env.NAME = value`):

| Variable | What it does |
| --- | --- |
| `RX_NO_SPIRV_CACHE=1` | Turn the SPIR-V cache off (try this first if a scene draws wrong after an update) |
| `RX_NO_PIPELINE_CACHE=1` | Neither read nor write the pipeline cache |
| `RX_ASTC_BC=0` / `2` | No BC7 encoding / also linear textures |
| `HOS_OUTPUT_RES=<w>x<h>` | Override the output resolution |
| `HOS_ASPECT=<w>:<h>` | Override the aspect ratio |
| `HOS_HUD=center` | Keep the HUD in the middle 16:9 on wide screens |
| `RX_FRAMEGEN_NO_CAMERA=1` | Frame generation with block matching only |
| `RX_GPU_TIMING=1` | GPU time of the game's passes and of each frame generation stage, in the log every 5 s |
| `RX_OBJ_MV=0` / `1` | Per-object motion off / on even without motion blur |
| `RX_OBJ_MV_DEBUG=1` | Show the per-object motion as colour instead of the blur |
| `RX_OBJ_MV_STATS=1` | What per-object motion followed and redrew, in the log every 5 s |
| `RX_OBJ_MV_RIGID=1` | Per-object motion also follows draws without bones (slower: off by default) |
| `RX_OBJ_MV_ALL=1` | With `RX_OBJ_MV_RIGID=1`: redraw everything whose constants changed (the first build's way: slow) |
| Ctrl+F5 in game | 8 s without the renderer's draws (the picture freezes or goes wrong), then the frame rate the game's own threads gave in the log: the ceiling a faster renderer could reach |
| F5 in game | 15 s of the sampling profiler, every thread with each one's load; F5 again: the renderer's replay thread with whole call stacks (`HOS_PROFILE_F5_THREAD=<name>` for another) |
| `RX_DUMP_DIR=<folder>` | Where F12 frame dumps go (default `rx_frame`) |

---

## Mods

- Set the launcher's **Mods folder** to TKMM's output folder, or to a folder holding emulator-style mods (each mod
  a folder with `romfs/` and/or `exefs/`). They load in name order, so `zz-My Mod` wins over `merged`.
- RomFS files and `.pchtxt` / `.ips` data patches apply at start; the log lists what was used.
- **Code patches** (patches to `main`) are left out because the code is compiled ahead of time. The log prints the
  command to translate the code again with your mods folder, then build again.

---

## Reporting a bug

1. Note what happened, where (map coordinates help), your GPU, driver and settings.
2. While the problem is on screen, press **F12** once and wait a few seconds (the game hitches while it saves).
3. Close the game **without pressing Play again**: the launcher overwrites the log file at each start.
4. Send the log (the launcher's **Open the log**) and, if asked, the F12 folder (`rx_frame` or `RX_DUMP_DIR`).

The F12 folder and the `rxcache` files contain data made from your game: share them privately, never publicly.

---

## Building from source

The launcher is the easy way (it installs everything). To build by hand:

**Requirements:** Windows 10/11 x64; a GPU supported by wgpu-native (Vulkan); 45 GB free on top of the game dump;
Python 3.10+ with `lz4` (`python -m pip install lz4`); Visual Studio 2022 Build Tools (MSVC C++ and Windows SDK);
clang, clang++ and lld (emsdk 6.0.9's LLVM: set `EMSDK`, default `C:\emsdk`, or put them on PATH).

1. Put your extracted game in `game/` as `game/exefs/main`, `game/exefs/sdk`... and `game/romfs/...` (not
   `game/exefs/exefs/main`). For 1.4.3, the RomFS is the base game's with the update applied
   (`python a64-kit/tools/nx.py romfs <base.nsp> <out> --update <update.nsp>`), and `SKYBOUND_EXPERIMENTAL=1` must
   be set.
2. In an **x64 Native Tools Command Prompt**, run `build.bat` in the SkyboundTOTK folder. It checks the `main` and
   `sdk` build IDs and a few RomFS folders (not a full integrity check), translates the code, compiles it and puts
   the game in `dist/SkyboundTOTK/`.
3. Run `dist/SkyboundTOTK/SkyboundTOTK.exe` (or the launcher next to it). Keep `image.bin`, `image.json`, `romfs/`,
   `wgpu_native.dll` and `SDL2.dll` beside the exe.

`SKYBOUND_JOBS=<n>` limits parallel compiles if you run out of memory. A completed translation is reused while its
inputs match. If one was interrupted, move `build/` aside and retry. The script won't overwrite a non-empty
`dist/SkyboundTOTK/`: move it aside first and keep its `user/` folder (your saves and settings).

**Troubleshooting:** Python or `lz4` missing → install them for the Python on PATH. Headers or libraries missing →
use the x64 Native Tools prompt and check the C++ tools, Windows SDK, clang and lld. "Wrong module" → check the
folder layout and version. DLSS starts as FSR 2 → build with **DLSS library** ticked (the WebGPU library has to be
rebuilt when it changes).

### Why WebGPU?

One renderer and shader backend (WGSL) serves native wgpu and browser WebGPU experiments, instead of separate
graphics backends. On Windows, wgpu-native runs it on Vulkan; the desktop game does not run in a browser. The cost is
some translation and API overhead that still needs optimizing.

---

## Game files and legal

You must supply your own legally obtained copy of the game: the base game and the 1.4.3 update, dumped from your own
Switch, with your own keys. **No game files, generated game code, keys, firmware or saves are included or
downloaded.**

Keep your game files, the generated code, the built game, its caches (`rxcache/`), frame dumps and saves private. The
`game/`, `build/` and `dist/` folders are Git-ignored, but check what you publish rather than relying on that. A
built game contains copyrighted, game-derived data and must not be shared.

## Source and licenses

NotRexed's original project code is offered under the [MIT License](LICENSE). The AArch64 lifting/runtime and renderer
sources originated in NotRexed's Rexcomp-based TOTKPort; unrelated Rexcomp kits and applications are not included.

**The whole repository and combined build are not MIT-only.** The AArch64 kit includes GPL-licensed suyu/yuzu
components and other third-party code under their own licenses (see [NOTICE.md](NOTICE.md) and the license files).
Several PC features use findings and patch sites from totk-pc-recomp and MacNativeGames (both MIT), credited in the
files that use them. MIT does not grant rights to the game. Before publishing, verify that every contributor whose code
is offered under MIT has agreed, and review the source and binary distribution obligations.
