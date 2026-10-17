#!/usr/bin/env python3
"""
disasm_ps.py <effect.xnb> [...]

Finds every D3D9 shader blob (vs_/ps_ 1.x-3.0) inside an XNB-wrapped fx_2_0 effect,
prints its constant table (CTAB) and a disassembly in Microsoft-style syntax.
Standard library only. Supports the ps_2_0 instruction subset (plus common SM3 ops).
"""
import struct
import sys

OPS = {
    0: ("nop", 0, 0), 1: ("mov", 1, 1), 2: ("add", 1, 2), 3: ("sub", 1, 2), 4: ("mad", 1, 3),
    5: ("mul", 1, 2), 6: ("rcp", 1, 1), 7: ("rsq", 1, 1), 8: ("dp3", 1, 2), 9: ("dp4", 1, 2),
    10: ("min", 1, 2), 11: ("max", 1, 2), 12: ("slt", 1, 2), 13: ("sge", 1, 2), 14: ("exp", 1, 1),
    15: ("log", 1, 1), 16: ("lit", 1, 1), 17: ("dst", 1, 2), 18: ("lrp", 1, 3), 19: ("frc", 1, 1),
    20: ("m4x4", 1, 2), 21: ("m4x3", 1, 2), 22: ("m3x4", 1, 2), 23: ("m3x3", 1, 2),
    24: ("m3x2", 1, 2), 32: ("pow", 1, 2), 33: ("crs", 1, 2), 34: ("sgn", 1, 3), 35: ("abs", 1, 1),
    36: ("nrm", 1, 1), 37: ("sincos", 1, 3), 65: ("texkill", 1, 0), 66: ("texld", 1, 2),
    78: ("expp", 1, 1), 79: ("logp", 1, 1), 80: ("cnd", 1, 3), 88: ("cmp", 1, 3),
    89: ("bem", 1, 2), 90: ("dp2add", 1, 3), 91: ("dsx", 1, 1), 92: ("dsy", 1, 1),
    93: ("texldd", 1, 4), 95: ("texldl", 1, 2),
}
REGT = {0: "r", 1: "v", 2: "c", 3: "t", 4: "rasout", 5: "attrout", 6: "o", 7: "i", 8: "oC",
        9: "oDepth", 10: "s", 11: "c", 12: "c", 13: "c", 14: "b", 15: "aL", 16: "half",
        17: "misc", 18: "l", 19: "p"}
SRCMOD = {0: "{}", 1: "-{}", 2: "{}_bias", 3: "-{}_bias", 4: "{}_bx2", 5: "-{}_bx2",
          6: "{}_x2", 7: "-{}_x2", 8: "{}_dz", 9: "{}_dw", 10: "{}_abs", 11: "-{}_abs", 13: "!{}"}
USAGE = {0: "position", 1: "blendweight", 2: "blendindices", 3: "normal", 4: "psize",
         5: "texcoord", 6: "tangent", 7: "binormal", 8: "tessfactor", 9: "positiont",
         10: "color", 11: "fog", 12: "depth", 13: "sample"}
TEXTYPE = {2: "2d", 3: "cube", 4: "volume"}


def regtype(tok):
    return ((tok >> 28) & 7) | ((tok >> 8) & 0x18)


def reg(tok):
    t = regtype(tok)
    return "%s%d" % (REGT.get(t, "?%d" % t), tok & 0x7FF)


def dst(tok):
    m = (tok >> 16) & 0xF
    mods = (tok >> 20) & 0xF
    s = reg(tok)
    if m != 0xF:
        s += "." + "".join(c for i, c in enumerate("xyzw") if m & (1 << i))
    suf = ("_sat" if mods & 1 else "") + ("_pp" if mods & 2 else "") + ("_centroid" if mods & 4 else "")
    return suf, s


def src(tok):
    sw = (tok >> 16) & 0xFF
    comps = "".join("xyzw"[(sw >> (2 * i)) & 3] for i in range(4))
    if comps == "xyzw":
        comps = ""
    elif len(set(comps)) == 1:
        comps = comps[0]
    s = reg(tok) + ("." + comps if comps else "")
    return SRCMOD.get((tok >> 24) & 0xF, "?{}").format(s)


def parse_ctab(words, start, nwords):
    blob = struct.pack("<%dI" % nwords, *words[start:start + nwords])
    if blob[:4] != b"CTAB":
        return None
    b = blob[4:]
    size, creator, version, count, cinfo, flags, target = struct.unpack_from("<7I", b, 0)
    out = []
    for i in range(count):
        name, rset, ridx, rcnt, _r, tinfo, defv = struct.unpack_from("<I4HII", b, cinfo + 20 * i)
        nm = b[name:b.index(b"\0", name)].decode()
        cls, typ, rows, cols, elems, members, moff = struct.unpack_from("<6HI", b, tinfo)
        out.append(dict(name=nm, set=["bool", "int4", "float4", "sampler"][rset],
                        reg=ridx, count=rcnt, rows=rows, cols=cols))
    tgt = b[target:b.index(b"\0", target)].decode()
    return dict(target=tgt, constants=out)


def disasm(words, i):
    ver = words[i]
    kind = "ps" if (ver >> 16) == 0xFFFF else "vs"
    major, minor = (ver >> 8) & 0xFF, ver & 0xFF
    lines = ["%s_%d_%d" % (kind, major, minor)]
    ctab = None
    i += 1
    while i < len(words):
        tok = words[i]
        op = tok & 0xFFFF
        if tok == 0x0000FFFF:
            break
        if op == 0xFFFE:
            n = (tok >> 16) & 0x7FFF
            ctab = parse_ctab(words, i + 1, n) or ctab
            i += 1 + n
            continue
        length = (tok >> 24) & 0xF
        args = words[i + 1:i + 1 + length]
        if op == 0x1F:  # dcl
            usage = args[0]
            t = regtype(args[1])
            if t == 10:
                lines.append("dcl_%s %s" % (TEXTYPE.get((usage >> 27) & 0xF, "?"), reg(args[1])))
            else:
                suf, d = dst(args[1])
                if kind == "ps" and major < 3:
                    lines.append("dcl%s %s" % (suf, d))
                else:
                    lines.append("dcl_%s%d %s" % (USAGE.get(usage & 0x1F, "?"), (usage >> 16) & 0xF, d))
        elif op == 0x51:  # def
            f = struct.unpack("<4f", struct.pack("<4I", *args[1:5]))
            lines.append("def %s, %s" % (reg(args[0]), ", ".join(repr(round(x, 9)) for x in f)))
        else:
            name = OPS.get(op, ("op%d" % op, 1, 0))[0]
            if args:
                suf, d = dst(args[0])
                ss = [src(a) for a in args[1:]]
                lines.append("%s%s %s" % (name, suf, ", ".join([d] + ss)))
            else:
                lines.append(name)
        i += 1 + length
    return lines, ctab, i


def main(paths):
    for p in paths:
        data = open(p, "rb").read()
        print("=" * 70)
        print(p)
        # scan dword-aligned positions relative to each possible base alignment
        found = set()
        for base in range(4):
            n = (len(data) - base) // 4
            words = list(struct.unpack_from("<%dI" % n, data, base))
            for wi, w in enumerate(words):
                if w in (0xFFFF0200, 0xFFFF0201, 0xFFFF0300, 0xFFFE0200, 0xFFFE0300, 0xFFFE0101, 0xFFFF0101, 0xFFFF0104) \
                        and wi + 1 < len(words) and (words[wi + 1] & 0xFFFF) == 0xFFFE:
                    off = base + 4 * wi
                    if off in found:
                        continue
                    found.add(off)
                    lines, ctab, end = disasm(words, wi)
                    print("-- shader at file offset 0x%x" % off)
                    if ctab:
                        for c in ctab["constants"]:
                            print("//   %-16s %-7s %s%d (count %d)" % (c["name"], c["set"],
                                  "s" if c["set"] == "sampler" else "c", c["reg"], c["count"]))
                    for l in lines:
                        print("    " + l)


if __name__ == "__main__":
    main(sys.argv[1:])
