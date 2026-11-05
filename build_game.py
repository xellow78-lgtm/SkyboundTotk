"""Build SkyboundTOTK from an already-extracted TOTK dump (1.0.0; 1.4.3 experimentally, see GAME_BUILDS)."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
GAME = ROOT / "game"
BUILD = ROOT / "build"
KIT = ROOT / "a64-kit"
WGPU = ROOT / "third_party" / "wgpu-native"
DIST = ROOT / "dist" / "SkyboundTOTK"
# The game builds this port knows, by their module IDs (ports/zelda-totk/versions.txt lists the same main IDs for
# a64-kit/build.py, which picks each build's hooks); this is not a full content-integrity check. An experimental
# build lifts and links, but not every hook is ported to it (ports/zelda-totk/versions.txt, hos/ GAME_VERSION):
# set SKYBOUND_EXPERIMENTAL=1 to build one.
GAME_BUILDS = {
    "1.0.0": {"main": "082ce09b06e33a123cb1e2770f5f9147709033db", "sdk": "b9046c31eb5d31271be970fe732d38df49c6aa21",
              "experimental": False},
    # 1.4.3: 30/60 fps pacing by its frame-rate controller, but no real-time frame steps, native frustum-cull,
    # ultrawide-HUD or title-credit hooks yet; RomFS = the base's with
    # the update applied (a64-kit/tools/nx.py romfs <base.nsp> <out> --update <update.nsp>)
    "1.4.3": {"main": "277178b7dba1b6d4949a84778d4abc58b31b34f5", "sdk": "574284b40c06cf1821a44857939083bee5716820",
              "experimental": True},
}


def fail(message):
    raise SystemExit("ERROR: " + message)


def module_id(path):
    with path.open("rb") as f:
        header = f.read(0x60)
    if len(header) < 0x60 or header[:4] != b"NSO0":
        fail(f"{path} is not an NSO module. Put your extracted TOTK ExeFS and RomFS in game/. See README.md.")
    return header[0x40:0x60].rstrip(b"\0").hex()


def check_game():
    """The extracted game's ExeFS and RomFS folders, and which known build it is."""
    exefs, romfs = GAME / "exefs", GAME / "romfs"
    for name in ("main", "sdk"):
        if not (exefs / name).is_file():
            fail(f"Missing {exefs / name}. Put your extracted TOTK ExeFS and RomFS in game/. See README.md.")
    ids = {name: module_id(exefs / name) for name in ("main", "sdk")}
    version = next((v for v, b in GAME_BUILDS.items() if b["main"] == ids["main"]), None)
    if version is None:
        fail(f"{exefs / 'main'} is not a TOTK build this port knows (main {ids['main']}). Known: "
             + ", ".join(GAME_BUILDS) + ".")
    if ids["sdk"] != GAME_BUILDS[version]["sdk"]:
        fail(f"{exefs / 'sdk'} is not TOTK {version}'s sdk module: main and sdk are from different dumps.")
    if GAME_BUILDS[version]["experimental"]:
        if os.environ.get("SKYBOUND_EXPERIMENTAL") != "1":
            fail(f"TOTK {version} support is experimental (not every hook is ported). Set SKYBOUND_EXPERIMENTAL=1 "
                 "to build it anyway.")
        print(f"WARNING: building TOTK {version}, which is experimental: see GAME_BUILDS in build_game.py.", flush=True)
    if not (romfs / "Event").is_dir() or not (romfs / "ActorSystem").is_dir():
        fail(f"{romfs} does not look like an extracted TOTK RomFS. See README.md.")
    return exefs, romfs, GAME_BUILDS[version]


def check_tools():
    if os.name != "nt":
        fail("This launcher builds the Windows x64 version; run it on Windows.")
    if sys.version_info < (3, 10):
        fail("Python 3.10 or newer is required.")
    try:
        import lz4.block
    except ImportError:
        fail(f"Python package lz4 is missing. Install it with: \"{sys.executable}\" -m pip install lz4")
    sys.path.insert(0, str(KIT / "tools"))
    import toolchain
    for tool in ("clang", "clang++", "lld"):
        toolchain.need(toolchain.find(tool))
    for path in (WGPU / "include/webgpu/webgpu.h", WGPU / "lib/wgpu_native.dll", WGPU / "lib/wgpu_native.dll.lib",
                 ROOT / "third_party/sdl2/SDL2.dll"):
        if not path.is_file():
            fail(f"Missing bundled wgpu-native file: {path}")
    # Fail before the lengthy lift if the compiler cannot build a native Windows program.
    run(sys.executable, KIT / "lift/build_emit.py")


def run(*args):
    print("\n==", " ".join(str(x) for x in args), flush=True)
    subprocess.run([str(x) for x in args], check=True, cwd=ROOT)


def lift_identity(exefs):
    digest = hashlib.sha256()
    for root in (KIT / "lift", KIT / "hos", KIT / "rt"):
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in (".py", ".c", ".cpp", ".h"):
                digest.update(path.relative_to(ROOT).as_posix().encode())
                digest.update(path.read_bytes())
    for path in (KIT / "build.py", KIT / "tools/nso.py", ROOT / "ports/zelda-totk/hot_funcs.txt",
                 ROOT / "ports/zelda-totk/versions.txt"):
        if path.is_file():
            digest.update(path.relative_to(ROOT).as_posix().encode())
            digest.update(path.read_bytes())
    for path in sorted(exefs.iterdir()):
        if path.is_file() and path.name in {"rtld", "main", "sdk"} | {f"subsdk{i}" for i in range(1, 10)}:
            digest.update(path.name.encode())
            with path.open("rb") as f:
                for chunk in iter(lambda: f.read(1024 * 1024), b""):
                    digest.update(chunk)
    return digest.hexdigest()


def package(romfs):
    native = BUILD / "totk/native"
    image = BUILD / "totk/image"
    DIST.mkdir(parents=True, exist_ok=True)
    # Refuse to silently replace a previous installation, including saves or mods.
    if any(DIST.iterdir()):
        fail(f"Standalone output {DIST} is not empty. Move it aside before rebuilding; keep its user folder.")
    for src, name in ((native / "totk.exe", "SkyboundTOTK.exe"),
                      (native / "wgpu_native.dll", "wgpu_native.dll"),
                      (ROOT / "third_party/sdl2/SDL2.dll", "SDL2.dll"),
                      (image / "image.bin", "image.bin"),
                      (image / "image.json", "image.json")):
        if not src.is_file():
            fail(f"Required build output is missing: {src}")
        shutil.copy2(src, DIST / name)
    # (the button icons for the device played with: the game runs this tool with that Python: hos/buttons.c)
    for name in ("button_icons.py", "python.txt"):
        if (native / name).is_file():
            shutil.copy2(native / name, DIST / name)
    print(f"Copying RomFS to {DIST / 'romfs'}; this may take several minutes.", flush=True)
    shutil.copytree(romfs, DIST / "romfs", symlinks=False)
    print(f"\nBuild complete. Open {DIST / 'SkyboundTOTK.exe'} to play.")


def main():
    exefs, romfs, game = check_game()
    check_tools()
    # A matching completed lift is reusable; changed inputs or an interrupted lift need a fresh directory.
    identity = lift_identity(exefs)
    stamp = BUILD / "totk/lift-input.sha256"
    image = BUILD / "totk/image/image.json"
    generated = BUILD / "totk/gen"
    complete = image.is_file() and (generated / "a64_suyu.h").is_file() and any(generated.glob("code_*.c"))
    if complete and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == identity:
        with image.open(encoding="utf-8") as f:
            modules = {m["name"]: m["build_id"] for m in json.load(f)["modules"]}
        if any(modules.get(name) != game[name] for name in ("main", "sdk")):
            fail("Existing build contains a different game version. Move the build folder aside before retrying.")
        print("Reusing the existing lifted game code.", flush=True)
    else:
        if BUILD.exists() and any(BUILD.iterdir()):
            fail("Build inputs changed or the lift is incomplete. Move the build folder aside before rebuilding.")
        run(sys.executable, KIT / "build.py", "lift", "totk", "--exefs", exefs,
            "--skip", "subsdk0", "--build", BUILD)
        stamp.write_text(identity + "\n", encoding="ascii")
    # SKYBOUND_JOBS=<n>: compile at most n files at once (default: one per CPU thread). Each lifted file is ~25 MB
    # of C and takes a few GB of memory to compile, so a machine with less memory than threads x that wants fewer.
    jobs = os.environ.get("SKYBOUND_JOBS", "")
    if jobs and not (jobs.isdigit() and int(jobs) > 0):
        fail(f"SKYBOUND_JOBS must be a positive number, not {jobs!r}.")
    run(sys.executable, KIT / "build.py", "native", "totk", "--build", BUILD, "--wgpu", WGPU,
        *(["-j", jobs] if jobs else []))
    package(romfs)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(f"ERROR: command failed with exit code {error.returncode}", file=sys.stderr)
        sys.exit(error.returncode or 1)
