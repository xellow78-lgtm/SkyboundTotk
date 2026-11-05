# SkyboundTOTK

SkyboundTOTK is an experimental Windows x64 static recompilation project for *The Legend of Zelda: Tears of the Kingdom*, **base game version 1.0.0**. It is not affiliated with Nintendo. Expect graphical problems and incomplete compatibility; a successful build does not guarantee a complete playthrough.

The project's AArch64 game code is translated ahead of time into C and compiled locally into a native executable. A runtime implements the services the game expects, and the renderer translates its graphics work and shaders for WebGPU. This is a game-specific port, not a general-purpose Switch emulator. Some graphics translation still happens at runtime.

## Game files and distribution

You must supply your own legally obtained, already-extracted, **unmodified 1.0.0 base-game ExeFS and RomFS**. No game files, generated game code, keys, firmware or saves are included in this source project. The build does not extract NSP or XCI files, and other versions or updates are not supported by the game-specific hooks.

Keep your game files, generated code, executable package and saves private. The `game/`, `build/` and `dist/` directories are Git-ignored, but check what you are publishing rather than relying on ignore rules alone. The generated standalone package contains copyrighted game-derived data and must not be shared or committed.

## Build on Windows

### Requirements

- Windows 10/11 x64 and a GPU and driver supported by wgpu-native. Allow **at least 45 GB free in addition to the original game dump** for intermediate files and the separate packaged RomFS; the build is CPU- and memory-intensive.
- Python 3.10 or newer on PATH, with `lz4` installed: `python -m pip install lz4`.
- Visual Studio 2022 Build Tools with the MSVC C++ tools and Windows SDK. Use an **x64 Native Tools Command Prompt** so the compiler can find the Windows headers and libraries.
- Clang, clang++ and lld. The build previously used emsdk 6.0.9: install it separately and set `EMSDK` to its directory (the lookup defaults to `C:\emsdk`), or put these LLVM tools on PATH. The build script does not download compilers or Python packages.

### Prepare and build

1. Put the extracted files in the project's `game` folder. The paths must look like this, not `game/exefs/exefs/main` or `game/romfs/romfs/Event`:

   ```text
   SkyboundTOTK/
     build.bat
     game/
       exefs/
         main
         main.npdm
         rtld
         sdk
         ...
       romfs/
         ActorSystem/
         Event/
         ...
   ```

2. Open an **x64 Native Tools Command Prompt**, change to the SkyboundTOTK directory and run `build.bat`. The launcher pauses at the end so you can read any errors.
3. The script checks the `main` and `sdk` module build IDs and a few RomFS directories, lifts the game code, compiles the native executable and copies the runtime and RomFS into `dist/SkyboundTOTK/`. The checks do **not** verify the entire RomFS or guarantee that a dump is complete.
4. Open `dist/SkyboundTOTK/SkyboundTOTK.exe` to run it. No command-line arguments are needed. Keep `image.bin`, `image.json`, `romfs/`, `wgpu_native.dll` and `SDL2.dll` alongside the executable. The standalone folder can be moved as a unit and does not need the original `game/` folder to launch.

The first lift and compile can take a long time. The builder reuses a completed lift only when its recorded game and lifter inputs match. If the lift was interrupted or those inputs change, move the existing `build/` directory aside before retrying. The script refuses to overwrite a nonempty `dist/SkyboundTOTK/`: move the existing package aside before a new build. Copy its `user/` folder into the new package if you want to retain its saves and settings. Do not delete your only save backup.

The package normally creates its own `user/` directory beside the executable; it does not automatically use the saves from another installation. Native renderer pipeline cache data is kept in `rxcache/` beside the executable. The executable also accepts an image path and `--romfs` / `--user` overrides for advanced use, but they are not needed for normal launching.

### The launcher

After the first build, `dist/SkyboundTOTK/SkyboundTOTK Launcher.exe` does the rest from one window:

- **Build** takes your game files, either the extracted ExeFS and RomFS folders (the RomFS is linked, not copied) or the base game and 1.4.3 update `.nsp` files with your own `prod.keys`. It then runs the build steps with their progress: game files, source changes, build tools, the optional DLSS library, translating the game's code, compiling and installing. Steps that are already done are skipped, and a stopped build continues where it left off. The build itself is `launcher/pipeline.py`, and its log is `launcher_build.log` in the game folder.
- **Settings** sets the upscaler (off, FSR 2 or DLSS), FSR 2 sharpness, fullscreen and window size. It also changes the game's own settings that F1 changes (3D resolution, anisotropic filtering, NVIDIA Reflex, button icons, the mouse), the mods folder and the log file. The launcher's own settings are in `launcher.ini` next to it. `env.NAME = value` lines there add environment variables for the game.
- **Play** starts the game with those settings and writes what it prints to the log file.
- **Updates:** at start the launcher asks GitHub for the newest commit of `update_repo` (`xellow78-lgtm/SkyboundTotk`) on `update_branch` (`update-1.4.3`); both can be changed in `launcher.ini`, and `update_check = 0` turns the check off. When there is a newer one, **Update and build** downloads that commit as a zip (no git needed), writes the changed files over the source, removes files the previous update brought that are gone, and runs the build, which redoes only what changed. Your game files, build folder, game folder, saves and mods are not touched. The installed version is in `.skybound_version` in the source folder.

The launcher needs the same tools as `build.bat` (Python, Visual Studio's C++ tools, clang from emsdk). Python packages it lacks are installed. It does not install compilers.

### Build and launch troubleshooting

- **Python or `lz4` missing:** install them for the Python on PATH, then reopen the tools prompt. Run `python -m pip install lz4` with that same Python.
- **Compiler, headers or libraries missing:** open the x64 Native Tools prompt and check that the C++ tools, Windows SDK, Clang, clang++ and lld are installed and discoverable. Set `EMSDK` if the LLVM tools are in a non-default emsdk directory.
- **Wrong module or missing RomFS:** check the extracted folder layout and version. A base-game ID match is not a complete integrity check.
- **Existing output or stale/incomplete lift:** move the indicated package or build directory aside, preserving `user/` first, then retry. An interrupted copy can also leave a nonempty package that needs to be moved aside.
- **First launch or new scene stutters:** shader translation and pipeline compilation may happen as scenes are encountered. The native pipeline cache helps prepare previously encountered pipelines on later launches; it cannot guarantee stutter-free play.
- **Crash or graphics issue:** note the game version, hardware, driver, location and settings. The standalone startup test initialized the renderer and loaded the packaged RomFS, but did not verify full gameplay or graphics correctness.

## Controls and display features

Focus the game window before using the keys. The renderer briefly reports display settings in the window title; changes are saved in `user/present.txt` for the next launch.

| Key | Action |
| --- | --- |
| F7 | Toggle experimental frame interpolation. |
| F8 | Toggle FSR 1 upscaling. |
| F9 | Cycle FSR sharpening. |
| F10 | Toggle aspect-preserving presentation versus stretching the image to fill the window. |
| F11 | Toggle fullscreen/windowed mode. |

**FSR:** The current implementation is FidelityFX Super Resolution **1**, with EASU upscaling and RCAS sharpening in the presentation pass. It is enabled by default and is used when the presented frame is enlarged; otherwise the renderer uses the normal sampling path. FSR 1 is spatial upscaling, not FSR 2/3 temporal upscaling or AMD frame generation, and does not increase the game's internal render resolution. F9 cycles sharpening from sharper to softer values.

**Output resolution (TOTK 1.4.3):** `output_resolution` in `user/input.ini` (F1 > Graphics, or the launcher) makes the game compose its frame and HUD at 2560x1440, 3840x2160, or the largest 16:9 size that fits the main display, instead of 1920x1080. `HOS_OUTPUT_RES=<w>x<h>` overrides it. It takes effect at the next start. The 3D scene follows at the game's own 5/6 of that size, or at the full size with the **Native (DLAA)** 3D resolution chosen before starting. The game's texture heap and memory grow to fit, using the sizes from totk-pc-recomp (MIT), whose patch sites come from MacNativeGames (MIT). Without Python and its packages (the heap table is rewritten by `tools/button_icons.py`) the game keeps 1920x1080. The output stays 16:9; other picture shapes are the aspect ratio setting below. Bigger sizes need more video memory.

**Frame rate (TOTK 1.4.3):** the game steps by the time its frames really take, so it runs at the right speed at any frame rate. `frame_rate_limit` in `user/input.ini` (F1 > Graphics, or the launcher; it changes live) caps it at 30 to 240 fps, or at the display's refresh rate with 0. The default is 60. Above 30 fps, fades, hair and cloth, the equipment-switch cooldown and trampolines are fixed for the faster frames (`hos/totk_highfps.c`, from totk-pc-recomp's and MacNativeGames' patch lists, both MIT). Above about 63 fps it is beta: as in other ports, the camera may turn faster and Link may not climb steps. `HOS_STEP_FRAMES=1..8` sets how many frame times the step averages (5).

**Texture memory:** colour (sRGB) ASTC textures are decoded on the GPU and encoded again to BC7 there, so they take a quarter of the video memory RGBA8 took (`gpu/render/rx_bc7_gpu.wgsl.h`; the log line `ASTC to BC7 on the GPU` counts the textures and the memory saved). The encoder uses BC7 mode 6 only, which fits each 4x4 block to one colour line: fine for colour, poor for bumpy normal maps, so linear ASTC textures (normals, masks) stay RGBA8. `RX_ASTC_BC=0` turns it off, `RX_ASTC_BC=2` includes linear textures, `RX_ASTC_BC_ARRAYS=0` leaves texture arrays as RGBA8.

**Frame interpolation:** This is the project's own experimental image-based optical-flow interpolation, not AMD FSR frame generation. F7 is off by default and inserts estimated frames between real frames, normally targeting twice the displayed frame rate when the hardware can keep up. It does not make the game simulate more frames or reduce input latency; the display runs about one game frame behind. UI and fast motion can smear or distort, and long frame gaps are not interpolated.

**Aspect ratio (ultrawide and 16:10):** `aspect_ratio` in `user/input.ini` (F1 > Graphics, or the launcher; at the next start): 16:9 (the game's own, the default), the display's shape, 16:10, 21:9 or 32:9. `HOS_ASPECT=<w>:<h>` (or `<w>x<h>`, or a ratio) overrides it. The game's cameras take that shape (`hos/totk_ultrawide.c`: four sites on 1.4.3, from MacNativeGames, MIT), so a wider screen shows more of the world at the sides and a 16:10 one keeps the vertical view; the picture is shown at that shape, with black bars only if the window is another shape. The HUD's layouts are made for the shape at the start by `tools/button_icons.py --layout` (pane lists from MacNativeGames, MIT, checked there on 1.4.3), from your own game files or your mod's: the HUD keeps its proportions, with its parts in the screen's corners (`HOS_HUD=center` keeps them in the middle 16:9). The game's frame takes the shape too (`hos/totk_res.c`): 16:10 keeps the output's width (2560x1440 becomes 2560x1600), wider shapes keep its height (3440x1440 at 21:9), and "Display" uses the display's own size. The 3D scene follows the same shape, so nothing is stretched. If the game's heap table cannot be grown, the output stays 1920x1080 and that 16:9 frame is stretched to the shape instead. Markers that follow things in the world (enemy and quest markers) may sit a little off.

**FPS counter:** `fps_counter` in `user/input.ini` (F1 > Graphics, or the launcher; it changes live): off, FPS, or FPS with the average frame time and the longest gap of the last half second, in the top left corner. It counts the frames shown (with frame interpolation, interpolated ones too).

Keyboard and mouse controls are experimental. Press **F1** in the game to rebind any key or mouse button (and the wheel), set mouse sensitivity, invert Y and walk speed; the bindings are saved to `user/input.ini`. Defaults: WASD moves, IJKL controls the camera, Space jumps, E is action, Shift or Q is run/cancel, F or left click attacks, Z/R are L/R, C or right click is ZL, V is ZR, Ctrl crouches, middle click is the right-stick click, Alt walks, Enter is Plus, Tab is Minus and the arrow keys are the D-pad. Click the window to capture the mouse, Esc to release it, and use `-` / `=` to adjust sensitivity. The mouse turns the camera as the right stick would (by its speed). XInput and SDL-supported controllers are also handled; buttons map by physical position.

### Why WebGPU?

The renderer targets the WebGPU API and WGSL so the same general renderer and shader backend can be used by native wgpu and by browser WebGPU experiments rather than maintaining wholly separate graphics implementations. On Windows, wgpu-native translates this API to an available native backend, such as Vulkan or Direct3D 12; choosing WebGPU does not mean the desktop game runs inside a browser. It also has portability and validation tradeoffs, and translation, pipeline compilation and API overhead still require optimization. The root `build.bat` described here builds the **Windows native** package; browser functionality and packaging are not promised or validated by that launcher.

## Known issues and limitations

These are reports and investigation notes, not a complete compatibility list. Some were observed in earlier TOTKPort builds rather than independently reproduced in standalone Skybound.

| Issue | Status | Details |
| --- | --- | --- |
| Shadows / shaders | **Reported** | Shadow shader problems have been reported. The standalone startup log also showed three shader-decoder assertions; their visual effect and relationship to the shadow issue are unknown. |
| Stretched terrain | **Reported** | Terrain can appear randomly stretched; cause and reproducibility need investigation. |
| Incorrect terrain textures | **Reported** | Some areas have wrong terrain textures; locations and causes need investigation. |
| Weapon and shield wheel background | **Reported** | The background can be black instead of blurred. An ability-wheel workaround was recorded earlier, but the weapon/shield behavior has not been verified fixed in standalone Skybound. |
| Aspect ratio | **New; needs testing** | The old F6 ultrawide stretched the view on 1.4.3: only the two viewport sites were hooked, and MacNativeGames found the same stretched result until the camera's own update and setup sites were patched too. Those are in now; 16:10 and 21:9 need a report. |
| Zonai Portable Pot particles | **Workaround; retest** | A workaround greatly reduced disappearing-effect spikes in an earlier build, but short stray lines remained and the underlying cause is unknown. Retest in Skybound. |
| Depths gloom | **Earlier fix; retest** | Missing or incorrect ground gloom was reported on the path to Ganon. Earlier notes record a fix for a reproduced case; standalone Skybound and other locations need verification. |
| Fused-weapon inventory icons | **Workaround; retest** | Black icons were seen in earlier testing. A cache-retry workaround was recorded, but the underlying timing issue remains unexplained; retest in Skybound. |
| Browser new-game loading | **Previously reported; retest** | Earlier web experiments had an intermittent loading-screen hang. This is not a verified Windows-launcher issue; the root build does not package a browser release. |
| Browser interpolation judder | **Earlier fix; retest** | Earlier web experiments had interpolation judder; notes record a pacing fix. Browser behavior is not validated by the Windows launcher. |
| Performance and compatibility | **Ongoing limitation** | Frame pacing, shader warm-up and rendering vary by scene and hardware. A build and startup test are not a full-game compatibility test. |

## To-do

- [ ] Add support for other TOTK versions beyond the 1.0.0 base game.
- [ ] Fix all current issues and bugs.

## Source and licenses

NotRexed's original project code is offered under the [MIT License](LICENSE). The required AArch64 lifting/runtime and renderer sources originated in NotRexed's Rexcomp-based TOTKPort; unrelated Rexcomp kits and applications are not included.

**The whole repository and combined build are not MIT-only.** The AArch64 kit includes GPL-licensed suyu/yuzu components and other third-party code under their own licenses. Their licenses and applicable GPL obligations still apply; see [NOTICE.md](NOTICE.md) and the individual license files. MIT does not grant rights to the game. Before publishing, verify that every contributor whose code is being offered under MIT has agreed, and review the applicable source and binary distribution obligations.
