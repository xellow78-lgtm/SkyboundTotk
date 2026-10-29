"""lift/a64_extra.py against Unicorn: every instruction form it covers, random and edge-case register states.

Usage:  python a64-kit/tests/extra_insn_test.py [--forms N] [--states N] [--seed N]
Needs: pip install unicorn; a C compiler (clang or cc) on PATH. Exit status 1 on any difference.

Each generated word is run once in Unicorn (one instruction, FPSR cleared) and once as a64_extra's C text compiled
against a minimal guest context (vreg[32][2], fpsr); all 32 V registers and FPSR.QC are compared.
"""
import argparse, ctypes, os, random, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "lift"))
import a64_extra
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_CPACR_EL1, UC_ARM64_REG_FPSR, UC_ARM64_REG_V0

CODE = 0x10000
QC = 1 << 27


def forms(rng, n):
    """Random encodings across the family: vector/scalar, U, opcode, Q, every immh class, edge shifts."""
    out = set()
    while len(out) < n:
        scalar = rng.random() < 0.3
        kind = rng.choice(["sqshl", "uqshl", "sqshlu"])
        u, opcode = {"sqshl": (0, 0b01110), "uqshl": (1, 0b01110), "sqshlu": (1, 0b01100)}[kind]
        esize = rng.choice([8, 16, 32, 64])
        q = 1 if (esize == 64 and not scalar) else rng.randint(0, 1)
        shift = rng.choice([0, 1, esize - 1, rng.randrange(esize)])
        imm = esize + shift   # immh:immb
        immh, immb = imm >> 3, imm & 7
        rn, rd = rng.randrange(32), rng.randrange(32)
        if scalar:
            w = 0x5F000400 | (u << 29) | (immh << 19) | (immb << 16) | (opcode << 11) | (rn << 5) | rd
        else:
            w = 0x0F000400 | (q << 30) | (u << 29) | (immh << 19) | (immb << 16) | (opcode << 11) | (rn << 5) | rd
        assert a64_extra.translate(w), hex(w)
        out.add(w)
    return sorted(out)


EDGES = [0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
         0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF, 0x00FF00FF00FF00FF, 0x0100010001000100]


def states(rng, n):
    out = []
    for _ in range(n):
        regs = []
        for _ in range(32):
            if rng.random() < 0.4:
                lo, hi = rng.choice(EDGES), rng.choice(EDGES)
            else:
                lo, hi = rng.getrandbits(64), rng.getrandbits(64)
            # small values too, so most shifts do not saturate
            if rng.random() < 0.3:
                lo, hi = lo & 0x0F0F0F0F0F0F0F0F >> rng.randrange(4), hi & 0x0F0F0F0F0F0F0F0F >> rng.randrange(4)
            regs.append((lo, hi))
        out.append(regs)
    return out


def run_unicorn(w, regs):
    mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    mu.mem_map(CODE, 0x1000)
    mu.mem_write(CODE, struct.pack("<I", w))
    mu.reg_write(UC_ARM64_REG_CPACR_EL1, mu.reg_read(UC_ARM64_REG_CPACR_EL1) | (3 << 20))   # FP/SIMD on
    for i, (lo, hi) in enumerate(regs):
        mu.reg_write(UC_ARM64_REG_V0 + i, lo | hi << 64)
    mu.reg_write(UC_ARM64_REG_FPSR, 0)
    mu.emu_start(CODE, CODE + 4, count=1)
    v = [mu.reg_read(UC_ARM64_REG_V0 + i) for i in range(32)]
    return [(x & (2 ** 64 - 1), x >> 64) for x in v], mu.reg_read(UC_ARM64_REG_FPSR) & QC


def build_c(words, tmp):
    src = ["#include <stdint.h>", "#include <string.h>",
           "typedef struct { uint64_t vreg[32][2]; uint64_t fpsr; } Ctx;"]
    for k, w in enumerate(words):
        src.append("void t%d(Ctx *c) { %s }" % (k, a64_extra.translate(w)))
    c_path, so_path = os.path.join(tmp, "extra.c"), os.path.join(tmp, "extra.so")
    open(c_path, "w").write("\n".join(src) + "\n")
    cc = shutil.which("clang") or shutil.which("cc")
    subprocess.run([cc, "-O1", "-shared", "-fPIC", "-Werror=implicit-function-declaration", "-o", so_path, c_path],
                   check=True)
    return ctypes.CDLL(so_path)


class Ctx(ctypes.Structure):
    _fields_ = [("vreg", (ctypes.c_uint64 * 2) * 32), ("fpsr", ctypes.c_uint64)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--forms", type=int, default=400)
    ap.add_argument("--states", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    words = forms(rng, a.forms)
    # the TOTK 1.4.3 SDK's own SQSHL words are always in the set
    words = sorted(set(words) | {0x4f2876f7, 0x4f287652, 0x4f287484, 0x4f2874a5, 0x4f2874e7, 0x4f287400,
                                 0x4f2874c6, 0x4f287421, 0x4f287442, 0x4f287463})
    sts = states(rng, a.states)
    with tempfile.TemporaryDirectory() as tmp:
        lib = build_c(words, tmp)
        diffs = runs = 0
        for k, w in enumerate(words):
            fn = getattr(lib, "t%d" % k)
            for regs in sts:
                want, want_qc = run_unicorn(w, regs)
                ctx = Ctx()
                for i, (lo, hi) in enumerate(regs):
                    ctx.vreg[i][0], ctx.vreg[i][1] = lo, hi
                fn(ctypes.byref(ctx))
                got = [(ctx.vreg[i][0], ctx.vreg[i][1]) for i in range(32)]
                got_qc = ctx.fpsr & QC
                runs += 1
                if got != want or got_qc != want_qc:
                    diffs += 1
                    if diffs <= 10:
                        bad = [i for i in range(32) if got[i] != want[i]]
                        print("DIFF %08x: regs %s; QC got %d want %d" % (w, bad, bool(got_qc), bool(want_qc)))
                        for i in bad[:2]:
                            print("   v%d in  %016x%016x" % (i, regs[i][1], regs[i][0]))
                            print("      got %016x%016x\n     want %016x%016x" % (got[i][1], got[i][0], want[i][1], want[i][0]))
    print("%d forms, %d states each: %d runs, %d differences from Unicorn" % (len(words), len(sts), runs, diffs))
    sys.exit(1 if diffs else 0)


if __name__ == "__main__":
    main()
