#!/usr/bin/env python3
"""Button icons that match the device played with: keyboard keys (the player's own bindings), Xbox or PlayStation
buttons in place of the Switch's.

Run by the game (hos/buttons.c) when its cache is missing or out of date: usage: button_icons.py <request.txt>.
The request names the game's own files to start from (each one as the game would load it: a mod's when a mod has it),
the folder to write into and the keyboard bindings. It writes, into that folder:
  <style>/<RomFS path>   for each style (keyboard, xbox, playstation): every font archive with its button glyphs
                         redrawn, and the resource size table with those archives' entries grown to fit
  <style>/files.txt      the RomFS paths written for that style
  hud.bin                the button guide's textures (bottom right in play), each one's original content hash and
                         its replacement for each style: the renderer swaps them as the device changes
  stamp.txt              the request's stamp, written last: the set is complete
Nothing of the game ships with the project: everything is made from the player's own RomFS, on their PC.

Where the icons are (as MacNativeGames' make_button_icons.py found them): the A/B/X/Y, L/R, ZL/ZR, +/-, stick and
d-pad icons in the game's text are glyphs U+E040..U+E068 of scft/External.bfotf, an OpenType (CFF) font in each
Font/*.bfarc.zs archive (a SARC, zstd with a dictionary of Pack/ZsDic.pack.zs). A .bfotf is the font XOR-ed with a
32-bit key behind an 8-byte header. An archive that grows needs its entry in the resource size table
(System/Resource/ResourceSizeTable.*.rsizetable.zs, entries by the CRC32 of the path without .zs) grown too, or the
game stops when it loads it. The button guide draws its keys from textures (Nt_KeyTex*, R8 masks: white key, black
letter) of UI/LayoutArchive/Common.*.blarc.zs.

Adapted from make_button_icons.py of MacNativeGames (MIT License, Copyright (c) 2026 MacNativeGames contributors):
the archive, font and size-table handling, the drawing helpers and the Xbox / PlayStation icon shapes. Changed here:
the keyboard style draws the player's bindings (and mouse buttons as a mouse), every style is made in one run, and
the button guide's textures go to the renderer (hud.bin) instead of into the layout archive, so they follow the
device live.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Needs fontTools and zstd: Python 3.14's compression.zstd, or the zstandard package (pip install fonttools zstandard;
build.py installs them).
"""
import io
import os
import struct
import sys
import time
import zlib

try:
    import compression.zstd as _z

    def zdict(data): return _z.ZstdDict(data)
    def zdict_id(d): return d.dict_id
    def zframe_dict(data): return _z.get_frame_info(data).dictionary_id
    def zdecompress(data, d=None): return _z.decompress(data, zstd_dict=d) if d else _z.decompress(data)
    def zcompress(data, d=None): return _z.compress(data, level=3, zstd_dict=d) if d else _z.compress(data, level=3)
except ImportError:
    try:
        import zstandard as _zs
    except ImportError:
        sys.exit('button_icons: needs zstd: pip install zstandard (or Python 3.14)')

    def zdict(data): return _zs.ZstdCompressionDict(data)
    def zdict_id(d): return d.dict_id()
    def zframe_dict(data): return _zs.get_frame_parameters(data).dict_id

    def zdecompress(data, d=None):
        size = _zs.get_frame_parameters(data).content_size
        dc = _zs.ZstdDecompressor(dict_data=d) if d else _zs.ZstdDecompressor()
        return dc.decompress(data, max_output_size=size if size > 0 else 1 << 30)

    def zcompress(data, d=None):
        c = _zs.ZstdCompressor(level=3, dict_data=d) if d else _zs.ZstdCompressor(level=3)
        return c.compress(data)

try:
    from fontTools.pens.basePen import BasePen
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.recordingPen import RecordingPen
    from fontTools.pens.reverseContourPen import ReverseContourPen
    from fontTools.pens.t2CharStringPen import T2CharStringPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
except ImportError:
    sys.exit('button_icons: needs fontTools: pip install fonttools')

DEFAULT_ARCHIVE = 'Font/Font.Nin_NX_NVN.bfarc.zs'   # the text fonts for the key letters are read from it
FONT = 'scft/External.bfotf'
TEXT_FONTS = ('scft/ninP_RodinNTLG-B_DH_003_subset.bfotf', 'scft/nintendo_NTLG-DB_DH_002.bfotf')
MAGIC = 0x18029a7f
STYLES = ('keyboard', 'xbox', 'playstation')   # hud.bin's order; hos/buttons.c numbers them 1, 2, 3

# hos/input_bind.h's actions, in its order (the request's "bind <index> <key>")
ACTIONS = ('A', 'B', 'X', 'Y', 'L', 'R', 'ZL', 'ZR', 'Plus', 'Minus', 'LS', 'RS', 'DLeft', 'DUp', 'DRight', 'DDown',
           'LUp', 'LDown', 'LLeft', 'LRight', 'RUp', 'RDown', 'RLeft', 'RRight', 'Walk')


# ---- the game's formats ----
def restbl_offsets(table):
    """RomFS path CRC32 (or the path itself, for the table's named entries) -> offset of its size."""
    assert table[:6] == b'RESTBL', 'not a resource size table'
    string_size, crc_count, name_count = struct.unpack_from('<III', table, 10)
    out = {}
    for i in range(crc_count):
        out[struct.unpack_from('<I', table, 22 + i * 8)[0]] = 22 + i * 8 + 4
    at = 22 + crc_count * 8
    for i in range(name_count):
        raw = table[at:at + string_size]
        out[raw.split(b'\0', 1)[0].decode('utf-8', 'replace')] = at + string_size
        at += string_size + 4
    return out


def grow_restbl(table, growth):
    """`table` with the entry of each path of `growth` (RomFS path -> bytes) grown by that much."""
    at = restbl_offsets(table)
    out = bytearray(table)
    for path, extra in growth.items():
        bare = path[:-3] if path.endswith('.zs') else path
        where = at.get(bare, at.get(zlib.crc32(bare.encode())))
        if where is None:
            print(f'  (the size table has no entry for {bare}: left as it is)')
            continue
        struct.pack_into('<I', out, where, struct.unpack_from('<I', out, where)[0] + max(extra, 0))
    return bytes(out)


def sarc_members(data):
    """name -> (absolute start, end) of every SARC member."""
    assert data[:4] == b'SARC' and data[6:8] == b'\xff\xfe', 'not a SARC (little-endian)'
    header, size, start = struct.unpack_from('<HxxII', data, 4)
    assert size == len(data) and data[header:header + 4] == b'SFAT'
    count = struct.unpack_from('<H', data, header + 6)[0]
    names = header + 12 + count * 16
    assert data[names:names + 4] == b'SFNT'
    names += 8
    out = {}
    for i in range(count):
        _, attribute, lo, hi = struct.unpack_from('<4I', data, header + 12 + i * 16)
        offset = names + (attribute & 0xffffff) * 4
        out[data[offset:data.index(0, offset, start)].decode()] = (start + lo, start + hi)
    return out


def bfotf_decode(blob):
    key = struct.unpack_from('<I', blob, 0)[0] ^ MAGIC
    size = int.from_bytes(struct.pack('<I', struct.unpack_from('<I', blob, 4)[0] ^ key), 'big')
    body, out = blob[8:8 + size], bytearray()
    for i in range(0, size - size % 4, 4):
        out += struct.pack('<I', struct.unpack_from('<I', body, i)[0] ^ key)
    return bytes(out + body[size - size % 4:]), key


def bfotf_encode(otf, key):
    size = len(otf)
    out = bytearray(struct.pack('<I', MAGIC ^ key) +
                    struct.pack('<I', struct.unpack('<I', size.to_bytes(4, 'big'))[0] ^ key))
    for i in range(0, size - size % 4, 4):
        out += struct.pack('<I', struct.unpack_from('<I', otf, i)[0] ^ key)
    return bytes(out + otf[size - size % 4:])


def repack(sarc, name, blob):
    """The SARC with member `name` replaced by `blob`, every other member byte-identical, in the original order and
    alignment."""
    start = struct.unpack_from('<I', sarc, 12)[0]
    fat = struct.unpack_from('<H', sarc, 4)[0]
    count = struct.unpack_from('<H', sarc, fat + 6)[0]
    nodes = [list(struct.unpack_from('<4I', sarc, fat + 12 + i * 16)) for i in range(count)]
    members = sarc_members(sarc)
    by_range = {(lo, hi): k for k, (lo, hi) in members.items()}
    blobs = [blob if by_range[(start + n[2], start + n[3])] == name else sarc[start + n[2]:start + n[3]] for n in nodes]

    def alignment(offset):
        a = 1
        while offset % (a * 2) == 0 and a < 0x2000:
            a *= 2
        return a
    data, placed = bytearray(), {}
    for i in sorted(range(count), key=lambda i: nodes[i][2]):
        a = alignment(nodes[i][2]) if nodes[i][2] else 1
        while len(data) % a:
            data += b'\0'
        placed[i] = (len(data), len(data) + len(blobs[i]))
        data += blobs[i]
    head = bytearray(sarc[:start])
    for i in range(count):
        struct.pack_into('<II', head, fat + 12 + i * 16 + 8, *placed[i])
    out = bytearray(bytes(head) + bytes(data))
    struct.pack_into('<I', out, 8, len(out))
    out = bytes(out)
    check = sarc_members(out)
    for k, (lo, hi) in members.items():
        assert out[check[k][0]:check[k][1]] == (blob if k == name else sarc[lo:hi]), k
    return out


# ---- drawing (units of the font's 1000-unit em; the round A/B/X/Y icons fill y 20..585) ----
Y0, Y1 = 20, 585
CY, R = (Y0 + Y1) / 2, (Y1 - Y0) / 2
K = 0.5523


class Glyphs:
    """Outlines of letters from the game's own text fonts, drawn as holes in a shape."""
    def __init__(self, fonts):
        self.fonts = fonts

    def outline(self, ch):
        for font in self.fonts:
            cmap = font.getBestCmap()
            if ord(ch) in cmap:
                glyphs = font.getGlyphSet()
                g = glyphs[cmap[ord(ch)]]
                rec = RecordingPen()
                g.draw(rec)
                box = BoundsPen(glyphs)
                g.draw(box)
                return rec, box.bounds, font['head'].unitsPerEm, g.width
        return None

    def hole(self, pen, text, cx, cy, cap, max_width):
        parts, x, ink = [], 0.0, [1e9, 1e9, -1e9, -1e9]
        for ch in text:
            o = self.outline(ch)
            if not o:
                continue
            rec, b, upm, advance = o
            k = 1000 / upm
            if b:   # (a space has no ink)
                parts.append((rec, k, x))
                ink = [min(ink[0], x + b[0] * k), min(ink[1], b[1] * k), max(ink[2], x + b[2] * k), max(ink[3], b[3] * k)]
            x += advance * k * 0.96
        if not parts:
            return
        width, height = max(ink[2] - ink[0], 1), max(ink[3] - ink[1], 1)
        scale = min(cap / height, max_width / width)
        ox, oy = cx - (ink[0] + width / 2) * scale, cy - (ink[1] + height / 2) * scale
        for rec, k, x in parts:
            # (a hole runs clockwise: CFF outlines run counter-clockwise and are reversed, TrueType ones already
            # run clockwise; the letter's net winding tells which it is)
            flat = PolygonPen()
            rec.replay(flat)
            area = sum(x0 * y1 - x1 * y0 for poly in flat.polygons for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
            target = ReverseContourPen(pen) if area > 0 else pen
            rec.replay(TransformPen(target, (scale * k, 0, 0, scale * k, ox + x * scale, oy)))


def circle(pen, cx, cy, r):
    k = K * r
    pen.moveTo((cx + r, cy))
    pen.curveTo((cx + r, cy + k), (cx + k, cy + r), (cx, cy + r))
    pen.curveTo((cx - k, cy + r), (cx - r, cy + k), (cx - r, cy))
    pen.curveTo((cx - r, cy - k), (cx - k, cy - r), (cx, cy - r))
    pen.curveTo((cx + k, cy - r), (cx + r, cy - k), (cx + r, cy))
    pen.closePath()


def rrect(pen, x0, y0, x1, y1, bl=0, br=0, tr=0, tl=0):
    """Counter-clockwise (filled) rectangle with its own radius at each corner (bottom left, bottom right, top
    right, top left; 0: square)."""
    pen.moveTo((x0 + bl, y0))
    pen.lineTo((x1 - br, y0))
    if br:
        pen.curveTo((x1 - br + K * br, y0), (x1, y0 + br - K * br), (x1, y0 + br))
    pen.lineTo((x1, y1 - tr))
    if tr:
        pen.curveTo((x1, y1 - tr + K * tr), (x1 - tr + K * tr, y1), (x1 - tr, y1))
    pen.lineTo((x0 + tl, y1))
    if tl:
        pen.curveTo((x0 + tl - K * tl, y1), (x0, y1 - tl + K * tl), (x0, y1 - tl))
    pen.lineTo((x0, y0 + bl))
    if bl:
        pen.curveTo((x0, y0 + bl - K * bl), (x0 + bl - K * bl, y0), (x0 + bl, y0))
    pen.closePath()


def rounded(pen, x0, y0, x1, y1, bottom, top=None):
    top = bottom if top is None else top
    rrect(pen, x0, y0, x1, y1, bottom, bottom, top, top)


def polygon(pen, points):
    pen.moveTo(points[0])
    for p in points[1:]:
        pen.lineTo(p)
    pen.closePath()


def oriented(pen, points, hole):
    """A polygon counter-clockwise (filled) or clockwise (a hole), whatever order the points come in."""
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]))
    polygon(pen, points if (area > 0) != hole else points[::-1])


ARROWS = {'↑': (0, 1), '↓': (0, -1), '←': (-1, 0), '→': (1, 0)}


def arrow(pen, direction, cx, cy, size, hole=True):
    """An arrow (triangle head and stem), a hole in a key or filled."""
    dx, dy = ARROWS[direction]

    def at(u, v):   # u along the arrow, v across it
        return (cx + u * dx * size - v * dy * size, cy + u * dy * size + v * dx * size)
    oriented(pen, [at(0.5, 0), at(0.0, 0.42), at(0.0, 0.15), at(-0.5, 0.15), at(-0.5, -0.15), at(0.0, -0.15),
                   at(0.0, -0.42)], hole)


def key(glyphs, pen, x0, y0, x1, y1, label, radius=None, cap=None):
    w, h = x1 - x0, y1 - y0
    rounded(pen, x0, y0, x1, y1, radius if radius is not None else min(w, h) * 0.2)
    if label in ARROWS:
        arrow(pen, label, (x0 + x1) / 2, (y0 + y1) / 2, min(w, h) * 0.62)
    elif label:
        glyphs.hole(pen, label, (x0 + x1) / 2, (y0 + y1) / 2, cap or h * 0.56, w * 0.78)


# ---- keyboard and mouse: the player's own bindings ----
MOUSE = {'Mouse1': 'left', 'Mouse2': 'right', 'Mouse3': 'middle', 'WheelUp': 'wheelup', 'WheelDown': 'wheeldown'}
KEY_LABELS = {
    'Left': '←', 'Up': '↑', 'Right': '→', 'Down': '↓', 'Mouse4': 'M4', 'Mouse5': 'M5',
    'LeftShift': 'Shift', 'RightShift': 'Shift', 'LeftCtrl': 'Ctrl', 'RightCtrl': 'Ctrl', 'LeftAlt': 'Alt',
    'RightAlt': 'Alt', 'Backspace': 'Bksp', 'CapsLock': 'Caps', 'PageUp': 'PgUp', 'PageDown': 'PgDn', 'Insert': 'Ins',
    'Delete': 'Del', 'Enter': 'Enter', 'NumMultiply': '*', 'NumPlus': '+', 'NumMinus': '-', 'NumDecimal': '.',
    'NumDivide': '/', 'Semicolon': ';', 'Equals': '=', 'Comma': ',', 'Minus': '-', 'Period': '.', 'Slash': '/',
    'Backquote': '`', 'LeftBracket': '[', 'Backslash': '\\', 'RightBracket': ']', 'Quote': "'",
}


def key_label(name):
    """input.ini's name of a key -> what its key cap shows."""
    if name in KEY_LABELS:
        return KEY_LABELS[name]
    if name.startswith('Num') and name[3:].isdigit():
        return 'N' + name[3:]
    if name.startswith('VK'):
        return '?'
    return name


def mouse_icon(kind, advance=560):
    """A mouse with the button bound lit (left, right, middle; the wheel with an arrow; 'move': the mouse itself)."""
    def draw(pen):
        cx = advance / 2
        x0, x1, y0, y1 = cx - 200, cx + 200, Y0, Y1
        t = 42
        rrect(pen, x0, y0, x1, y1, 170, 170, 190, 190)
        ix0, ix1, iy0, iy1 = x0 + t, x1 - t, y0 + t, y1 - t
        rrect(ReverseContourPen(pen), ix0, iy0, ix1, iy1, 170 - t, 170 - t, 190 - t, 190 - t)
        split = y0 + (y1 - y0) * 0.56
        rrect(pen, ix0, split - t / 2, ix1, split + t / 2)
        rrect(pen, cx - t / 2, split, cx + t / 2, iy1)
        if kind == 'left':
            rrect(pen, ix0, split, cx, iy1, 0, 0, 0, 190 - t)
        elif kind == 'right':
            rrect(pen, cx, split, ix1, iy1, 0, 0, 190 - t, 0)
        elif kind in ('middle', 'wheelup', 'wheeldown'):
            rounded(pen, cx - 50, split + 45, cx + 50, iy1 - 45, 50)
        low = (iy0 + split - t / 2) / 2
        if kind == 'wheelup':
            arrow(pen, '↑', cx, low, 150, hole=False)
        elif kind == 'wheeldown':
            arrow(pen, '↓', cx, low, 150, hole=False)
        elif kind == 'move':
            for d, (dx, dy) in ARROWS.items():
                arrow(pen, d, cx + dx * 75, low + dy * 75, 80, hole=False)
    return advance, draw


def keyboard_style(g, binds, mouse_camera):
    """binds: action name -> input.ini key name ('' unbound). An unbound button keeps the Switch's icon."""
    def label(act):
        return binds.get(act, '')

    def single(act):
        name = label(act)
        if not name:
            return None
        if name in MOUSE:
            return mouse_icon(MOUSE[name])
        text = key_label(name)
        if len(text) <= 1 or text in ARROWS:
            return 640, lambda pen: key(g, pen, 40, Y0, 600, Y1, text)
        advance = 820 if len(text) <= 3 else 1080
        return advance, lambda pen: key(g, pen, 40, Y0 + 40, advance - 40, Y1 - 40, text, radius=110, cap=230)

    def short(act):   # a key cap among others: one or two letters fit
        name = label(act)
        if name in MOUSE:
            return {'left': 'M1', 'right': 'M2', 'middle': 'M3', 'wheelup': 'W↑', 'wheeldown': 'W↓'}[MOUSE[name]]
        return key_label(name)

    def pair(a, b, advance=960):
        if not label(a) or not label(b):
            return None

        def draw(pen):
            w = (advance - 80 - 50) / 2
            key(g, pen, 40, Y0 + 60, 40 + w, Y1 - 60, short(a))
            key(g, pen, 40 + w + 50, Y0 + 60, advance - 40, Y1 - 60, short(b))
        return advance, draw

    def cluster(top, left, mid, right, advance=960):
        if not all(label(x) for x in (top, left, mid, right)):
            return None

        def draw(pen):
            gap = 24
            w = (advance - 80 - 2 * gap) / 3
            h = (Y1 + 40 - (Y0 - 40) - gap) / 2
            bottom = Y0 - 40
            upper = bottom + h + gap
            xs = [40, 40 + w + gap, 40 + 2 * (w + gap)]
            for act, (x, y) in ((top, (xs[1], upper)), (left, (xs[0], bottom)), (mid, (xs[1], bottom)),
                                (right, (xs[2], bottom))):
                key(g, pen, x, y, x + w, y + h, short(act), radius=w * 0.18, cap=h * 0.6)
        return advance, draw

    camera = mouse_icon('move') if mouse_camera else cluster('RUp', 'RLeft', 'RDown', 'RRight')
    icons = {
        0xE040: single('A'), 0xE041: single('B'), 0xE042: single('X'), 0xE043: single('Y'),
        0xE044: single('L'), 0xE045: single('R'), 0xE046: single('ZL'), 0xE047: single('ZR'),
        0xE04B: single('Plus'), 0xE04C: single('Minus'), 0xE056: single('LS'), 0xE057: single('RS'),
        0xE049: cluster('DUp', 'DLeft', 'DDown', 'DRight', 900),
        0xE060: single('DUp'), 0xE061: single('DDown'), 0xE062: single('DLeft'), 0xE063: single('DRight'),
        0xE064: pair('DUp', 'DDown', 900), 0xE065: pair('DLeft', 'DRight', 900),
        # the left stick: movement
        0xE050: single('LDown'), 0xE052: single('LUp'), 0xE054: pair('LLeft', 'LRight'), 0xE058: single('LRight'),
        0xE05A: single('LLeft'), 0xE05C: pair('LUp', 'LDown'), 0xE05E: cluster('LUp', 'LLeft', 'LDown', 'LRight'),
        # the right stick: the camera (the mouse, when it turns it), or its keys one way at a time
        0xE051: single('RDown'), 0xE053: single('RUp'), 0xE055: pair('RLeft', 'RRight'), 0xE059: single('RRight'),
        0xE05B: single('RLeft'), 0xE05D: pair('RUp', 'RDown'), 0xE05F: camera, 0xE068: camera,
    }
    return {cp: icon for cp, icon in icons.items() if icon}


# ---- controllers (shapes as in MacNativeGames' script; buttons by position, as hos/pad_sdl.c maps them) ----
def stick(g, label, up=False, advance=640):
    """The left stick as the controller's own label in a circle; up: with an arrow up (push it forward)."""
    def draw(pen):
        cx = advance / 2
        circle(pen, cx, CY, R)
        if up:
            g.hole(pen, label, cx, CY - R * 0.25, R * 0.55, R * 1.1)
            arrow(pen, '↑', cx, CY + R * 0.5, R * 0.6)
        else:
            g.hole(pen, label, cx, CY, R * 0.95, R * 1.3)
    return advance, draw


def xbox_style(g):
    def face(letter, advance=640):
        def draw(pen):
            circle(pen, advance / 2, CY, R)
            g.hole(pen, letter, advance / 2, CY, R * 0.95, R * 1.3)
        return advance, draw

    def bumper(label, advance=820):
        def draw(pen):
            rounded(pen, 40, Y0 + 70, advance - 40, Y1 - 70, 70, 150)
            g.hole(pen, label, advance / 2, CY, 230, advance - 200)
        return advance, draw

    def trigger(label, advance=760):
        def draw(pen):
            rounded(pen, 60, Y0, advance - 60, Y1, 60, 230)
            g.hole(pen, label, advance / 2, CY - 20, 230, advance - 220)
        return advance, draw

    def menu(advance=640):
        def draw(pen):
            circle(pen, advance / 2, CY, R)
            reverse = ReverseContourPen(pen)
            for dy in (-115, 0, 115):
                rounded(reverse, advance / 2 - 150, CY + dy - 38, advance / 2 + 150, CY + dy + 38, 38)
        return advance, draw

    def view(advance=640):
        def draw(pen):
            circle(pen, advance / 2, CY, R)
            reverse = ReverseContourPen(pen)
            s = 150
            rounded(reverse, advance / 2 - s - 15, CY + 15, advance / 2 - 15, CY + s + 15, 16)
            rounded(reverse, advance / 2 + 15, CY - s - 15, advance / 2 + s + 15, CY - 15, 16)
        return advance, draw

    def dpad(*lit, advance=640):
        def draw(pen):
            cx, length, arm = advance / 2, R, R * 0.40
            polygon(pen, [(cx + length, CY - arm), (cx + length, CY + arm), (cx + arm, CY + arm), (cx + arm, CY + length),
                          (cx - arm, CY + length), (cx - arm, CY + arm), (cx - length, CY + arm), (cx - length, CY - arm),
                          (cx - arm, CY - arm), (cx - arm, CY - length), (cx + arm, CY - length), (cx + arm, CY - arm)])
            m, t, h = (length + arm) / 2, arm * 0.72, arm * 0.80
            heads = {'up': [(cx - t, CY + m - h / 2), (cx, CY + m + h / 2), (cx + t, CY + m - h / 2)],
                     'down': [(cx + t, CY - m + h / 2), (cx, CY - m - h / 2), (cx - t, CY - m + h / 2)],
                     'left': [(cx - m + h / 2, CY - t), (cx - m - h / 2, CY), (cx - m + h / 2, CY + t)],
                     'right': [(cx + m - h / 2, CY + t), (cx + m + h / 2, CY), (cx + m - h / 2, CY - t)]}
            for d in lit:
                polygon(pen, heads[d])   # (clockwise: a hole)
        return advance, draw
    return {
        0xE040: face('B'), 0xE041: face('A'), 0xE042: face('Y'), 0xE043: face('X'),
        0xE044: bumper('LB'), 0xE045: bumper('RB'), 0xE046: trigger('LT'), 0xE047: trigger('RT'),
        0xE04B: menu(), 0xE04C: view(),
        0xE049: dpad('up', 'down', 'left', 'right'), 0xE060: dpad('up'), 0xE061: dpad('down'),
        0xE062: dpad('left'), 0xE063: dpad('right'), 0xE064: dpad('up', 'down'), 0xE065: dpad('left', 'right'),
        0xE052: stick(g, 'LS', up=True), 0xE05E: stick(g, 'LS'),
    }


def playstation_style(g):
    """DualShock / DualSense symbols by position: A B X Y = Circle Cross Triangle Square."""
    def face(symbol, advance=640):
        def draw(pen):
            cx, s = advance / 2, R * 0.50
            circle(pen, cx, CY, R)
            if symbol == 'circle':
                circle(ReverseContourPen(pen), cx, CY, s)
                circle(pen, cx, CY, s * 0.70)
            elif symbol == 'square':
                oriented(pen, [(cx - s, CY - s), (cx + s, CY - s), (cx + s, CY + s), (cx - s, CY + s)], hole=True)
                t = s * 0.70
                oriented(pen, [(cx - t, CY - t), (cx + t, CY - t), (cx + t, CY + t), (cx - t, CY + t)], hole=False)
            elif symbol == 'triangle':
                pts = [(cx, CY + s * 1.10), (cx - s * 1.10, CY - s * 0.75), (cx + s * 1.10, CY - s * 0.75)]
                gx, gy = cx, sum(y for _, y in pts) / 3
                oriented(pen, pts, hole=True)
                oriented(pen, [(gx + (x - gx) * 0.62, gy + (y - gy) * 0.62) for x, y in pts], hole=False)
            else:   # cross: one 12-point outline
                a, w = s * 1.05, s * 0.22 * 2 ** 0.5
                pts = [(cx, CY + w), (cx + a - w, CY + a), (cx + a, CY + a - w), (cx + w, CY),
                       (cx + a, CY - a + w), (cx + a - w, CY - a), (cx, CY - w), (cx - a + w, CY - a),
                       (cx - a, CY - a + w), (cx - w, CY), (cx - a, CY + a - w), (cx - a + w, CY + a)]
                oriented(pen, pts, hole=True)
        return advance, draw

    def bumper(label, advance=820):
        def draw(pen):
            rounded(pen, 40, Y0 + 70, advance - 40, Y1 - 70, 70, 150)
            g.hole(pen, label, advance / 2, CY, 230, advance - 200)
        return advance, draw

    def trigger(label, advance=760):
        def draw(pen):
            rounded(pen, 60, Y0, advance - 60, Y1, 60, 230)
            g.hole(pen, label, advance / 2, CY - 20, 230, advance - 220)
        return advance, draw

    def options(advance=640):
        def draw(pen):
            circle(pen, advance / 2, CY, R)
            reverse = ReverseContourPen(pen)
            for dy in (-115, 0, 115):
                rounded(reverse, advance / 2 - 150, CY + dy - 38, advance / 2 + 150, CY + dy + 38, 38)
        return advance, draw

    def share(advance=640):   # three short upright strokes (Share / Create)
        def draw(pen):
            circle(pen, advance / 2, CY, R)
            cx = advance / 2
            for dx in (-110, 0, 110):
                oriented(pen, [(cx + dx - 30, CY - 150), (cx + dx + 30, CY - 150), (cx + dx + 30, CY + 150),
                               (cx + dx - 30, CY + 150)], hole=True)
        return advance, draw
    dpad_style = xbox_style(g)
    icons = {
        0xE040: face('circle'), 0xE041: face('cross'), 0xE042: face('triangle'), 0xE043: face('square'),
        0xE044: bumper('L1'), 0xE045: bumper('R1'), 0xE046: trigger('L2'), 0xE047: trigger('R2'),
        0xE04B: options(), 0xE04C: share(),
        0xE052: stick(g, 'L3', up=True), 0xE05E: stick(g, 'L3'),
    }
    for cp in (0xE049, 0xE060, 0xE061, 0xE062, 0xE063, 0xE064, 0xE065):
        icons[cp] = dpad_style[cp]
    return icons


# ---- the button guide's textures ----
BNTX = 'timg/__Combined.bntx'
HUD_TEXTURES = {'Nt_KeyTexA_00': 0xE040, 'Nt_KeyTexB_00': 0xE041, 'Nt_KeyTexX_00': 0xE042, 'Nt_KeyTexY_00': 0xE043,
                'Nt_KeyTexL_00': 0xE044, 'Nt_KeyTexZL_00': 0xE046,
                'Nt_KeyTexLSt_00': 0xE052,   # the left stick with an arrow up: push it forward
                'Nt_KeyTexLSt_01': 0xE05E}   # the left stick
R8_UNORM, HUD_MARGIN, SUPERSAMPLE = 0x201, 4, 4


def gob_offset(x, y, width_gobs, block_height):
    """Byte offset of (x, y) in a Tegra block-linear surface of 1 byte per pixel (GOBs of 64 bytes x 8 rows)."""
    return ((y // (8 * block_height)) * 512 * block_height * width_gobs + (x // 64) * 512 * block_height
            + ((y % (8 * block_height)) // 8) * 512 + ((x % 64) // 32) * 256 + ((y % 8) // 2) * 64
            + ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16))


class PolygonPen(BasePen):
    """Outlines flattened to closed polygons."""
    def __init__(self):
        super().__init__(None)
        self.polygons = []

    def _moveTo(self, pt):
        self.polygons.append([pt])

    def _lineTo(self, pt):
        self.polygons[-1].append(pt)

    def _curveToOne(self, pt1, pt2, pt3):
        p0 = self.polygons[-1][-1]
        for i in range(1, 17):
            t = i / 16
            u = 1 - t
            self.polygons[-1].append(tuple(u ** 3 * p0[k] + 3 * u * u * t * pt1[k] + 3 * u * t * t * pt2[k] + t ** 3 * pt3[k]
                                           for k in (0, 1)))

    def _qCurveToOne(self, pt1, pt2):
        p0 = self.polygons[-1][-1]
        for i in range(1, 17):
            t = i / 16
            u = 1 - t
            self.polygons[-1].append(tuple(u * u * p0[k] + 2 * u * t * pt1[k] + t * t * pt2[k] for k in (0, 1)))


def rasterize(draw, size):
    """A size x size coverage mask (0..255, rows top down) of `draw`'s outline, fitted into the square, nonzero
    winding."""
    rec = RecordingPen()
    draw(rec)
    box = BoundsPen(None)
    rec.replay(box)
    x0, y0, x1, y1 = box.bounds
    inner = size - 2 * HUD_MARGIN
    scale = inner / max(x1 - x0, y1 - y0)
    ox = HUD_MARGIN + (inner - (x1 - x0) * scale) / 2 - x0 * scale
    oy = HUD_MARGIN + (inner - (y1 - y0) * scale) / 2 - y0 * scale
    flat = PolygonPen()
    rec.replay(TransformPen(flat, (scale, 0, 0, -scale, ox, size - oy)))   # (font y up -> rows down)
    n = size * SUPERSAMPLE
    rows = [[] for _ in range(n)]   # the edges each sample row crosses
    for poly in flat.polygons:
        for a, b in zip(poly, poly[1:] + poly[:1]):
            if a[1] == b[1]:
                continue
            lo, hi = min(a[1], b[1]), max(a[1], b[1])
            first = max(0, int(lo * SUPERSAMPLE - 0.5) - 1)
            for row in range(first, min(n, int(hi * SUPERSAMPLE + 0.5) + 1)):
                y = (row + 0.5) / SUPERSAMPLE
                if lo <= y < hi:
                    rows[row].append((a, b))
    cover = [0] * (size * size)
    for row in range(n):
        y = (row + 0.5) / SUPERSAMPLE
        crossings = sorted((a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]), 1 if b[1] > a[1] else -1)
                           for a, b in rows[row])
        winding, start = 0, None
        base = (row // SUPERSAMPLE) * size
        for x, d in crossings:
            if winding == 0:
                start = x
            winding += d
            if winding == 0:
                for col in range(max(0, round(start * SUPERSAMPLE)), min(n, round(x * SUPERSAMPLE))):
                    cover[base + col // SUPERSAMPLE] += 1
    return bytes(min(255, c * 255 // (SUPERSAMPLE * SUPERSAMPLE)) for c in cover)


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def hud_textures(bntx):
    """name -> (width, height, linear pixels) of every texture of HUD_TEXTURES in the BNTX."""
    out, pos = {}, 0
    while (i := bntx.find(b'BRTI', pos)) >= 0:
        pos = i + 4
        name_at = struct.unpack_from('<Q', bntx, i + 0x60)[0]
        if name_at + 2 > len(bntx):
            continue
        length = struct.unpack_from('<H', bntx, name_at)[0]
        name = bntx[name_at + 2:name_at + 2 + length].decode('utf-8', 'replace').split('^')[0]
        if name not in HUD_TEXTURES:
            continue
        mips, fmt = struct.unpack_from('<H', bntx, i + 0x16)[0], struct.unpack_from('<I', bntx, i + 0x1C)[0]
        w, h = struct.unpack_from('<II', bntx, i + 0x24)
        layout = struct.unpack_from('<I', bntx, i + 0x34)[0]
        if fmt != R8_UNORM or mips != 1:
            print(f'  {name}: format {fmt:#x}, {w}x{h}, {mips} mips: not the R8 mask expected, left out')
            continue
        data = struct.unpack_from('<Q', bntx, struct.unpack_from('<Q', bntx, i + 0x70)[0])[0]
        gobs, bh = (w + 63) // 64, 1 << (layout & 7)
        out[name] = (w, h, bytes(bntx[data + gob_offset(x, y, gobs, bh)] for y in range(h) for x in range(w)))
    return out


# ---- the run ----
def read_request(path):
    req = {'font': [], 'restbl': [], 'layout': [], 'bind': {}, 'mouse_camera': 1}
    # (hos/buttons.c writes it in the Windows code page, as its paths are)
    for line in open(path, encoding='mbcs' if os.name == 'nt' else 'utf-8', errors='replace'):
        line = line.rstrip('\r\n')
        if not line or line.startswith('#'):
            continue
        k, _, v = line.partition(' ')
        if k in ('font', 'restbl', 'layout'):
            rel, _, host = v.partition('|')
            req[k].append((rel.replace('\\', '/'), host))
        elif k == 'bind':
            i, _, name = v.partition(' ')
            if int(i) < len(ACTIONS):
                req['bind'][ACTIONS[int(i)]] = name.strip()
        elif k == 'mouse_camera':
            req['mouse_camera'] = int(v)
        else:
            req[k] = v
    return req


def redraw(otf, icons):
    font = TTFont(io.BytesIO(otf))
    cff = font['CFF '].cff
    top = cff.topDictIndex[0]
    charstrings, private = top.CharStrings, top.Private
    nominal = getattr(private, 'nominalWidthX', 0)
    cmap = font.getBestCmap()
    done = []
    for cp, (advance, draw) in icons.items():
        if cp not in cmap:
            continue
        pen = T2CharStringPen(advance - nominal, None)
        draw(pen)
        charstrings[cmap[cp]] = pen.getCharString(private=private, globalSubrs=cff.GlobalSubrs)
        font['hmtx'][cmap[cp]] = (advance, 0)
        done.append(cp)
    glyphs = font.getGlyphSet()
    for cp in done:
        box = BoundsPen(glyphs)
        glyphs[cmap[cp]].draw(box)
        font['hmtx'][cmap[cp]] = (icons[cp][0], int(round(box.bounds[0])) if box.bounds else 0)
    out = io.BytesIO()
    font.save(out)
    return out.getvalue(), len(done)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: button_icons.py <request.txt>')
    t0 = time.time()
    for stream in (sys.stdout, sys.stderr):   # (tool.log: whatever the console's code page)
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass
    req = read_request(sys.argv[1])
    out = req['out']
    stamp = os.path.join(out, 'stamp.txt')
    if os.path.exists(stamp):
        os.remove(stamp)

    dictionaries = {}
    pack = zdecompress(open(req['zsdic'], 'rb').read())
    for lo, hi in sarc_members(pack).values():
        d = zdict(pack[lo:hi])
        dictionaries[zdict_id(d)] = d

    def unpack(host):
        raw = open(host, 'rb').read()
        did = zframe_dict(raw)
        d = dictionaries.get(did) if did else None
        if did and d is None:
            raise SystemExit(f'{host}: needs zstd dictionary {did:#x}, not in Pack/ZsDic.pack.zs')
        return zdecompress(raw, d), d

    archives = []   # (rel, archive, dictionary)
    for rel, host in sorted(req['font'], key=lambda f: f[0] != DEFAULT_ARCHIVE):
        archive, d = unpack(host)
        if FONT in sarc_members(archive):
            archives.append((rel, archive, d))
    if not archives:
        sys.exit('button_icons: no font archive holds ' + FONT)
    text_fonts = []
    for _, archive, _ in archives:
        members = sarc_members(archive)
        if all(n in members for n in TEXT_FONTS):
            text_fonts = [TTFont(io.BytesIO(bfotf_decode(archive[slice(*members[n])])[0])) for n in TEXT_FONTS]
            break
    if not text_fonts:
        sys.exit('button_icons: the text fonts for the key letters were not found')
    g = Glyphs(text_fonts)
    styles = {'keyboard': keyboard_style(g, req['bind'], req['mouse_camera']), 'xbox': xbox_style(g),
              'playstation': playstation_style(g)}

    restbls = [(rel, *unpack(host)) for rel, host in req['restbl']]
    for style, icons in styles.items():
        growth, written, made = {}, [], {}
        for rel, archive, d in archives:
            members = sarc_members(archive)
            otf, key = bfotf_decode(archive[slice(*members[FONT])])
            if otf[:4] != b'OTTO':
                sys.exit(f'button_icons: {FONT} in {rel} did not decode to an OpenType font')
            if (otf, id(d)) not in made:   # (1.4.3: the four archives are the same: made once)
                new_otf, count = redraw(otf, icons)
                new_archive = repack(archive, FONT, bfotf_encode(new_otf, key))
                made[(otf, id(d))] = (new_archive, zcompress(new_archive, d), count)
            new_archive, data, count = made[(otf, id(d))]
            assert zdecompress(data, d) == new_archive
            growth[rel] = len(new_archive) - len(archive)
            write(os.path.join(out, style, rel), data)
            written.append(rel)
            print(f'{style}: {rel}: {count} icons, {growth[rel]:+d} bytes')
        for rel, table, d in restbls:
            write(os.path.join(out, style, rel), zcompress(grow_restbl(table, growth), d))
            written.append(rel)
        with open(os.path.join(out, style, 'files.txt'), 'w', encoding='utf-8') as f:
            f.write(''.join(r + '\n' for r in written))

    # the button guide: each texture's original (its hash, as the renderer sees it decoded) and its redraws
    entries = []
    for rel, host in req['layout']:
        archive, _ = unpack(host)
        members = sarc_members(archive)
        if BNTX not in members:
            continue
        for name, (w, h, pixels) in sorted(hud_textures(archive[slice(*members[BNTX])]).items()):
            masks = []
            for style in STYLES:
                icon = styles[style].get(HUD_TEXTURES[name])
                masks.append(rasterize(icon[1], w) if icon and w == h else pixels)
            entries.append((fnv1a64(pixels), w, h, masks))
            print(f'button guide: {name} {w}x{h} ({fnv1a64(pixels):016x})')
        if entries:
            break
    hud = bytearray(b'SKBH' + struct.pack('<III', 1, len(entries), len(STYLES)))
    for h64, w, h, masks in entries:
        hud += struct.pack('<QII', h64, w, h) + b''.join(masks)
    write(os.path.join(out, 'hud.bin'), bytes(hud))

    with open(stamp, 'w', encoding='utf-8') as f:
        f.write(req['stamp'] + '\n')
    print(f'done in {time.time() - t0:.1f} s')


if __name__ == '__main__':
    main()
