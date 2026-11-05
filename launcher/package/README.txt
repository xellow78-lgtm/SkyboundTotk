SkyboundTOTK - Tears of the Kingdom on PC, built on your PC from your own game files
=====================================================================================

Nothing of the game is in this download. You need your own copy of The Legend of Zelda: Tears of the Kingdom with
the 1.4.3 update, dumped from your own Switch as ExeFS and RomFS folders (or the base game and update .nsp files with
your prod.keys). Everything made from it is made on your PC, for your own use: do not share it.

1. Put this folder on a drive with plenty of room (tens of GB: the tools, the build and the game).
2. Start "SkyboundTOTK Launcher.exe" and open the Build page.
3. Choose your ExeFS and RomFS folders (or the .nsp files and keys). Tick "Make the DLSS library" for DLSS and Reflex
   (NVIDIA RTX; adds Rust and ~20-30 minutes once).
4. Click BUILD. The first build downloads what it needs into this folder, nothing system-wide, no admin:
   - Python (if the PC has none), from python-build-standalone
   - the game's source, from GitHub
   - clang (emsdk's LLVM)
   - Microsoft's C++ Build Tools: the launcher shows Microsoft's license and asks you first
   The first build takes a long time (it translates and compiles all of the game's code); later ones only redo what
   changed.
5. Click Play. Updates: the Play page says when there is a new version; "Update and build" fetches and rebuilds it.

Folders made here: source (the source, the build, and the game in source\dist\SkyboundTOTK), python, emsdk, msvc,
rust and wgpu-build (with DLSS) and tmp.

Source, issues and license (MIT): https://github.com/xellow78-lgtm/SkyboundTotk
Not affiliated with or endorsed by Nintendo.
