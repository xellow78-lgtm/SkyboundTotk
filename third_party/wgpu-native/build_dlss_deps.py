"""Builds what the DLSS path needs, on Windows, from sources it downloads (nothing here is installed system-wide):

1. wgpu_native.dll with the TOTK patches and the Vulkan interop for DLSS (totk-src/: the patched files of wgpu 29.0.3
   and wgpu-native 29.0.1.1; the same changes as totk-patches/*.patch), into third_party/wgpu-native/lib (the
   release that was there is kept as *.orig).
2. NVIDIA's DLSS SDK (github.com/NVIDIA/DLSS, its license: LICENSE.txt there): the NGX headers, the static NGX
   library and nvngx_dlss.dll, into third_party/dlss. Not part of this repository: it is fetched on your PC.
3. The Khronos Vulkan headers (vulkan_core.h and what it includes; Apache 2.0), into third_party/vulkan-headers.

Needs: Python 3, a Rust toolchain (cargo on the PATH, or RUSTUP_HOME / CARGO_HOME set), the MSVC linker in the
environment (vcvars64), and libclang for bindgen (LIBCLANG_PATH; `pip install libclang` has one: --libclang finds it).
setup_dlss.bat (claude stuff) does all of that.

    python build_dlss_deps.py --work <a scratch folder> [--skip-wgpu] [--skip-sdk]
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TP = os.path.dirname(HERE)   # third_party

WGPU_NATIVE = ("https://codeload.github.com/gfx-rs/wgpu-native/zip/refs/tags/v29.0.1.1", "wgpu-native")
WGPU = ("https://codeload.github.com/gfx-rs/wgpu/zip/refs/tags/v29.0.3", "wgpu-patched")
WEBGPU_HEADERS = ("https://codeload.github.com/webgpu-native/webgpu-headers/zip/673658bc2bd70ec39fc55ebe6bb0173cf6d0a603",
                  os.path.join("wgpu-native", "ffi", "webgpu-headers"))
VULKAN_RAW = "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.4.367/"
VULKAN_FILES = ["include/vulkan/vulkan_core.h", "include/vulkan/vk_platform.h", "LICENSE.md"] + [
    "include/vk_video/vulkan_video_codec%s.h" % n for n in (
        "_av1std", "_av1std_decode", "_av1std_encode", "_h264std", "_h264std_decode", "_h264std_encode", "_h265std",
        "_h265std_decode", "_h265std_encode", "_vp9std", "_vp9std_decode", "s_common")]
DLSS_COMMIT = "9981c6350dc58c0ce21f024fd65e31e851c31689"
DLSS_RAW = "https://raw.githubusercontent.com/NVIDIA/DLSS/%s/" % DLSS_COMMIT
DLSS_HEADERS = ["nvsdk_ngx.h", "nvsdk_ngx_defs.h", "nvsdk_ngx_defs_vk.h", "nvsdk_ngx_helpers.h", "nvsdk_ngx_helpers_vk.h",
                "nvsdk_ngx_params.h", "nvsdk_ngx_vk.h", "nvsdk_ngx_defs_dlssd.h", "nvsdk_ngx_defs_dlssg.h",
                "nvsdk_ngx_params_dlssd.h", "nvsdk_ngx_params_dlssg.h"]
DLSS_FILES = [("include/" + h, "include/" + h) for h in DLSS_HEADERS] + [
    # (the static-CRT one, /MT, as the game's exe is built; _d is /MD: the link refuses the mismatch)
    ("lib/Windows_x86_64/x64/nvsdk_ngx_s.lib", "lib/nvsdk_ngx_s.lib"),
    ("lib/Windows_x86_64/rel/nvngx_dlss.dll", "lib/nvngx_dlss.dll"),
    ("LICENSE.txt", "LICENSE.txt"),
]


def fetch(url):
    print("  downloading %s" % url, flush=True)
    with urllib.request.urlopen(url, timeout=600) as r:
        return r.read()


def unzip_to(data, dst, skip=()):
    """The zip's single top folder's contents into dst (paths under `skip` left out)."""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        top = names[0].split("/")[0] + "/"
        for n in names:
            rel = n[len(top):]
            if not rel or any(rel.startswith(s) for s in skip):
                continue
            out = os.path.join(dst, rel)
            if n.endswith("/"):
                os.makedirs(out, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(n) as f, open(out, "wb") as o:
                shutil.copyfileobj(f, o)


def sources(work):
    for url, sub in (WGPU_NATIVE, WGPU, WEBGPU_HEADERS):
        dst = os.path.join(work, sub)
        marker = os.path.join(dst, ".downloaded")
        if os.path.exists(marker):
            continue
        unzip_to(fetch(url), dst, skip=("examples/",) if sub == "wgpu-native" else ())
        open(marker, "w").write(url)
    # the patched files over the pristine ones
    src = os.path.join(HERE, "totk-src")
    n = 0
    for root, _, files in os.walk(src):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), src)
            dst = os.path.join(work, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(root, f), dst)
            n += 1
    print("  %d patched files applied" % n)


def find_libclang():
    if os.environ.get("LIBCLANG_PATH"):
        return os.environ["LIBCLANG_PATH"]
    try:
        import clang   # (pip install libclang)
        d = os.path.join(os.path.dirname(clang.__file__), "native")
        if os.path.isfile(os.path.join(d, "libclang.dll")):
            return d
    except ImportError:
        pass
    return None


def build_wgpu(work):
    print("== wgpu-native with the DLSS interop", flush=True)
    sources(work)
    env = dict(os.environ)
    lc = find_libclang()
    if not lc:
        sys.exit("no libclang for bindgen: pip install libclang (or set LIBCLANG_PATH)")
    env["LIBCLANG_PATH"] = lc
    env["CARGO_TARGET_DIR"] = os.path.join(work, "target")
    crate = os.path.join(work, "wgpu-native")
    print("  cargo build --release (10-20 minutes the first time)", flush=True)
    # (by its full path: Windows looks a bare name up in this process's PATH, not env's)
    cargo = shutil.which("cargo", path=env.get("PATH")) or shutil.which(
        "cargo", path=os.path.join(env.get("CARGO_HOME", os.path.expanduser("~/.cargo")), "bin"))
    if not cargo:
        sys.exit("no cargo: install Rust (rustup), or set CARGO_HOME to where it is")
    subprocess.run([cargo, "build", "--release"], cwd=crate, env=env, check=True)
    out = os.path.join(work, "target", "release")
    lib = os.path.join(HERE, "lib")
    for name in ("wgpu_native.dll", "wgpu_native.dll.lib"):
        dst = os.path.join(lib, name)
        if os.path.exists(dst) and not os.path.exists(dst + ".orig"):
            shutil.copyfile(dst, dst + ".orig")
        shutil.copyfile(os.path.join(out, name), dst)
    data = open(os.path.join(lib, "wgpu_native.dll.lib"), "rb").read()
    for sym in (b"wgpuTotkCommandEncoderRaw", b"wgpuTotkCreateRenderPipelineCapture", b"wgpuTotkMemoryHints"):
        if sym not in data:
            sys.exit("the new wgpu_native.dll.lib has no %s: the patch did not apply?" % sym.decode())
    print("  wgpu_native.dll (%.1f MB) and its import library are in %s" % (
        os.path.getsize(os.path.join(lib, "wgpu_native.dll")) / 2 ** 20, lib))


def get_sdk():
    print("== NVIDIA DLSS SDK (commit %s) and the Vulkan headers" % DLSS_COMMIT[:12], flush=True)
    dlss = os.path.join(TP, "dlss")
    for src, dst in DLSS_FILES:
        out = os.path.join(dlss, dst)
        if os.path.exists(out):
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        data = fetch(DLSS_RAW + src)
        open(out, "wb").write(data)
    vk = os.path.join(TP, "vulkan-headers")
    for f in VULKAN_FILES:
        out = os.path.join(vk, f)
        if not os.path.exists(out):
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, "wb").write(fetch(VULKAN_RAW + f))
    print("  %s, %s" % (dlss, vk))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--work", required=True, help="scratch folder for the sources and the cargo build")
    p.add_argument("--skip-wgpu", action="store_true")
    p.add_argument("--skip-sdk", action="store_true")
    a = p.parse_args()
    os.makedirs(a.work, exist_ok=True)
    if not a.skip_sdk:
        get_sdk()
    if not a.skip_wgpu:
        build_wgpu(os.path.abspath(a.work))
    print("done")


if __name__ == "__main__":
    main()
