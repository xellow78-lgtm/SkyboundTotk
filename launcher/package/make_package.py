#!/usr/bin/env python3
"""The release package: SkyboundTOTK-<version>.zip with the launcher, pipeline.py (its first build fetches the rest) and
README.txt, in one folder named SkyboundTOTK.

    python launcher/package/make_package.py --exe "<built SkyboundTOTK Launcher.exe>" --version <name> --out <dir>
"""
import argparse
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.dirname(HERE)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--exe", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--out", default=".")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, "SkyboundTOTK-%s.zip" % a.version)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(a.exe, "SkyboundTOTK/SkyboundTOTK Launcher.exe")
        z.write(os.path.join(LAUNCHER, "pipeline.py"), "SkyboundTOTK/pipeline.py")
        z.write(os.path.join(HERE, "README.txt"), "SkyboundTOTK/README.txt")
        z.write(os.path.join(LAUNCHER, "..", "LICENSE"), "SkyboundTOTK/LICENSE.txt")
    print("%s (%d KB)" % (out, os.path.getsize(out) >> 10))


if __name__ == "__main__":
    main()
