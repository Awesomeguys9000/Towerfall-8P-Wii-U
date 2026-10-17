#!/usr/bin/env python3
"""
extract_music.py <xwb> <xsb> <outdir> [--xgs TowerFallMusic.xgs]

Parses an XACT (v46 / XACT3, as used by XNA 4 / FNA's FACT) WaveBank + SoundBank,
maps every cue to the wave(s) it plays, and writes each wave as a standalone
RIFF file named after the cue:

    <Cue>.wav                         cue plays exactly one wave
    <Cue>__t<track>.wav               cue sound has several tracks (one wave each)
    <Cue>__t<track>v<k>.wav           track is a PlayWaveTrackVariation (k-th variation)
    _unreferenced_wave<NN>.wav        waves no cue points at

Formats: PCM -> WAVE_FORMAT_PCM, MS-ADPCM -> WAVE_FORMAT_ADPCM (with standard
7 coefficients, playable by ffmpeg/SDL), xWMA -> RIFF/XWMA with dpds chunk.
XMA (Xbox only) is dumped raw.
A 'smpl' chunk with the XWB loop region is added to every file.

Also writes <outdir>/music_manifest.json with the full cue/sound/event structure
(loop counts, event timestamps, variation types, category, volumes, formats).
Only the Python standard library is used.
"""
import json
import math
import os
import re
import struct
import sys

FMT_NAMES = {0: "PCM", 1: "XMA", 2: "MS-ADPCM", 3: "xWMA"}
ADPCM_COEFS = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]
VARIATION_TYPES = {0: "ordered", 1: "ordered-from-random", 2: "random",
                   3: "random-no-immediate-repeat", 4: "shuffle"}
BEHAVIOR = {0: "fail", 1: "queue", 2: "replace-oldest", 3: "replace-quietest",
            4: "replace-lowest-priority"}
EVENT_TYPES = {0: "Stop", 1: "PlayWave", 3: "PlayWaveTrackVariation",
               4: "PlayWaveEffectVariation", 6: "PlayWaveTrackEffectVariation",
               7: "Pitch", 8: "Volume", 9: "Marker", 16: "PitchRepeating",
               17: "VolumeRepeating", 18: "MarkerRepeating"}
# wmav2 xWMA tables (from FACT)
WMA_AVG_BYTES = [12000, 24000, 4000, 6000, 8000, 20000, 2500]
WMA_BLOCK_ALIGN = [929, 1487, 1280, 2230, 8917, 8192, 4459, 5945, 2304, 1536, 1485,
                   1008, 2731, 4096, 6827, 5462, 1280]


def vol_byte_to_db(v):
    """XACT volume byte -> decibels (same formula as FACT_INTERNAL_ParseVolumeByte)."""
    return ((-96.0 - 67.7385212334047) /
            (1.0 + math.pow(v / 80.1748600297963, 0.432254984608615))) + 67.7385212334047


def db_to_amp(db):
    return math.pow(10.0, db / 20.0)


class R:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def s16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def skip(self, n):
        self.p += n


# ----------------------------------------------------------------- XWB
def parse_xwb(data):
    if data[:4] != b"WBND":
        raise ValueError("not a little-endian XWB (WBND)")
    r = R(data, 4)
    version, header_version = r.u32(), r.u32()
    segs = [(r.u32(), r.u32()) for _ in range(5)]
    bankdata, meta, seek, names, wavedata = segs
    r.p = bankdata[0]
    flags = r.u32()
    count = r.u32()
    bank_name = data[r.p:r.p + 64].split(b"\0")[0].decode("ascii", "replace"); r.skip(64)
    meta_size = r.u32(); name_size = r.u32(); alignment = r.u32()
    compact_fmt = r.u32()
    compact = bool(flags & 0x20000)
    entries = []
    for i in range(count):
        r.p = meta[0] + i * meta_size
        if compact:
            raise NotImplementedError("compact wavebanks not supported")
        fd = r.u32(); fmt = r.u32()
        play_off, play_len = r.u32(), r.u32()
        loop_start, loop_len = r.u32(), r.u32()
        tag = fmt & 3
        ch = (fmt >> 2) & 7
        rate = (fmt >> 5) & 0x3FFFF
        ba = (fmt >> 23) & 0xFF
        bits16 = fmt >> 31
        e = dict(index=i, flags=fd & 0xF, duration_samples=fd >> 4, format_tag=tag,
                 codec=FMT_NAMES[tag], channels=ch, sample_rate=rate,
                 raw_block_align=ba, bits=(16 if bits16 else 8),
                 offset=wavedata[0] + play_off, length=play_len,
                 loop_start=loop_start, loop_length=loop_len)
        if tag == 2:
            e["block_align"] = (ba + 22) * ch
            e["samples_per_block"] = ((e["block_align"] // ch) - 7) * 2 + 2
            e["bits"] = 4
        elif tag == 0:
            e["block_align"] = ba
        elif tag == 3:
            e["block_align"] = WMA_BLOCK_ALIGN[ba & 0x1F]
            e["avg_bytes"] = WMA_AVG_BYTES[ba >> 5] if (ba >> 5) < len(WMA_AVG_BYTES) else 0
        e["duration_sec"] = round(e["duration_samples"] / rate, 3) if rate else 0
        entries.append(e)
    # seek tables (xWMA): table of per-wave offsets followed by packet tables
    seektab = {}
    if seek[1]:
        base = seek[0]
        for i in range(count):
            off = struct.unpack_from("<I", data, base + 4 * i)[0]
            if off == 0xFFFFFFFF:
                continue
            p = base + 4 * count + off
            n = struct.unpack_from("<I", data, p)[0]
            seektab[i] = list(struct.unpack_from("<%dI" % n, data, p + 4))
    wnames = {}
    if names[1] and (flags & 0x10000):
        for i in range(count):
            raw = data[names[0] + i * name_size: names[0] + (i + 1) * name_size]
            wnames[i] = raw.split(b"\0")[0].decode("ascii", "replace")
    return dict(version=version, header_version=header_version, flags=flags,
                streaming=bool(flags & 1), name=bank_name, alignment=alignment,
                entries=entries, seek=seektab, names=wnames)


def riff(chunks, form=b"WAVE"):
    body = b"".join(cid + struct.pack("<I", len(c)) + c + (b"\0" if len(c) & 1 else b"")
                    for cid, c in chunks)
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + form + body


def smpl_chunk(rate, start, length):
    period = int(round(1e9 / rate)) if rate else 0
    end = start + length - 1 if length else 0
    hdr = struct.pack("<9I", 0, 0, period, 60, 0, 0, 0, 1 if length else 0, 0)
    if length:
        hdr += struct.pack("<6I", 0, 0, start, end, 0, 0)
    return (b"smpl", hdr)


def wave_file_bytes(xwb, data, e):
    raw = data[e["offset"]:e["offset"] + e["length"]]
    ch, rate = e["channels"], e["sample_rate"]
    if e["format_tag"] == 0:
        bits = e["bits"]
        ba = ch * bits // 8
        fmt = struct.pack("<HHIIHH", 1, ch, rate, rate * ba, ba, bits)
        return riff([(b"fmt ", fmt), smpl_chunk(rate, e["loop_start"], e["loop_length"]),
                     (b"data", raw)]), "wav"
    if e["format_tag"] == 2:
        ba, spb = e["block_align"], e["samples_per_block"]
        avg = rate * ba // spb
        fmt = struct.pack("<HHIIHHHHH", 2, ch, rate, avg, ba, 4, 32, spb, 7)
        fmt += b"".join(struct.pack("<hh", a, b) for a, b in ADPCM_COEFS)
        fact = struct.pack("<I", e["duration_samples"])
        return riff([(b"fmt ", fmt), (b"fact", fact),
                     smpl_chunk(rate, e["loop_start"], e["loop_length"]),
                     (b"data", raw)]), "wav"
    if e["format_tag"] == 3:
        fmt = struct.pack("<HHIIHHH", 0x161, ch, rate, e["avg_bytes"], e["block_align"], 16, 0)
        dpds = struct.pack("<%dI" % len(xwb["seek"].get(e["index"], [])),
                           *xwb["seek"].get(e["index"], []))
        return riff([(b"fmt ", fmt), (b"dpds", dpds), (b"data", raw)], form=b"XWMA"), "xwma"
    return raw, "xma"


# ----------------------------------------------------------------- XSB
def parse_events(data, off):
    r = R(data, off)
    n = r.u8()
    events = []
    for _ in range(n):
        info = r.u32()
        rnd = r.u16()
        etype = info & 0x1F
        ev = dict(type=etype, type_name=EVENT_TYPES.get(etype, "?%d" % etype),
                  timestamp_ms=(info >> 5) & 0xFFFF, random_offset_ms=rnd)
        r.skip(1)  # separator 0xFF
        if etype == 1:  # PlayWave
            ev["flags"] = r.u8()
            ev["track"] = r.u16()
            ev["wavebank"] = r.u8()
            ev["loop_count"] = r.u8()
            ev["position"] = r.u16(); ev["angle"] = r.u16()
            ev["waves"] = [dict(track=ev["track"], wavebank=ev["wavebank"])]
        elif etype in (3, 4, 6):
            ev["flags"] = r.u8()
            if etype == 3:
                ev["loop_count"] = r.u8(); ev["position"] = r.u16(); ev["angle"] = r.u16()
            else:
                ev["loop_count"] = r.u8(); ev["position"] = r.u16(); ev["angle"] = r.u16()
                if etype == 4:
                    ev["track"] = r.u16(); ev["wavebank"] = r.u8()
                # effect variation
                ev["pitch_min"] = r.s16(); ev["pitch_max"] = r.s16()
                ev["vol_min"] = r.u8(); ev["vol_max"] = r.u8()
                ev["filter_fmin"] = struct.unpack_from("<f", data, r.p)[0]; r.skip(4)
                ev["filter_fmax"] = struct.unpack_from("<f", data, r.p)[0]; r.skip(4)
                ev["filter_qmin"] = struct.unpack_from("<f", data, r.p)[0]; r.skip(4)
                ev["filter_qmax"] = struct.unpack_from("<f", data, r.p)[0]; r.skip(4)
                r.skip(2)
                ev["variation_flags"] = r.u8()
                if etype == 4:
                    ev["waves"] = [dict(track=ev["track"], wavebank=ev["wavebank"])]
            if etype in (3, 6):
                cnt = r.u16()
                vt = r.u16()
                r.skip(4)
                ev["variation_type"] = VARIATION_TYPES.get(vt & 0xF, vt)
                ev["variation_type_raw"] = vt
                ev["waves"] = []
                for _k in range(cnt):
                    ev["waves"].append(dict(track=r.u16(), wavebank=r.u8(),
                                            weight_min=r.u8(), weight_max=r.u8()))
        elif etype == 0:
            ev["stop_flags"] = r.u8()
        else:
            ev["unparsed"] = True
            events.append(ev)
            break
        events.append(ev)
    return events


def parse_xsb(data):
    if data[:4] != b"SDBK":
        raise ValueError("not an XSB")
    r = R(data, 4)
    content_ver, tool_ver, crc = r.u16(), r.u16(), r.u16()
    r.skip(8); platform = r.u8()
    n_simple, n_complex = r.u16(), r.u16(); r.skip(2)
    n_total = r.u16(); n_wb = r.u8(); n_sounds = r.u16()
    name_len = r.u16(); r.skip(2)
    off_simple, off_complex, off_names = r.u32(), r.u32(), r.u32(); r.skip(4)
    off_var = r.u32(); r.skip(4)
    off_wbnames, off_hash, off_nameidx, off_sounds = r.u32(), r.u32(), r.u32(), r.u32()
    sb_name = data[r.p:r.p + 64].split(b"\0")[0].decode()
    wbnames = [data[off_wbnames + 64 * i: off_wbnames + 64 * (i + 1)].split(b"\0")[0].decode()
               for i in range(n_wb)]

    def sound_at(off):
        s = R(data, off)
        flags = s.u8(); cat = s.u16(); vol = s.u8(); pitch = s.s16(); prio = s.u8()
        s.skip(2)
        ntracks = s.u8() if flags & 1 else 1
        snd = dict(offset=off, flags=flags, category=cat, volume_byte=vol,
                   volume_db=round(vol_byte_to_db(vol), 3), pitch_semitones=pitch / 1000.0,
                   priority=prio, tracks=[])
        if flags & 0x0E:
            ln = s.u16(); s.skip(ln - 2); snd["has_rpc"] = True
        if flags & 0x10:
            ln = s.u16(); s.skip(ln - 2); snd["has_dsp"] = True
        if flags & 1:
            for _ in range(ntracks):
                tv = s.u8(); evoff = s.u32(); filt = s.u8(); q = s.u8(); freq = s.u16()
                snd["tracks"].append(dict(volume_byte=tv, volume_db=round(vol_byte_to_db(tv), 3),
                                          filter=filt, filter_q=q, filter_freq=freq,
                                          events=parse_events(data, evoff)))
        else:
            tr = s.u16(); wb = s.u8()
            snd["tracks"].append(dict(volume_byte=None, events=[dict(
                type=1, type_name="PlayWave(simple)", timestamp_ms=0, loop_count=0,
                waves=[dict(track=tr, wavebank=wb)])]))
        return snd

    cues = []
    for i in range(n_simple):
        c = R(data, off_simple + 5 * i)
        fl = c.u8(); so = c.u32()
        cues.append(dict(kind="simple", flags=fl, sound=sound_at(so)))
    for i in range(n_complex):
        c = R(data, off_complex + 15 * i)
        fl = c.u8()
        cue = dict(kind="complex", flags=fl)
        if fl & 0x04:
            cue["sound"] = sound_at(c.u32())
        else:
            cue["variation_table_offset"] = c.u32(); cue["sound"] = None
        c.skip(4)
        cue["instance_limit"] = c.u8()
        cue["fade_in_ms"] = c.u16(); cue["fade_out_ms"] = c.u16()
        b = c.u8()
        cue["max_instance_behavior"] = BEHAVIOR.get(b >> 3, b >> 3)
        cues.append(cue)
    for i, cue in enumerate(cues):
        noff = struct.unpack_from("<I", data, off_nameidx + 6 * i)[0]
        end = data.index(b"\0", noff)
        cue["name"] = data[noff:end].decode()
        cue["index"] = i
    return dict(content_version=content_ver, tool_version=tool_ver, platform=platform,
                name=sb_name, wavebanks=wbnames, has_variation_table=off_var != 0xFFFFFFFF,
                cues=cues, n_sounds=n_sounds)


# ----------------------------------------------------------------- XGS
def parse_xgs(data):
    if data[:4] != b"XGSF":
        raise ValueError("not an XGS")
    r = R(data, 4)
    r.skip(6 + 8 + 1)
    ncat, nvar = r.u16(), r.u16(); r.skip(4); nrpc, npre, npar = r.u16(), r.u16(), r.u16()
    off_cat, off_var = r.u32(), r.u32(); r.skip(4); off_catidx = r.u32(); r.skip(4)
    off_varidx, off_catname, off_varname = r.u32(), r.u32(), r.u32()
    cats = []
    r.p = off_cat
    for i in range(ncat):
        c = dict(instance_limit=r.u8(), fade_in_ms=r.u16(), fade_out_ms=r.u16())
        b = r.u8(); c["max_instance_behavior"] = BEHAVIOR.get(b >> 3, b >> 3)
        c["parent"] = r.u16(); vb = r.u8()
        c["volume_byte"] = vb; c["volume_db"] = round(vol_byte_to_db(vb), 3)
        c["volume_amp"] = round(db_to_amp(vol_byte_to_db(vb)), 4)
        c["visibility"] = r.u8()
        cats.append(c)
    for i in range(ncat):
        noff = struct.unpack_from("<I", data, off_catidx + 6 * i)[0]
        cats[i]["name"] = data[noff:data.index(b"\0", noff)].decode()
    return dict(categories=cats)


def safe(name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) < 3:
        print(__doc__); return 1
    xwb_path, xsb_path, outdir = args[:3]
    xgs_path = None
    if "--xgs" in argv:
        xgs_path = argv[argv.index("--xgs") + 1]
        args = [a for a in args if a != xgs_path]
    else:
        cand = os.path.join(os.path.dirname(xsb_path), "TowerFallMusic.xgs")
        xgs_path = cand if os.path.exists(cand) else None
    os.makedirs(outdir, exist_ok=True)
    with open(xwb_path, "rb") as f:
        wdata = f.read()
    with open(xsb_path, "rb") as f:
        sdata = f.read()
    xwb = parse_xwb(wdata)
    xsb = parse_xsb(sdata)
    manifest = dict(wavebank={k: v for k, v in xwb.items() if k not in ("entries", "seek")},
                    soundbank={k: v for k, v in xsb.items() if k != "cues"},
                    cues=[], waves=xwb["entries"])
    if xgs_path:
        with open(xgs_path, "rb") as f:
            manifest["engine"] = parse_xgs(f.read())
    used = set()
    for cue in xsb["cues"]:
        snd = cue["sound"]
        entry = dict(cue)
        entry["files"] = []
        if snd is None:
            manifest["cues"].append(entry); continue
        refs = []  # (track_idx, var_idx or None, wave_index)
        for ti, tr in enumerate(snd["tracks"]):
            for ev in tr["events"]:
                ws = ev.get("waves", [])
                for vi, w in enumerate(ws):
                    refs.append((ti, vi if len(ws) > 1 else None, w["track"], ev))
        for ti, vi, widx, ev in refs:
            if len(refs) == 1:
                fname = safe(cue["name"])
            else:
                fname = "%s__t%d" % (safe(cue["name"]), ti) + ("" if vi is None else "v%d" % vi)
            e = xwb["entries"][widx]
            blob, ext = wave_file_bytes(xwb, wdata, e)
            fn = fname + "." + ext
            with open(os.path.join(outdir, fn), "wb") as f:
                f.write(blob)
            used.add(widx)
            entry["files"].append(dict(file=fn, wave=widx, track=ti, variation=vi,
                                       event_timestamp_ms=ev["timestamp_ms"],
                                       loop_count=ev.get("loop_count"),
                                       loops_forever=ev.get("loop_count") == 255))
        manifest["cues"].append(entry)
    for e in xwb["entries"]:
        if e["index"] not in used:
            blob, ext = wave_file_bytes(xwb, wdata, e)
            fn = "_unreferenced_wave%02d.%s" % (e["index"], ext)
            with open(os.path.join(outdir, fn), "wb") as f:
                f.write(blob)
    with open(os.path.join(outdir, "music_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    # human summary
    for c in manifest["cues"]:
        files = ", ".join("%s(w%d,%s,t=%dms)" % (x["file"], x["wave"],
                          "loop" if x["loops_forever"] else "loop%s" % x["loop_count"],
                          x["event_timestamp_ms"]) for x in c["files"])
        print("%-22s %-7s %s" % (c["name"], c["kind"], files))
    print("waves:", len(xwb["entries"]), "referenced:", len(used))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
