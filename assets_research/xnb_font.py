#!/usr/bin/env python3
"""
xnb_font.py <font.xnb> [<outdir>]

Decodes an XNA 4.0 SpriteFont .xnb into
    <outdir>/<name>.png   RGBA8 texture exactly as stored (XNA fonts are PREMULTIPLIED:
                          white glyphs, rgb == alpha)
    <outdir>/<name>.json  lineSpacing, spacing, defaultChar, and per glyph:
                          char, code, glyph rect (x,y,w,h in texture), cropping
                          (x,y,w,h: x/y = draw offset, w = unused, h = line height cell),
                          kerning [A, B, C] (left bearing, glyph width, right bearing)
Standard library only (zlib for the PNG). Handles SurfaceFormat Color, Bgr565,
Bgra5551, Bgra4444, Dxt1, Dxt3, Dxt5, Alpha8. XNB LZ4 (FNA, flag 0x40) is supported;
LZX (flag 0x80) is detected and reported (none of TowerFall's fonts use it).
"""
import json
import os
import struct
import sys
import zlib


class R:
    def __init__(self, d, p=0):
        self.d, self.p = d, p

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]; self.p += 4; return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v

    def v7(self):
        r = s = 0
        while True:
            b = self.u8(); r |= (b & 0x7F) << s; s += 7
            if not b & 0x80:
                return r

    def string(self):
        return self.bytes(self.v7()).decode("utf-8")

    def char(self):
        # .NET BinaryReader.ReadChar with UTF-8 encoding
        b0 = self.d[self.p]
        n = 1 if b0 < 0x80 else 2 if b0 < 0xE0 else 3 if b0 < 0xF0 else 4
        return self.bytes(n).decode("utf-8")


def lz4_block(src, out_size):
    out = bytearray(); p = 0
    while p < len(src):
        tok = src[p]; p += 1
        ln = tok >> 4
        if ln == 15:
            while True:
                b = src[p]; p += 1; ln += b
                if b != 255:
                    break
        out += src[p:p + ln]; p += ln
        if p >= len(src):
            break
        off = src[p] | (src[p + 1] << 8); p += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[p]; p += 1; ml += b
                if b != 255:
                    break
        ml += 4
        for _ in range(ml):
            out.append(out[-off])
    return bytes(out[:out_size])


def read_xnb(path):
    d = open(path, "rb").read()
    if d[:3] != b"XNB":
        raise ValueError("not an XNB")
    platform, version, flags = chr(d[3]), d[4], d[5]
    size = struct.unpack_from("<I", d, 6)[0]
    if flags & 0x80:
        raise NotImplementedError("LZX-compressed XNB: decompress with FNA's LzxDecoder first")
    if flags & 0x40:
        dsize = struct.unpack_from("<I", d, 10)[0]
        body = lz4_block(d[14:size], dsize)
    else:
        body = d[10:size]
    return dict(platform=platform, version=version, hidef=bool(flags & 1)), body


def rgb565(c):
    r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def dxt_colors(c0, c1, dxt1):
    a, b = rgb565(c0), rgb565(c1)
    if c0 > c1 or not dxt1:
        c2 = tuple((2 * x + y + 1) // 3 for x, y in zip(a, b))
        c3 = tuple((x + 2 * y + 1) // 3 for x, y in zip(a, b))
        return [a + (255,), b + (255,), c2 + (255,), c3 + (255,)]
    c2 = tuple((x + y) // 2 for x, y in zip(a, b))
    return [a + (255,), b + (255,), c2 + (255,), (0, 0, 0, 0)]


def decode_dxt(data, w, h, kind):
    out = bytearray(w * h * 4)
    bs = 8 if kind == 1 else 16
    p = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            blk = data[p:p + bs]; p += bs
            cb = blk[bs - 8:]
            c0, c1, idx = struct.unpack("<HHI", cb)
            cols = dxt_colors(c0, c1, kind == 1)
            alphas = None
            if kind == 3:
                a64 = struct.unpack("<Q", blk[:8])[0]
                alphas = [((a64 >> (4 * i)) & 15) * 17 for i in range(16)]
            elif kind == 5:
                a0, a1 = blk[0], blk[1]
                bits = int.from_bytes(blk[2:8], "little")
                tab = [a0, a1]
                if a0 > a1:
                    tab += [((6 - i) * a0 + (1 + i) * a1 + 3) // 7 for i in range(6)]
                else:
                    tab += [((4 - i) * a0 + (1 + i) * a1 + 2) // 5 for i in range(4)] + [0, 255]
                alphas = [tab[(bits >> (3 * i)) & 7] for i in range(16)]
            for i in range(16):
                x, y = bx + (i & 3), by + (i >> 2)
                if x >= w or y >= h:
                    continue
                c = list(cols[(idx >> (2 * i)) & 3])
                if alphas is not None:
                    c[3] = alphas[i]
                o = (y * w + x) * 4
                out[o:o + 4] = bytes(c)
    return bytes(out)


def decode_surface(fmt, w, h, data):
    if fmt == 0:  # Color (R,G,B,A bytes)
        return bytes(data[:w * h * 4])
    out = bytearray(w * h * 4)
    if fmt in (1, 2, 3):
        for i in range(w * h):
            c = struct.unpack_from("<H", data, 2 * i)[0]
            if fmt == 1:
                px = rgb565(c) + (255,)
            elif fmt == 2:
                px = (((c >> 10) & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                      (c & 31) * 255 // 31, 255 if c >> 15 else 0)
            else:
                px = (((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17, (c >> 12) * 17)
            out[4 * i:4 * i + 4] = bytes(px)
        return bytes(out)
    if fmt in (4, 5, 6):
        return decode_dxt(data, w, h, {4: 1, 5: 3, 6: 5}[fmt])
    if fmt == 12:  # Alpha8
        for i in range(w * h):
            out[4 * i:4 * i + 4] = bytes((255, 255, 255, data[i]))
        return bytes(out)
    raise NotImplementedError("SurfaceFormat %d" % fmt)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


SURFACE_NAMES = {0: "Color", 1: "Bgr565", 2: "Bgra5551", 3: "Bgra4444", 4: "Dxt1", 5: "Dxt3",
                 6: "Dxt5", 12: "Alpha8"}


def parse_spritefont(body):
    r = R(body)
    readers = [(r.string(), r.i32()) for _ in range(r.v7())]
    shared = r.v7()
    tid = r.v7()
    if "SpriteFontReader" not in readers[tid - 1][0]:
        raise ValueError("primary asset is %s" % readers[tid - 1][0])
    # Texture2D
    r.v7()
    fmt, w, h, mips = r.i32(), r.u32(), r.u32(), r.u32()
    size = r.u32()
    tex = r.bytes(size)
    for _ in range(mips - 1):
        r.bytes(r.u32())

    def rect_list():
        r.v7()
        return [[r.i32(), r.i32(), r.i32(), r.i32()] for _ in range(r.u32())]
    glyphs = rect_list()
    crops = rect_list()
    r.v7()
    chars = [r.char() for _ in range(r.u32())]
    line_spacing = r.i32()
    spacing = r.f32()
    r.v7()
    kerning = [[r.f32(), r.f32(), r.f32()] for _ in range(r.u32())]
    default_char = r.char() if r.u8() else None
    return dict(readers=[x[0].split(",")[0] for x in readers], surface_format=fmt,
                surface_format_name=SURFACE_NAMES.get(fmt, str(fmt)), width=w, height=h,
                mip_levels=mips, tex=tex, glyphs=glyphs, crops=crops, chars=chars,
                line_spacing=line_spacing, spacing=spacing, kerning=kerning,
                default_char=default_char)


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 1
    path = argv[1]
    outdir = argv[2] if len(argv) > 2 else "."
    os.makedirs(outdir, exist_ok=True)
    hdr, body = read_xnb(path)
    f = parse_spritefont(body)
    name = os.path.splitext(os.path.basename(path))[0]
    rgba = decode_surface(f["surface_format"], f["width"], f["height"], f["tex"])
    write_png(os.path.join(outdir, name + ".png"), f["width"], f["height"], rgba)
    meta = dict(source=os.path.basename(path), xnb=hdr, texture=dict(
        width=f["width"], height=f["height"], surface_format=f["surface_format_name"],
        premultiplied=True), line_spacing=f["line_spacing"], spacing=f["spacing"],
        default_char=f["default_char"],
        default_char_code=ord(f["default_char"]) if f["default_char"] else None,
        glyph_count=len(f["chars"]), glyphs=[])
    for i, ch in enumerate(f["chars"]):
        meta["glyphs"].append(dict(char=ch, code=ord(ch), rect=f["glyphs"][i],
                                   cropping=f["crops"][i], kerning=f["kerning"][i]))
    with open(os.path.join(outdir, name + ".json"), "w", encoding="utf-8") as fp:
        json.dump(meta, fp, indent=1, ensure_ascii=False)
    print("%s: %dx%d %s, %d glyphs, lineSpacing=%d spacing=%g default=%r, codes %d..%d"
          % (name, f["width"], f["height"], f["surface_format_name"], len(f["chars"]),
             f["line_spacing"], f["spacing"], f["default_char"],
             min(map(ord, f["chars"])), max(map(ord, f["chars"]))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
