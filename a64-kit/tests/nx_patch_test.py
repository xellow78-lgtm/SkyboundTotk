"""nx.py's PatchedSection and Ivfc on synthetic data: a base section and an update's patch section built here with real
AES-CTR (the patch's data under several subsection counters, its BKTR tables under the plain section counter), more
relocations than fit one bucket, and an IVFC tree over the patched result. Every random read of the patched view must
equal the intended bytes, the hash tree must verify, and a corrupted patch byte must make it fail.

Usage:  python a64-kit/tests/nx_patch_test.py      (needs: pip install cryptography)
This checks the code against the format as nx.py's docstrings state it; the check against real game files is
`nx.py romfs-verify <base.nsp> --update <update.nsp>`, whose hashes come from the update itself.
"""
import hashlib, io, os, random, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import nx
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

rng = random.Random(7)
KEY = bytes(rng.randrange(256) for _ in range(16))


def ctr_crypt(data, nca_off, ctr_hi):
    """AES-CTR of `data` placed at NCA offset `nca_off` (16-byte aligned), counter word = ctr_hi."""
    assert nca_off % 16 == 0
    iv = ctr_hi.to_bytes(8, "big") + (nca_off >> 4).to_bytes(8, "big")
    e = Cipher(algorithms.AES(KEY), modes.CTR(iv)).encryptor()
    return e.update(data) + e.finalize()


def fake_nca(blob):
    n = object.__new__(nx.Nca)
    n.f, n.base, n.ctr_key = io.BytesIO(blob), 0, KEY
    return n


def table(entries, fmt, esize, first_key, total):
    """A BKTR table: 0x4000 header + 0x4000 buckets of up to (0x3FF0 // esize) entries."""
    per = 0x3FF0 // esize
    buckets = [entries[i:i + per] for i in range(0, len(entries), per)]
    head = struct.pack("<IIQ", 0, len(buckets), total) + b"".join(struct.pack("<Q", first_key(b[0])) for b in buckets)
    out = head.ljust(0x4000, b"\0")
    for k, b in enumerate(buckets):
        end = first_key(buckets[k + 1][0]) if k + 1 < len(buckets) else total
        body = struct.pack("<IIQ", 0, len(b), end) + b"".join(struct.pack(fmt, *e) for e in b)
        out += body.ljust(0x4000, b"\0")
    return out


def ivfc_tree(data_len, bs_log2=9, first_off=0):
    """IVFC levels over a data level of data_len bytes; returns (levels [(off, size, bs)], layout size)."""
    sizes = [data_len]
    b = 1 << bs_log2
    while len(sizes) < 6:
        n = (sizes[-1] + b - 1) // b
        sizes.append(32 * n)
    sizes = sizes[::-1]   # level 1 (smallest) .. level 6 (data)
    levels, off = [], first_off
    for s in sizes:
        levels.append((off, s, bs_log2))
        off += (s + 15) & ~15
    return levels, off


def build():
    # the patched section's content: an IVFC tree whose last level is "romfs" data
    data_len = 600_000 + rng.randrange(1000)
    levels, layout = ivfc_tree(data_len)
    virt = bytearray(layout)
    off6 = levels[5][0]
    virt[off6:off6 + data_len] = bytes(rng.randrange(256) for _ in range(data_len))
    for k in range(4, -1, -1):   # each level = hashes of the next one's zero-padded blocks
        doff, dsize, bs = levels[k + 1]
        b = 1 << bs
        hashes = b"".join(hashlib.sha256(bytes(virt[doff + i:doff + min(i + b, dsize)]).ljust(b, b"\0")).digest()
                          for i in range(0, dsize, b))
        o, s, _ = levels[k]
        assert len(hashes) == s
        virt[o:o + s] = hashes
    b0 = 1 << levels[0][2]
    master = hashlib.sha256(bytes(virt[levels[0][0]:levels[0][0] + levels[0][1]]).ljust(b0, b"\0")).digest()
    virt_size = len(virt)

    # relocations: small runs alternating base / patch, > one bucket's worth (0x3FF0 // 0x14 = 818)
    cuts = sorted(set([0] + [rng.randrange(1, virt_size) for _ in range(1500)]))
    runs = list(zip(cuts, cuts[1:] + [virt_size]))
    base_sec = bytearray(rng.randrange(256) for _ in range(virt_size + 50_000))   # base content, at other offsets
    # base runs get non-overlapping places in the base section, in shuffled order, with random gaps
    base_runs = [r for k, r in enumerate(runs) if k % 3 == 0]
    order = list(range(len(base_runs)))
    rng.shuffle(order)
    place, at = {}, rng.randrange(64)
    for j in order:
        a, e = base_runs[j]
        place[a] = at
        at += (e - a) + rng.randrange(8)
    assert at <= len(base_sec)
    patch_data, rel = bytearray(), []
    for k, (a, e) in enumerate(runs):
        n = e - a
        if k % 3 == 0:   # from the base
            phys = place[a]
            base_sec[phys:phys + n] = virt[a:e]
            rel.append((a, phys, 0))
        else:
            rel.append((a, len(patch_data), 1))
            patch_data += virt[a:e]
    # patch section: data, padding to 16, then the two tables
    patch_data += b"\0" * (-len(patch_data) % 16)
    ind_off = len(patch_data)
    ind = table(rel, "<QQI", 0x14, lambda e: e[0], virt_size)
    # subsections: random cut points (16-aligned) over the data, each with its own counter value
    scuts = sorted(set([0] + [rng.randrange(1, ind_off // 16) * 16 for _ in range(1200)]))
    subs = [(c, 0, rng.getrandbits(32)) for c in scuts]
    sub_off = ind_off + len(ind)
    sub = table(subs, "<QII", 0x10, lambda e: e[0], ind_off)
    psize = sub_off + len(sub)

    # the NCA blobs: section at NCA offset 0xC00 (a media-unit multiple), encrypted
    SEC = 0xC00
    ctr_hi_p, ctr_hi_b = rng.getrandbits(64), rng.getrandbits(64)
    pblob = bytearray(SEC + psize)
    for j, (c, _, val) in enumerate(subs):   # data, subsection by subsection
        end = subs[j + 1][0] if j + 1 < len(subs) else ind_off
        word = (ctr_hi_p & 0xFFFFFFFF00000000) | val
        pblob[SEC + c:SEC + end] = ctr_crypt(bytes(patch_data[c:end]), SEC + c, word)
    pblob[SEC + ind_off:SEC + psize] = ctr_crypt(ind + sub, SEC + ind_off, ctr_hi_p)   # plain section counter
    bblob = bytearray(SEC) + ctr_crypt(bytes(base_sec), SEC, ctr_hi_b)

    def fsh(crypt, ctr_hi, patch_info=b""):
        h = bytearray(0x200)
        h[2], h[3], h[4] = 0, 3, crypt
        h[0x8:0xC] = b"IVFC"
        struct.pack_into("<II", h, 0xC, 0x20, 7)
        for i, (o, s, bs) in enumerate(levels):
            struct.pack_into("<QQI", h, 0x18 + 0x18 * i, o, s, bs)
        h[0xC8:0xE8] = master
        h[0x100:0x100 + len(patch_info)] = patch_info
        struct.pack_into("<Q", h, 0x140, ctr_hi)
        return bytes(h)

    pinfo = struct.pack("<QQ4sIII", ind_off, len(ind), b"BKTR", 1, len(rel), 0) + \
        struct.pack("<QQ4sIII", sub_off, len(sub), b"BKTR", 1, len(subs), 0)
    psec = {"index": 1, "start": SEC, "end": SEC + psize, "crypt": 4, "ctr_hi": ctr_hi_p,
            "fsh": fsh(4, ctr_hi_p, pinfo)}
    bsec = {"index": 1, "start": SEC, "end": SEC + len(base_sec), "crypt": 3, "ctr_hi": ctr_hi_b,
            "fsh": fsh(3, ctr_hi_b)}
    return bytes(virt), fake_nca(bytes(pblob)), psec, fake_nca(bytes(bblob)), bsec, pblob, levels, len(rel), len(subs)


def main():
    virt, pnca, psec, bnca, bsec, pblob, levels, nrel, nsub = build()
    ps = nx.PatchedSection(pnca, psec, bnca, bsec)
    assert ps.virt_size == len(virt)
    print(f"synthetic patch: {len(virt):,}-byte patched section, {nrel:,} relocations, {nsub:,} subsections")
    bad = 0
    for _ in range(3000):
        a = rng.randrange(len(virt))
        n = rng.randrange(1, min(70_000, len(virt) - a) + 1)
        if ps.read(a, n) != virt[a:a + n]:
            bad += 1
    whole = ps.read(0, len(virt)) == virt
    print(f"random reads: 3000, wrong: {bad}; whole section equal: {whole}")
    iv = nx.Ivfc(psec["fsh"], ps.read)
    checked, badb = iv.verify()
    print(f"IVFC over the patched view: {checked:,} blocks, {len(badb)} bad")
    # a corrupted patch byte inside the data level must be caught by the hash tree
    off6, size6, _ = levels[5]
    target = next(a for a in range(off6, off6 + size6)
                  if ps.r_patch[nx.bisect.bisect_right(ps.r_virt, a) - 1])
    i = nx.bisect.bisect_right(ps.r_virt, target) - 1
    phys = ps.r_phys[i] + (target - ps.r_virt[i])
    pblob[psec["start"] + phys] ^= 0xFF
    pnca2 = fake_nca(bytes(pblob))
    ps2 = nx.PatchedSection(pnca2, psec, bnca, bsec)
    _, badc = nx.Ivfc(psec["fsh"], ps2.read).verify()
    print(f"after flipping one patch byte: {len(badc)} bad block(s) {badc[:3]}")
    ok = bad == 0 and whole and not badb and len(badc) >= 1
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
