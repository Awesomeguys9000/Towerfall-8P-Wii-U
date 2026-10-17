#!/usr/bin/env python3
"""Generate the Wii U .wuhb packaging images (stdlib only).

    python3 port/wiiu/make_assets.py [path/to/Content/Atlas]

Outputs (next to this script):
    icon.png        128x128
    tv-splash.png   1280x720
    drc-splash.png  854x480

If the game's menuAtlas.png/.xml are found (default: ../../TF-8P/Content/Atlas), the
TowerFall title logo is cropped from the atlas and scaled up with nearest-neighbour.
Otherwise a plain "TOWERFALL 8P" text logo drawn with a built-in bitmap font is used.
"""
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


# ------------------------------------------------------------------ PNG I/O
def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", chunk)
            assert depth == 8 and ctype in (2, 6) and inter == 0, "unsupported PNG format"
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        if f == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        o = y * w * 4
        if bpp == 4:
            out[o:o + w * 4] = line
        else:
            for x in range(w):
                out[o + x * 4:o + x * 4 + 3] = line[x * 3:x * 3 + 3]; out[o + x * 4 + 3] = 255
        prev = line
    return w, h, out


def write_png(path, w, h, rgba):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


# ------------------------------------------------------------------ canvas
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)

    def gradient(self, top, bottom):
        for y in range(self.h):
            t = y / max(1, self.h - 1)
            c = bytes(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + b"\xff"
            self.px[y * self.w * 4:(y + 1) * self.w * 4] = c * self.w

    def blend(self, x, y, r, g, b, a):
        if a == 0 or x < 0 or y < 0 or x >= self.w or y >= self.h: return
        o = (y * self.w + x) * 4
        if a == 255:
            self.px[o:o + 4] = bytes((r, g, b, 255)); return
        ia = 255 - a
        self.px[o] = (r * a + self.px[o] * ia) // 255
        self.px[o + 1] = (g * a + self.px[o + 1] * ia) // 255
        self.px[o + 2] = (b * a + self.px[o + 2] * ia) // 255
        self.px[o + 3] = 255

    def rect(self, x0, y0, w, h, col):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w): self.blend(x, y, *col)

    def sprite(self, spr, sw, sh, dx, dy, scale):
        for y in range(sh):
            for x in range(sw):
                o = (y * sw + x) * 4
                a = spr[o + 3]
                if not a: continue
                r, g, b = spr[o], spr[o + 1], spr[o + 2]
                for yy in range(scale):
                    for xx in range(scale):
                        self.blend(dx + x * scale + xx, dy + y * scale + yy, r, g, b, a)

    def save(self, path):
        write_png(path, self.w, self.h, self.px)
        print("wrote", path, f"{self.w}x{self.h}")


# 5x7 bitmap font for the fallback logo / subtitle
FONT = {
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
    " ": ["....."] * 7,
}


def text_width(s, scale):
    return len(s) * 6 * scale - scale


def draw_text(cv, s, x, y, scale, col, shadow=None):
    if shadow:
        draw_text(cv, s, x + scale, y + scale, scale, shadow)
    for ch in s:
        g = FONT.get(ch, FONT[" "])
        for gy, row in enumerate(g):
            for gx, c in enumerate(row):
                if c == "#": cv.rect(x + gx * scale, y + gy * scale, scale, scale, col)
        x += 6 * scale


def draw_text_centered(cv, s, cy, scale, col, shadow=None):
    draw_text(cv, s, (cv.w - text_width(s, scale)) // 2, cy, scale, col, shadow)


# ------------------------------------------------------------------ atlas logo
def load_logo(atlas_dir):
    png = os.path.join(atlas_dir, "menuAtlas.png")
    xml = os.path.join(atlas_dir, "menuAtlas.xml")
    if not (os.path.isfile(png) and os.path.isfile(xml)):
        return None
    regions = {}
    for m in re.finditer(r'name="([^"]+)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)"', open(xml).read()):
        regions[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    aw, ah, apx = read_png(png)

    def crop(name):
        x, y, w, h = regions[name]
        out = bytearray(w * h * 4)
        for yy in range(h):
            s = ((y + yy) * aw + x) * 4
            out[yy * w * 4:(yy + 1) * w * 4] = apx[s:s + w * 4]
        return w, h, out

    try:
        return {"bg": crop("title/titleBg"), "letters": crop("title/titleLetters"),
                "eight": crop("title/eight/0")}
    except KeyError:
        return None


def draw_logo(cv, logo, cx, cy, scale):
    """Draw titleBg with the letters centred on it, plus the '8' badge; (cx,cy) = centre."""
    bw, bh, bpx = logo["bg"]
    lw, lh, lpx = logo["letters"]
    x0 = cx - bw * scale // 2
    y0 = cy - bh * scale // 2
    cv.sprite(bpx, bw, bh, x0, y0, scale)
    cv.sprite(lpx, lw, lh, cx - lw * scale // 2, cy - lh * scale // 2, scale)
    ew, eh, epx = logo["eight"]
    cv.sprite(epx, ew, eh, cx - ew * scale // 2, y0 + bh * scale - eh * scale // 2, scale)


BG_TOP = (14, 12, 30)
BG_BOT = (44, 22, 46)
GOLD = (255, 214, 92, 255)
SHADOW = (0, 0, 0, 160)
WHITE = (230, 230, 240, 255)


def make(path, w, h, logo, logo_scale, sub_scale, subtitle=True):
    cv = Canvas(w, h)
    cv.gradient(BG_TOP, BG_BOT)
    if logo:
        cy = h // 2 - (h // 12 if subtitle else 0)
        draw_logo(cv, logo, w // 2, cy, logo_scale)
    else:
        s1 = max(2, w // 64)
        draw_text_centered(cv, "TOWERFALL", h // 2 - 9 * s1, s1, GOLD, SHADOW)
        draw_text_centered(cv, "8P", h // 2 + s1, s1, WHITE, SHADOW)
    if subtitle:
        draw_text_centered(cv, "WII U", h - 7 * sub_scale - h // 28, sub_scale, WHITE, SHADOW)
    cv.save(path)


def main():
    atlas = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "TF-8P", "Content", "Atlas")
    logo = load_logo(atlas)
    print("atlas logo:", "found" if logo else "not found, using text logo")
    make(os.path.join(HERE, "tv-splash.png"), 1280, 720, logo, 4, 4)
    make(os.path.join(HERE, "drc-splash.png"), 854, 480, logo, 2, 3)
    # icon: logo at 1x would be 274 wide, so use the letters only at scale 1 cropped by
    # drawing at a smaller centre; simplest: text icon if the logo doesn't fit
    cv = Canvas(128, 128)
    cv.gradient(BG_TOP, BG_BOT)
    if logo:
        ew, eh, epx = logo["eight"]
        lw, lh, lpx = logo["letters"]
        # downscale letters by 2 (nearest) so they fit 128 px
        sw, sh = lw // 2, lh // 2
        small = bytearray(sw * sh * 4)
        for y in range(sh):
            for x in range(sw):
                s = ((y * 2) * lw + x * 2) * 4
                small[(y * sw + x) * 4:(y * sw + x) * 4 + 4] = lpx[s:s + 4]
        cv.sprite(small, sw, sh, (128 - sw) // 2, 14, 1)
        cv.sprite(epx, ew, eh, (128 - ew * 2) // 2, 128 - eh * 2 - 8, 2)
    else:
        draw_text_centered(cv, "TOWER", 30, 3, GOLD, SHADOW)
        draw_text_centered(cv, "FALL", 58, 3, GOLD, SHADOW)
        draw_text_centered(cv, "8P", 90, 3, WHITE, SHADOW)
    cv.save(os.path.join(HERE, "icon.png"))


if __name__ == "__main__":
    main()
