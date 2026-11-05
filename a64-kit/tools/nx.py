"""Switch container reader: NSP (PFS0) or XCI (gamecard image: HFS0) -> NCA -> ExeFS (PFS0) / RomFS.

Formats (switchbrew.org "NCA", "PFS0", "HFS0", "XCI", "RomFS", "Ticket"):
  - NSP is a PFS0: the NCAs, plus a .tik/.cert per rights ID for titlekey-encrypted content.
  - XCI: a gamecard header ("HEAD" at 0x100; the root HFS0's offset at 0x130), the root HFS0's partitions (update,
    normal, secure, ...), the game's NCAs in "secure" (an HFS0 too). Gamecard NCAs use their key area (no tickets).
  - An NCA's first 0xC00 bytes are AES-128-XTS with header_key, 0x200 sectors, and a BIG-endian sector tweak.
  - Each NCA section is AES-128-CTR. The IV is the section's 8-byte counter (fs header +0x140, read little-endian,
    stored big-endian) followed by (offset within the NCA >> 4) big-endian.
  - The section key comes from the ticket (rights ID set: titlekey decrypted with titlekek_<gen>) or from the key
    area (key_area_key_application_<gen>, slot 2 for CTR).
  - Section 0 of a program NCA is the ExeFS (PFS0 behind a SHA-256 hash table), section 1 the RomFS (behind IVFC).

  - An update's program NCA has its own ExeFS, and a patch RomFS (crypt type 4, BKTR) that only makes sense over
    the base game's RomFS: see PatchedSection. Its hash tree (IVFC) covers the patched result, so romfs-verify
    checks the merge against hashes the update itself carries.

Keys are read from ~/.switch/prod.keys (or --keys). Key values are never printed.

Usage:
  python nx.py info  <game.nsp>
  python nx.py exefs <game.nsp> <out_dir>
  python nx.py romfs-list <game.nsp> <out.tsv> [--update <update.nsp>]   path, size, offset of every RomFS file
  python nx.py romfs <game.nsp> <out_dir> [glob ...] [--update <update.nsp>]
                                                          extract RomFS files (all, or those matching a glob)
  python nx.py romfs-verify <game.nsp> [--update <update.nsp>] [--sample N]
                                                          check the RomFS against its hash tree (all of it, or
                                                          N random blocks of the file data level)
  With --update, <game.nsp> is the base game and the update's patch RomFS is applied over it.
"""
import bisect
import fnmatch
import hashlib
import os
import random
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

MEDIA = 0x200
CONTENT_TYPES = {0: "program", 1: "meta", 2: "control", 3: "manual", 4: "data", 5: "publicdata"}
FS_TYPES = {0: "romfs", 1: "pfs0"}
CRYPT_TYPES = {0: "auto", 1: "none", 2: "xts", 3: "ctr", 4: "bktr", 5: "ctr-skip-hash", 6: "ctr-ex-skip-hash"}


def load_keys(path=None):
    path = path or os.path.join(os.path.expanduser("~"), ".switch", "prod.keys")
    keys = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        name, sep, value = line.partition("=")
        if sep:
            keys[name.strip().lower()] = bytes.fromhex(value.strip())
    return keys


def need(keys, name):
    if name not in keys:
        raise KeyError(f"{name} missing from prod.keys")
    return keys[name]


def aes_ecb_dec(key, data):
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data) + d.finalize()


def _cstr(buf, off):
    end = buf.find(b"\0", off)
    return buf[off:end if end >= 0 else len(buf)].decode("utf-8", "replace")


class Pfs0:
    """A PFS0 read through `read(off, size)`, which addresses the bytes the PFS0 starts at."""

    def __init__(self, read):
        self.read = read
        magic, n, strsz, _ = struct.unpack("<4sIII", read(0, 0x10))
        if magic != b"PFS0":
            raise ValueError("not a PFS0")
        ents = read(0x10, 0x18 * n)
        strs = read(0x10 + 0x18 * n, strsz)
        self.data_off = 0x10 + 0x18 * n + strsz
        self.files = {}
        for i in range(n):
            off, size, name_off, _ = struct.unpack_from("<QQII", ents, 0x18 * i)
            self.files[_cstr(strs, name_off)] = (self.data_off + off, size)

    def get(self, name):
        off, size = self.files[name]
        return self.read(off, size)


class Hfs0:
    """An HFS0 (a gamecard partition) read through `read(off, size)`; offsets in `files` are from its start."""

    def __init__(self, read):
        self.read = read
        magic, n, strsz, _ = struct.unpack("<4sIII", read(0, 0x10))
        if magic != b"HFS0":
            raise ValueError("not an HFS0")
        ents = read(0x10, 0x40 * n)
        strs = read(0x10 + 0x40 * n, strsz)
        self.data_off = 0x10 + 0x40 * n + strsz
        self.files = {}
        for i in range(n):
            off, size, name_off = struct.unpack_from("<QQI", ents, 0x40 * i)
            self.files[_cstr(strs, name_off)] = (self.data_off + off, size)


class Nca:
    def __init__(self, f, base, size, keys, titlekeys):
        self.f, self.base, self.size = f, base, size
        f.seek(base)
        enc = f.read(0xC00)
        hk = need(keys, "header_key")
        dec = b""
        for s in range(6):
            d = Cipher(algorithms.AES(hk), modes.XTS(s.to_bytes(16, "big"))).decryptor()
            dec += d.update(enc[s * MEDIA:(s + 1) * MEDIA]) + d.finalize()
        if dec[0x200:0x204] != b"NCA3":
            raise ValueError(f"NCA at {base:#x}: bad magic {dec[0x200:0x204]!r} (wrong header_key?)")
        self.hdr = dec
        self.content_type = dec[0x205]
        self.kaek_index = dec[0x207]
        self.program_id = struct.unpack_from("<Q", dec, 0x210)[0]
        self.sdk_version = struct.unpack_from("<I", dec, 0x21C)[0]
        gen = max(dec[0x206], dec[0x220])
        self.key_gen = gen - 1 if gen else 0
        self.rights_id = dec[0x230:0x240]
        if any(self.rights_id):
            tk = titlekeys.get(self.rights_id)
            if tk is None:
                raise KeyError(f"no ticket for rights ID {self.rights_id.hex()}")
            self.ctr_key = aes_ecb_dec(need(keys, f"titlekek_{self.key_gen:02x}"), tk)
        else:
            kaek = need(keys, f"key_area_key_{('application', 'ocean', 'system')[self.kaek_index]}_{self.key_gen:02x}")
            self.ctr_key = aes_ecb_dec(kaek, dec[0x300:0x340])[0x20:0x30]
        self.sections = []
        for i in range(4):
            start, end = struct.unpack_from("<II", dec, 0x240 + 0x10 * i)
            if not end:
                self.sections.append(None)
                continue
            fsh = dec[0x400 + 0x200 * i:0x600 + 0x200 * i]
            self.sections.append({
                "index": i, "start": start * MEDIA, "end": end * MEDIA,
                "fs_type": fsh[2], "hash_type": fsh[3], "crypt": fsh[4],
                "ctr_hi": struct.unpack_from("<Q", fsh, 0x140)[0], "fsh": fsh,
                "sparse": any(fsh[0x148:0x178]), "compressed": any(fsh[0x178:0x1A0]),
            })

    def read_raw(self, sec, off, size, ctr_val=None):
        """Decrypted bytes of section `sec` at section-relative `off`. ctr_val (AesCtrEx, a patch section's data):
        the counter word that replaces IV bytes 4-7 for this subsection; a patch section's own BKTR tables are read
        without it, with the section's plain counter."""
        a = sec["start"] + off
        lo = a & ~0xF
        self.f.seek(self.base + lo)
        data = self.f.read(a + size - lo)
        if len(data) != a + size - lo:
            raise ValueError(f"section {sec['index']}: read past the end of the NCA ({off:#x}+{size:#x})")
        if sec["crypt"] in (3, 4, 5, 6):
            hi = sec["ctr_hi"]
            if ctr_val is not None:
                hi = (hi & 0xFFFFFFFF00000000) | ctr_val
            iv = hi.to_bytes(8, "big") + (lo >> 4).to_bytes(8, "big")
            d = Cipher(algorithms.AES(self.ctr_key), modes.CTR(iv)).decryptor()
            data = d.update(data) + d.finalize()
        elif sec["crypt"] != 1:
            raise NotImplementedError(f"section crypt type {CRYPT_TYPES.get(sec['crypt'], sec['crypt'])}")
        return data[a - lo:a - lo + size]

    def data_region(self, sec):
        """(offset, size) of the payload behind the section's hash layers."""
        fsh = sec["fsh"]
        if sec["hash_type"] == 2:  # hierarchical SHA-256: payload is the second region
            return struct.unpack_from("<QQ", fsh, 0x40)
        if sec["hash_type"] == 3:  # IVFC: payload is the last of six levels
            if fsh[0x8:0xC] != b"IVFC":
                raise ValueError("bad IVFC header")
            return struct.unpack_from("<QQ", fsh, 0x18 + 5 * 0x18)
        raise NotImplementedError(f"hash type {sec['hash_type']}")

    def reader(self, sec):
        if sec["crypt"] == 4:
            raise ValueError(f"section {sec['index']} is an update's patch RomFS: read it over the base game's "
                             "(--update, PatchedSection)")
        base, _ = self.data_region(sec)
        return lambda off, size: self.read_raw(sec, base + off, size)

    def pfs0(self, idx):
        return Pfs0(self.reader(self.sections[idx]))

    def romfs(self, idx):
        return RomFs(self.reader(self.sections[idx]))


class RomFs:
    def __init__(self, read):
        self.read = read
        h = struct.unpack("<10Q", read(0, 0x50))
        _hsize, _dht, _dhts, dmt, dmts, _fht, _fhts, fmt, fmts, self.data_off = h
        self.dirs = read(dmt, dmts)
        self.fents = read(fmt, fmts)

    def walk(self):
        """Yield (path, offset in RomFS data, size) for every file."""
        stack = [(0, "")]
        while stack:
            d, prefix = stack.pop()
            _parent, _sib, child, file_, _hash, nlen = struct.unpack_from("<6I", self.dirs, d)
            name = self.dirs[d + 0x18:d + 0x18 + nlen].decode("utf-8", "replace")
            path = f"{prefix}/{name}" if d else ""
            while file_ != 0xFFFFFFFF:
                _p, sib, off, size, _h, flen = struct.unpack_from("<IIQQII", self.fents, file_)
                fname = self.fents[file_ + 0x20:file_ + 0x20 + flen].decode("utf-8", "replace")
                yield f"{path}/{fname}", off, size
                file_ = sib
            c = child
            while c != 0xFFFFFFFF:
                stack.append((c, path))
                c = struct.unpack_from("<I", self.dirs, c + 4)[0]

    def file_reader(self, off):
        return lambda o, size: self.read(self.data_off + off + o, size)


class PatchedSection:
    """An update's patch RomFS section (crypt type 4, "BKTR") over the base game's RomFS section.

    Layout (switchbrew.org "NCA", PatchInfo at fs header +0x100; entry sizes and the counter as in hactool's bktr.h /
    nca.c, github.com/SciresM/hactool, ISC):
      - The fs header has two BKTR headers: the indirect (relocation) table at +0x100 and the AesCtrEx (subsection)
        table at +0x120, each {offset u64, size u64, "BKTR", version u32, entry count u32, reserved u32}, offsets
        relative to the section. Both tables sit at the section's end and are encrypted with its plain AES-CTR.
      - A table: a 0x4000-byte header {u32, bucket count u32, total size u64, the buckets' first offsets u64[]},
        then 0x4000-byte buckets {u32, entry count u32, end offset u64, entries}. Relocation entries are 0x14 bytes
        {virtual offset u64, physical offset u64, 1 = from the patch / 0 = from the base u32}; subsection entries
        0x10 bytes {physical offset u64, u32, counter value u32}.
      - The patched section is a virtual space: each relocation maps a run of it to the same-length run at its
        physical offset in the patch section or in the base section (decrypted as usual). Patch data is AES-CTR with
        the counter value of the subsection it is in written over IV bytes 4-7; from the tables on, the section's
        own counter (its low word) applies.
      - The patched space has its own IVFC hash tree (the patch fs header's): the RomFS is its last level."""
    HEADER = BUCKET = 0x4000

    def __init__(self, patch, psec, base, bsec):
        if psec["crypt"] != 4:
            raise ValueError("not a patch (BKTR) section")
        if bsec["crypt"] == 4:
            raise ValueError("the base section is itself a patch")
        self.patch, self.psec, self.base, self.bsec = patch, psec, base, bsec
        fsh = psec["fsh"]
        ind_off, ind_size, ind_magic, _, ind_n, _ = struct.unpack_from("<QQ4sIII", fsh, 0x100)
        sub_off, sub_size, sub_magic, _, sub_n, _ = struct.unpack_from("<QQ4sIII", fsh, 0x120)
        if ind_magic != b"BKTR" or sub_magic != b"BKTR":
            raise ValueError("patch section: no BKTR headers")
        self.size = psec["end"] - psec["start"]
        if ind_off + ind_size != sub_off or sub_off + sub_size != self.size:
            # (hactool insists on the tables ending the section; newer NCAs can have more after them. The hash
            # tree, romfs-verify, is what says whether the merge is right, so this only reports the layout.)
            extra = [name for name, a, b in (("sparse", 0x148, 0x178), ("compression", 0x178, 0x1A0),
                                             ("metadata hash", 0x1A0, 0x1C8)) if any(fsh[a:b])]
            print(f"note: patch section layout: indirect table {ind_off:#x}+{ind_size:#x}, AesCtrEx table "
                  f"{sub_off:#x}+{sub_size:#x}, section size {self.size:#x}; fs header info present: "
                  f"{', '.join(extra) or 'none'}", file=sys.stderr)
        if ind_off + ind_size > self.size or sub_off + sub_size > self.size:
            raise ValueError("patch section: BKTR tables outside the section")
        ind = patch.read_raw(psec, ind_off, ind_size)
        sub = patch.read_raw(psec, sub_off, sub_size)
        rel, self.virt_size = self._table(ind, 0x14, "<QQI")
        if len(rel) != ind_n:
            raise ValueError(f"relocation table: {len(rel)} entries, header says {ind_n}")
        self.r_virt = [e[0] for e in rel]
        self.r_phys = [e[1] for e in rel]
        self.r_patch = [e[2] for e in rel]
        if self.r_virt != sorted(self.r_virt) or self.r_virt[0] != 0:
            raise ValueError("relocation table not sorted from 0")
        subs, _ = self._table(sub, 0x10, "<QII")
        if len(subs) != sub_n:
            raise ValueError(f"subsection table: {len(subs)} entries, header says {sub_n}")
        # from the tables on, the section's own counter (hactool: the fake last entry with section_ctr_low)
        subs.append((ind_off, 0, psec["ctr_hi"] & 0xFFFFFFFF))
        self.s_off = [e[0] for e in subs]
        self.s_ctr = [e[2] for e in subs]
        if self.s_off != sorted(self.s_off):
            raise ValueError("subsection table not sorted")

    def _table(self, raw, esize, fmt):
        _, nb, total = struct.unpack_from("<IIQ", raw, 0)
        out = []
        for b in range(nb):
            at = self.HEADER + b * self.BUCKET
            _, n, _end = struct.unpack_from("<IIQ", raw, at)
            if 0x10 + n * esize > self.BUCKET:
                raise ValueError(f"bucket {b}: {n} entries do not fit")
            out += [struct.unpack_from(fmt, raw, at + 0x10 + i * esize) for i in range(n)]
        return out, total

    def _read_patch(self, off, size):
        out = []
        while size:
            j = bisect.bisect_right(self.s_off, off) - 1
            end = self.s_off[j + 1] if j + 1 < len(self.s_off) else self.size
            n = min(size, end - off)
            out.append(self.patch.read_raw(self.psec, off, n, ctr_val=self.s_ctr[j]))
            off, size = off + n, size - n
        return b"".join(out)

    def read(self, off, size):
        """Bytes of the patched section at `off` (section-relative, before its hash tree)."""
        if off < 0 or off + size > self.virt_size:
            raise ValueError(f"patched read {off:#x}+{size:#x} outside {self.virt_size:#x}")
        out = []
        while size:
            i = bisect.bisect_right(self.r_virt, off) - 1
            end = self.r_virt[i + 1] if i + 1 < len(self.r_virt) else self.virt_size
            n = min(size, end - off)
            phys = self.r_phys[i] + (off - self.r_virt[i])
            if self.r_patch[i]:
                out.append(self._read_patch(phys, n))
            else:
                out.append(self.base.read_raw(self.bsec, phys, n))
            off, size = off + n, size - n
        return b"".join(out)


class Ivfc:
    """A RomFS section's IVFC hash tree (fs header +0x8): six levels {offset u64, size u64, log2 block size u32,
    u32} at +0x18; each level's blocks hash (SHA-256, the last block zero-padded) into the level before it, the
    first level into the master hash at +0xC8. `read(off, size)` reads the section (or a PatchedSection)."""

    def __init__(self, fsh, read):
        if fsh[0x8:0xC] != b"IVFC":
            raise ValueError("bad IVFC header")
        self.read = read
        self.levels = [struct.unpack_from("<QQI", fsh, 0x18 + i * 0x18) for i in range(6)]
        self.master = fsh[0xC8:0xE8]
        self.tables = {}

    def data_region(self):
        return self.levels[5][:2]

    def blocks(self, k):
        off, size, bs = self.levels[k]
        return (size + (1 << bs) - 1) >> bs

    def _block_ok(self, k, i, hashes):
        off, size, bs = self.levels[k]
        b = 1 << bs
        n = min(b, size - i * b)
        data = self.read(off + i * b, n) + bytes(b - n)
        return hashlib.sha256(data).digest() == hashes[32 * i:32 * i + 32]

    def verify(self, sample=None, rng=None, progress=None):
        """Check levels 1-5 completely (they are the hash tables), and every block of the RomFS level or, with
        `sample`, that many random blocks plus its first and last 64. Returns (blocks checked, bad blocks)."""
        hashes = self.master
        checked, bad = 0, []
        for k in range(6):
            n = self.blocks(k)
            if k < 5 or sample is None:
                which = range(n)
            else:
                rng = rng or random.Random(0)
                which = sorted(set(range(min(64, n))) | set(range(max(0, n - 64), n)) |
                               {rng.randrange(n) for _ in range(sample)})
            for i in which:
                checked += 1
                if not self._block_ok(k, i, hashes):
                    bad.append((k + 1, i))
                if progress and checked % 4096 == 0:
                    progress(k + 1, checked)
            if k < 5:
                off, size, _ = self.levels[k]
                hashes = self.read(off, size)
        return checked, bad


class Nsp:
    """An NSP, or an XCI (its secure partition's NCAs; `pfs` then holds that partition's files at file offsets)"""

    def __init__(self, path, keys):
        self.f = open(path, "rb")
        if self._read(0x100, 4) == b"HEAD":
            root_off = struct.unpack("<Q", self._read(0x130, 8))[0]
            root = Hfs0(lambda o, n: self._read(root_off + o, n))
            if "secure" not in root.files:
                raise ValueError("XCI without a secure partition: " + ", ".join(root.files))
            sec_off = root_off + root.files["secure"][0]
            sec = Hfs0(lambda o, n: self._read(sec_off + o, n))
            sec.files = {name: (sec_off + off, size) for name, (off, size) in sec.files.items()}
            sec.get = lambda name: self._read(*sec.files[name])
            self.pfs = sec
        else:
            self.pfs = Pfs0(self._read)
        titlekeys = {}
        for name in self.pfs.files:
            if name.endswith(".tik"):
                tik = self.pfs.get(name)
                if struct.unpack_from("<I", tik, 0)[0] != 0x10004:
                    raise ValueError(f"{name}: unexpected ticket signature type")
                if tik[0x281] != 0:
                    raise ValueError(f"{name}: personalized ticket, only common tickets are supported")
                titlekeys[tik[0x2A0:0x2B0]] = tik[0x180:0x190]
        self.ncas = {}
        for name, (off, size) in self.pfs.files.items():
            if name.endswith(".nca"):
                self.ncas[name] = Nca(self.f, off, size, keys, titlekeys)

    def _read(self, off, size):
        self.f.seek(off)
        return self.f.read(size)

    def program(self, base=None):
        """The program NCA. A gamecard can hold more than one (the game and an update put on the cartridge): then
        the base game's (program ID ending in 000, no patch RomFS) unless base=False, else the largest."""
        progs = [n for n in self.ncas.values() if n.content_type == 0]
        if not progs:
            raise ValueError("no program NCA")
        if len(progs) > 1:
            def patch(n):
                return any(sec and sec["crypt"] == 4 for sec in n.sections)
            pick = [n for n in progs if (base is not False) == ((n.program_id & 0xFFF) == 0 and not patch(n))]
            progs = sorted(pick or progs, key=lambda n: -n.size)
            print(f"  ({len(self.ncas)} NCAs; program {progs[0].program_id:016x} used)", flush=True)
        return progs[0]


def copy_out(read, size, path, chunk=8 << 20):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as out:
        done = 0
        while done < size:
            n = min(chunk, size - done)
            out.write(read(done, n))
            done += n


def romfs_source(prog, update_prog=None):
    """(RomFs, Ivfc) of a program NCA's RomFS (section 1), or of an update's patch RomFS applied over it."""
    bsec = prog.sections[1]
    if update_prog is None:
        ivfc = Ivfc(bsec["fsh"], lambda o, n: prog.read_raw(bsec, o, n))
    else:
        if update_prog.program_id != prog.program_id:
            raise SystemExit(f"the update is for program {update_prog.program_id:016x}, the base is "
                             f"{prog.program_id:016x}")
        psec = update_prog.sections[1]
        patched = PatchedSection(update_prog, psec, prog, bsec)
        ivfc = Ivfc(psec["fsh"], patched.read)
    off, _ = ivfc.data_region()
    return RomFs(lambda o, n: ivfc.read(off + o, n)), ivfc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    def option(name):
        if name not in args:
            return None
        i = args.index(name)
        if i + 1 >= len(args):
            raise SystemExit(f"{name} needs a value")
        value = args[i + 1]
        del args[i:i + 2]
        return value
    keys = load_keys(option("--keys"))
    update_path, sample = option("--update"), option("--sample")
    nsp = Nsp(path, keys)
    update = Nsp(update_path, keys) if update_path else None

    if cmd == "info":
        for name, (off, size) in nsp.pfs.files.items():
            print(f"{name:<48} {size:>14,}")
        for name, n in nsp.ncas.items():
            print(f"\n{name}: {CONTENT_TYPES.get(n.content_type, n.content_type)}  program {n.program_id:016x}"
                  f"  key gen {n.key_gen}  sdk {n.sdk_version >> 24}.{(n.sdk_version >> 16) & 0xFF}."
                  f"{(n.sdk_version >> 8) & 0xFF}  titlekey {'yes' if any(n.rights_id) else 'no'}")
            for s in n.sections:
                if s:
                    off, size = n.data_region(s)
                    extra = (" sparse" if s["sparse"] else "") + (" compressed" if s["compressed"] else "")
                    print(f"  section {s['index']}: {FS_TYPES.get(s['fs_type'], s['fs_type']):<5} "
                          f"{CRYPT_TYPES.get(s['crypt'], s['crypt']):<4} payload {size:>14,} bytes{extra}")
        return

    prog = nsp.program()
    if cmd == "exefs":
        out = args[0]
        pfs = prog.pfs0(0)
        for name, (off, size) in pfs.files.items():
            copy_out(lambda o, n, off=off: pfs.read(off + o, n), size, os.path.join(out, name))
            print(f"  {name:<12} {size:>12,}")
    elif cmd in ("romfs-list", "romfs", "romfs-verify"):
        rfs, ivfc = romfs_source(prog, update.program(base=False) if update else None)
        if cmd == "romfs-verify":
            n6 = ivfc.blocks(5)
            print(f"RomFS level: {n6:,} blocks of {1 << ivfc.levels[5][2]:,} bytes; checking "
                  + (f"{int(sample):,} random blocks + the first and last 64" if sample else "all of them"), flush=True)
            checked, bad = ivfc.verify(int(sample) if sample else None,
                                       progress=lambda lvl, n: print(f"  level {lvl}: {n:,} blocks checked",
                                                                     end="\r", flush=True))
            print(f"\n{checked:,} blocks checked, {len(bad)} bad" + (f": first {bad[:5]}" if bad else ""))
            sys.exit(1 if bad else 0)
        files = sorted(rfs.walk())
        if cmd == "romfs-list":
            with open(args[0], "w", encoding="utf-8") as out:
                for p, off, size in files:
                    out.write(f"{p}\t{size}\t{off}\n")
            print(f"{len(files):,} files, {sum(f[2] for f in files):,} bytes -> {args[0]}")
        else:
            out, globs = args[0], args[1:]
            total = 0
            for p, off, size in files:
                if globs and not any(fnmatch.fnmatch(p, g) for g in globs):
                    continue
                copy_out(rfs.file_reader(off), size, os.path.join(out, p.lstrip("/")))
                total += size
            print(f"extracted {total:,} bytes -> {out}")
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
