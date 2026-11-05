#!/usr/bin/env python3
"""The launcher's build: your game files in, the game out (launcher/launcher.cpp runs this and shows its progress).

Steps (each skipped when its result is already there, so a stopped build continues):
  update   (--fetch <owner/repo> --branch <name>) the newest source from GitHub over <source>: the branch's newest
           commit as a zip (no git needed). Files the previous update brought and the new one no longer has are removed;
           nothing else is touched (your game files, build, dist, user folder). The steps below then rebuild what changed.
  files    the game's ExeFS and RomFS into <source>/game: from folders you extracted (the ExeFS is copied, the RomFS is
           linked, not copied), or from your base game and 1.4.3 update .nsp files with your prod.keys (nx.py)
  overlay  changed source files copied over the source (--overlay: the folder Claude's changes go into)
  tools    Python packages (installed if missing), clang (emsdk), Visual Studio's C++ tools
  dlss     (--dlss) the WebGPU library with what DLSS and Reflex need, and NVIDIA's DLSS files (Rust, ~20-30 min once)
  lift     the game's code translated to C (a64-kit/build.py lift; skipped when nothing it reads changed)
  compile  compiled and linked (a64-kit/build.py native: only what changed)
  install  the game into <source>/dist/SkyboundTOTK (the RomFS linked; your user folder and mods untouched)

It prints lines for the launcher between the build tools' own output:
  @@step <key> <state> <label>    state: wait, run, done, skip, fail
  @@progress <0..1> <text>        the running step's progress
  @@end ok|fail <message>
usage: pipeline.py --source <SkyboundTOTK folder> [--exefs <dir> --romfs <dir> | --nsp <base.nsp> --update <upd.nsp>
                   --keys <prod.keys>] [--overlay <dir>] [--dlss] [--fetch <owner/repo> --branch <name>] [--log <file>]
       pipeline.py --source <folder> --check <owner/repo> --branch <name>: is there a newer commit? one line:
                   @@update <sha> <unix time> <subject> | @@uptodate <sha> | @@updatefail <why>
"""
import argparse
import calendar
import os
import re
import shutil
import subprocess
import sys
import io
import json
import time
import urllib.request
import zipfile
from pathlib import Path

STEPS = [("update", "Updates"), ("files", "Game files"), ("overlay", "Source changes"), ("tools", "Build tools"),
         ("dlss", "DLSS library"), ("lift", "Translate the game's code"), ("compile", "Compile"),
         ("install", "Install")]
LOG = None


def say(line):
    print(line, flush=True)
    if LOG:
        LOG.write(line + "\n")
        LOG.flush()


def step(key, state):
    say("@@step %s %s %s" % (key, state, dict(STEPS)[key]))


def progress(frac, text=""):
    say("@@progress %.3f %s" % (max(0.0, min(1.0, frac)), text))


class Failed(Exception):
    pass


def run(args, cwd, env, parse=None):
    """Runs a build tool, its output passed on line by line (parse(line) may report progress)."""
    say("> " + " ".join('"%s"' % a if " " in str(a) else str(a) for a in args))
    p = subprocess.Popen([str(a) for a in args], cwd=str(cwd), env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, bufsize=1, text=True, encoding="utf-8", errors="replace")
    for line in p.stdout:
        line = line.rstrip("\r\n")
        say(line)
        if parse:
            parse(line)
    if p.wait():
        raise Failed("%s failed (exit code %d): its output is above" % (Path(str(args[1] if len(args) > 1 else args[0])).name,
                                                                         p.returncode))


def vs_env():
    """Visual Studio's C++ build environment (vcvars64.bat), as variables, or None."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    candidates = []
    if vswhere.is_file():
        out = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                             capture_output=True, text=True).stdout.strip()
        if out:
            candidates.append(Path(out) / "VC/Auxiliary/Build/vcvars64.bat")
    candidates.append(SKYBOUND / "msvc/setup_x64.bat")   # (setup_school.bat's, no admin)
    for bat in candidates:
        if bat.is_file():
            out = subprocess.run('"%s" >nul && set' % bat, shell=True, capture_output=True, text=True,
                                 errors="replace").stdout
            env = dict(line.split("=", 1) for line in out.splitlines() if "=" in line and not line.startswith("="))
            if "INCLUDE" in env:
                return env, bat
    return None, None


def module_id(path):
    if not os.path.isfile(path):
        return ""
    with open(path, "rb") as f:
        header = f.read(0x60)
    return header[0x40:0x60].rstrip(b"\0").hex() if header[:4] == b"NSO0" else ""


MAIN_143 = "277178b7dba1b6d4949a84778d4abc58b31b34f5"


def game_files(a, env):
    exefs, romfs = SOURCE / "game/exefs", SOURCE / "game/romfs"
    have_exefs = (exefs / "main").is_file() and module_id(exefs / "main") == MAIN_143
    have_romfs = (romfs / "Event").is_dir() and (romfs / "ActorSystem").is_dir()
    if have_exefs and have_romfs and not a.exefs and not a.nsp:
        return "skip"
    exefs.parent.mkdir(parents=True, exist_ok=True)
    if a.exefs:   # (folders you extracted)
        src = Path(a.exefs)
        if module_id(src / "main") != MAIN_143:
            raise Failed("%s\\main is not TOTK 1.4.3's (this build needs 1.4.3: the update's ExeFS)" % src)
        if src.resolve() != exefs.resolve():
            exefs.mkdir(exist_ok=True)
            for name in ("main", "main.npdm", "rtld", "sdk", "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4"):
                if (src / name).is_file():
                    shutil.copy2(src / name, exefs / name)
        progress(0.2, "ExeFS copied")
    if a.romfs:
        src = Path(a.romfs)
        if not (src / "Event").is_dir() or not (src / "ActorSystem").is_dir():
            raise Failed("%s does not look like TOTK's RomFS (no Event and ActorSystem folders)" % src)
        if not romfs.exists() or romfs.resolve() != src.resolve():
            if is_junction(romfs) or os.path.islink(romfs):
                remove_link(romfs)
            elif romfs.is_dir():
                if any(romfs.iterdir()):
                    raise Failed("%s already holds a RomFS: move it away to use %s instead" % (romfs, src))
                romfs.rmdir()
            junction(romfs, src)
        progress(0.4, "RomFS linked (not copied)")
    if a.nsp:   # (your .nsp files and keys: nx.py decrypts them on this PC)
        nx = SOURCE / "a64-kit/tools/nx.py"
        keys = ["--keys", a.keys] if a.keys else []
        if not (exefs / "main").is_file() or module_id(exefs / "main") != MAIN_143:
            run([sys.executable, "-u", nx, "exefs", a.update, exefs] + keys, SOURCE, env)
        progress(0.1, "ExeFS extracted")
        if not (SOURCE / "game/romfs_1.4.3.done").is_file():
            say("The RomFS: about 17 GB, half an hour or more")
            run([sys.executable, "-u", nx, "romfs", a.nsp, romfs, "--update", a.update] + keys, SOURCE, env)
            (SOURCE / "game/romfs_1.4.3.done").write_text("done\n")
    if module_id(exefs / "main") != MAIN_143:
        raise Failed("no TOTK 1.4.3 ExeFS in %s: choose your game files on the Build page" % exefs)
    if not (romfs / "Event").is_dir():
        raise Failed("no RomFS in %s: choose your game files on the Build page" % romfs)
    return "done"


def is_junction(p):
    try:
        return bool(os.lstat(p).st_file_attributes & 0x400)   # (FILE_ATTRIBUTE_REPARSE_POINT)
    except (OSError, AttributeError):
        return False


def remove_link(p):
    if is_junction(p) or os.path.islink(p):
        os.rmdir(p) if os.path.isdir(p) else os.unlink(p)
    elif Path(p).is_dir():
        Path(p).rmdir()


def junction(link, target):
    r = subprocess.run('mklink /J "%s" "%s"' % (link, target), shell=True, capture_output=True, text=True)
    if r.returncode:
        raise Failed("could not link %s to %s: %s" % (link, target, (r.stdout + r.stderr).strip()))
    say("linked %s -> %s" % (link, target))


def overlay(a):
    if not a.overlay or not Path(a.overlay).is_dir():
        return "skip"
    n = old = 0
    since = installed_version()[1]   # (overlay files from before the installed update are in it already, or older)
    for root, _, files in os.walk(a.overlay):
        for f in files:
            src = Path(root) / f
            if since and src.stat().st_mtime < since:
                old += 1
                continue
            dst = SOURCE / src.relative_to(a.overlay)
            dst.parent.mkdir(parents=True, exist_ok=True)
            if not dst.is_file() or dst.read_bytes() != src.read_bytes():
                shutil.copy2(src, dst)
                n += 1
    say("%d changed files copied from %s%s" % (n, a.overlay, " (%d older than the installed update left out)" % old if old else ""))
    return "done"


# ---- updates: the newest commit of a branch on GitHub ----
VERSION_FILE, MANIFEST_FILE = ".skybound_version", ".skybound_files.txt"


def installed_version():
    """(sha, commit time) of the update the source holds, ("", 0) if none"""
    try:
        sha, t = (SOURCE / VERSION_FILE).read_text(encoding="utf-8").split()[:2]
        return sha, int(t)
    except (OSError, ValueError):
        return "", 0


def github(url):
    req = urllib.request.Request(url, headers={"User-Agent": "SkyboundTOTK-launcher",
                                               "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def newest_commit(repo, branch):
    """(sha, unix time, subject) of the branch's newest commit"""
    c = json.loads(github("https://api.github.com/repos/%s/commits/%s" % (repo, branch)))
    when = c["commit"]["committer"]["date"]   # e.g. 2026-11-05T05:40:00Z
    t = calendar.timegm(time.strptime(when, "%Y-%m-%dT%H:%M:%SZ"))
    return c["sha"], t, c["commit"]["message"].splitlines()[0][:120]


def check(a):
    try:
        sha, t, subject = newest_commit(a.check, a.branch)
    except Exception as e:
        say("@@updatefail %s" % e)
        return 0
    have = installed_version()[0]
    say("@@uptodate %s" % sha if have == sha else "@@update %s %d %s" % (sha, t, subject))
    return 0


def update(a):
    if not a.fetch:
        return "skip"
    sha, t, subject = newest_commit(a.fetch, a.branch)
    if installed_version()[0] == sha:
        say("the source is already at %s" % sha[:10])
        return "skip"
    progress(0.1, "downloading %s" % sha[:10])
    data = github("https://api.github.com/repos/%s/zipball/%s" % (a.fetch, sha))
    say("downloaded %d KB: %s" % (len(data) >> 10, subject))
    z = zipfile.ZipFile(io.BytesIO(data))
    names = [n for n in z.namelist() if not n.endswith("/")]
    prefix = names[0].split("/")[0] + "/" if names else ""
    files = sorted(n[len(prefix):] for n in names if n.startswith(prefix) and len(n) > len(prefix))
    keep = ("game/", "build/", "dist/", "tmp/", ".git/")
    written = 0
    for i, rel in enumerate(files):
        if rel.startswith(keep) or ".." in Path(rel).parts:
            continue
        dst = SOURCE / rel
        blob = z.read(prefix + rel)
        if not dst.is_file() or dst.read_bytes() != blob:
            dst.parent.mkdir(parents=True, exist_ok=True)
            try:
                dst.write_bytes(blob)
            except PermissionError:   # (a running launcher's own exe: aside, then written)
                dst.rename(dst.with_name(dst.name + ".old"))
                dst.write_bytes(blob)
            written += 1
        if i % 50 == 0:
            progress(0.2 + 0.7 * i / max(1, len(files)), rel[-60:])
    removed = 0
    try:   # (what the previous update brought and this one no longer has)
        for rel in (SOURCE / MANIFEST_FILE).read_text(encoding="utf-8").splitlines():
            if rel and rel not in files and not rel.startswith(keep) and (SOURCE / rel).is_file():
                (SOURCE / rel).unlink()
                removed += 1
    except OSError:
        pass
    (SOURCE / MANIFEST_FILE).write_text("\n".join(files) + "\n", encoding="utf-8")
    (SOURCE / VERSION_FILE).write_text("%s %d\n" % (sha, t), encoding="utf-8")
    say("updated to %s (%s): %d files changed, %d removed" % (sha[:10], subject, written, removed))
    return "done"


def tools(a, env):
    need = []
    for mod, pkg in (("lz4.block", "lz4"), ("cryptography", "cryptography"), ("fontTools", "fonttools")):
        if subprocess.run([sys.executable, "-c", "import " + mod], capture_output=True).returncode:
            need.append(pkg)
    if subprocess.run([sys.executable, "-c", "import compression.zstd"], capture_output=True).returncode and \
            subprocess.run([sys.executable, "-c", "import zstandard"], capture_output=True).returncode:
        need.append("zstandard")
    if need:
        user = [] if sys.prefix != sys.base_prefix else ["--user"]
        run([sys.executable, "-m", "pip", "install"] + user + need, SOURCE, env)
    progress(0.3, "Python packages")
    clang = Path(env["EMSDK"]) / "upstream/bin/clang.exe"
    if not clang.is_file():
        raise Failed("clang not found at %s: run setup_tools.bat once (it installs emsdk 6.0.9)" % clang)
    progress(0.6, "clang")
    vs, bat = vs_env()
    if not vs:
        raise Failed("no Visual Studio with the C++ build tools: install \"Build Tools for Visual Studio\" (Desktop "
                     "development with C++), or run setup_school.bat once (no admin)")
    say("using %s" % bat)
    for k, v in vs.items():
        if k.upper() not in ("EMSDK", "TEMP", "TMP", "PYTHONIOENCODING", "PYTHONUNBUFFERED"):
            env[k] = v
    return "done"


def has_reflex_lib():
    lib = SOURCE / "third_party/wgpu-native/lib/wgpu_native.dll.lib"
    return lib.is_file() and b"wgpuTotkReflexSleep" in lib.read_bytes()


def dlss(a, env):
    dlss_dir = SOURCE / "third_party/dlss"
    builder = SOURCE / "third_party/wgpu-native/build_dlss_deps.py"
    if has_reflex_lib() and (dlss_dir / "lib/nvsdk_ngx_s.lib").is_file():
        return "skip"
    if has_reflex_lib() and (dlss_dir / "include").is_dir():   # (the library is there, NVIDIA's files not yet)
        run([sys.executable, "-u", builder, "--work", SKYBOUND / "wgpu-build", "--skip-wgpu"], SOURCE, env)
        return "done"
    if not a.dlss:
        say("DLSS and Reflex need the rebuilt WebGPU library: tick \"DLSS library\" on the Build page to make it "
            "(~20-30 min once). Building without them now (FSR 2 works).")
        return "skip"
    env["RUSTUP_HOME"] = str(SKYBOUND / "rust/rustup")
    env["CARGO_HOME"] = str(SKYBOUND / "rust/cargo")
    env["PATH"] = str(SKYBOUND / "rust/cargo/bin") + os.pathsep + env["PATH"]
    if not (SKYBOUND / "rust/cargo/bin/cargo.exe").is_file():
        init = SKYBOUND / "rustup-init.exe"   # (it must keep this name: rustup picks what it does from it)
        say("downloading rustup")
        urllib.request.urlretrieve("https://win.rustup.rs/x86_64", init)
        run([init, "-y", "--no-modify-path", "--profile", "minimal", "--default-toolchain", "1.93",
             "--default-host", "x86_64-pc-windows-msvc"], SKYBOUND, env)
    progress(0.15, "Rust")
    user = [] if sys.prefix != sys.base_prefix else ["--user"]
    run([sys.executable, "-m", "pip", "install"] + user + ["--quiet", "libclang"], SOURCE, env)
    progress(0.2, "libclang")

    def parse(line):
        m = re.search(r"Compiling (\S+)", line)
        if m:
            parse.n += 1
            progress(0.2 + 0.75 * min(1, parse.n / 260), "compiling " + m.group(1))
    parse.n = 0
    run([sys.executable, "-u", builder, "--work", SKYBOUND / "wgpu-build"], SOURCE, env, parse)
    return "done"


def lift(a, env):
    sys.path.insert(0, str(SOURCE))
    import build_game   # (its identity of what the lift reads: the lifter, hos/, rt/, the ExeFS)
    build_game.ROOT, build_game.KIT = SOURCE, SOURCE / "a64-kit"
    identity = build_game.lift_identity(SOURCE / "game/exefs")
    stamp, gen = SOURCE / "build/totk/lift-input.sha256", SOURCE / "build/totk/gen"
    if stamp.is_file() and stamp.read_text(encoding="ascii").strip() == identity and \
            (gen / "a64_suyu.h").is_file() and any(gen.glob("code_*.c")):
        return "skip"
    marks = (("image:", 0.1), ("discovered", 0.4), ("functions ", 0.5), ("wrote ", 0.9))

    def parse(line):
        for text, frac in marks:
            if line.startswith(text):
                progress(frac, line[:60])
    run([sys.executable, "-u", SOURCE / "a64-kit/build.py", "lift", "totk", "--exefs", SOURCE / "game/exefs",
         "--skip", "subsdk0", "--build", SOURCE / "build"], SOURCE, env, parse)
    stamp.write_text(identity + "\n", encoding="ascii")
    return "done"


def compile_(a, env):
    try:   # (a file compiles in ~3.5 GB at most: as many at once as memory allows, 4 GB left for Windows)
        import ctypes

        class MS(ctypes.Structure):
            _fields_ = [("len", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong)] + \
                       [("f%d" % i, ctypes.c_ulonglong) for i in range(6)]
        ms = MS()
        ms.len = ctypes.sizeof(MS)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
        jobs = max(1, min(os.cpu_count() or 1, int((ms.total / 2 ** 30 - 4) / 3.5)))
    except Exception:
        jobs = max(1, (os.cpu_count() or 2) // 2)
    say("%d files compiled at once" % jobs)

    def parse(line):
        m = re.match(r"\[(\d+)/(\d+)\]", line)
        if m:
            progress(int(m.group(1)) / int(m.group(2)) * 0.9, line[:70])
        elif line.startswith("linked "):
            progress(1, "linked")
        elif "link" in line.lower() and "rsp" in line:
            progress(0.92, "linking")
    run([sys.executable, "-u", SOURCE / "a64-kit/build.py", "native", "totk", "--build", SOURCE / "build", "--wgpu",
         SOURCE / "third_party/wgpu-native", "-j", str(jobs)], SOURCE, env, parse)
    return "done"


def put(src, dst):
    """Copies src to dst; a dst still in use (a game Windows still reports on; this launcher) is renamed aside."""
    old = dst.with_name(dst.name + ".old")
    try:
        if old.exists():
            old.unlink()
    except OSError:
        pass
    try:
        shutil.copy2(src, dst)
    except PermissionError:
        dst.rename(old)
        shutil.copy2(src, dst)


def install(a, env):
    native, image, dist = SOURCE / "build/totk/native", SOURCE / "build/totk/image", SOURCE / "dist/SkyboundTOTK"
    dist.mkdir(parents=True, exist_ok=True)
    files = [(native / "totk.exe", "SkyboundTOTK.exe"), (image / "image.bin", "image.bin"),
             (image / "image.json", "image.json"), (native / "wgpu_native.dll", "wgpu_native.dll"),
             (native / "SDL2.dll", "SDL2.dll"), (native / "nvngx_dlss.dll", "nvngx_dlss.dll"),
             (native / "totk.pdb", "totk.pdb"), (native / "button_icons.py", "button_icons.py"),
             (native / "python.txt", "python.txt"), (native / "SkyboundTOTK Launcher.exe", "SkyboundTOTK Launcher.exe"),
             (SOURCE / "launcher/pipeline.py", "pipeline.py")]
    for i, (src, name) in enumerate(files):
        if src.is_file():
            put(src, dist / name)
        elif name in ("SkyboundTOTK.exe", "image.bin", "image.json", "wgpu_native.dll"):
            raise Failed("the build made no %s" % src)
        progress((i + 1) / (len(files) + 1), name)
    if not (dist / "romfs").exists():
        junction(dist / "romfs", SOURCE / "game/romfs")
    (dist / "user").mkdir(exist_ok=True)
    return "done"


def main():
    global SOURCE, SKYBOUND, LOG
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True)
    p.add_argument("--exefs")
    p.add_argument("--romfs")
    p.add_argument("--nsp")
    p.add_argument("--update")
    p.add_argument("--keys")
    p.add_argument("--overlay")
    p.add_argument("--dlss", action="store_true")
    p.add_argument("--log")
    p.add_argument("--check")
    p.add_argument("--fetch")
    p.add_argument("--branch", default="update-1.4.3")
    a = p.parse_args()
    SOURCE = Path(a.source).resolve()
    SKYBOUND = SOURCE.parent
    if a.check:
        return check(a)
    if a.log:
        LOG = open(a.log, "w", encoding="utf-8")
    if a.nsp and not a.update:
        say("@@end fail The 1.4.3 update's .nsp is needed with the base game's")
        return 1
    env = dict(os.environ)
    env.setdefault("EMSDK", str(SKYBOUND / "emsdk"))
    env["TEMP"] = env["TMP"] = str(SKYBOUND / "tmp")
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUNBUFFERED"] = "1"
    env["SKYBOUND_EXPERIMENTAL"] = "1"
    (SKYBOUND / "tmp").mkdir(exist_ok=True)
    for key, _ in STEPS:
        step(key, "wait")
    work = {"update": lambda: update(a), "files": lambda: game_files(a, env), "overlay": lambda: overlay(a), "tools": lambda: tools(a, env),
            "dlss": lambda: dlss(a, env), "lift": lambda: lift(a, env), "compile": lambda: compile_(a, env),
            "install": lambda: install(a, env)}
    t0 = time.time()
    for key, label in STEPS:
        step(key, "run")
        progress(0, "")
        try:
            state = work[key]() or "done"
        except Failed as e:
            step(key, "fail")
            say("@@end fail %s: %s" % (label, e))
            return 1
        except Exception as e:   # (anything else: said, not a crash dialog)
            step(key, "fail")
            say("%s: %r" % (label, e))
            say("@@end fail %s: %s" % (label, e))
            return 1
        step(key, state)
        if key == "update" and state == "done":
            new = SOURCE / "launcher/pipeline.py"
            if new.is_file() and new.read_bytes() != Path(__file__).read_bytes():
                # (the update brought another pipeline: the rest of the build is its, in this process's place)
                args = [sys.executable, "-u", str(new)] + [x for x in sys.argv[1:]]
                i = args.index("--fetch")
                del args[i:i + 2]
                say("continuing with the updated pipeline")
                if LOG:
                    LOG.close()
                    LOG = None
                return subprocess.call(args)
    say("@@end ok Built in %d min. Press Play." % max(1, round((time.time() - t0) / 60)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
