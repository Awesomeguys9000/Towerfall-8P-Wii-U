#!/usr/bin/env python3
"""Set the main-thread stack size in an RPX's SHT_RPL_FILEINFO section (and fix its CRC).

elf2rpl always writes a 64 KB stack; the translated game needs more, and it must run on the
real main thread (ProcUI / HOME menu handling), so we enlarge the main stack instead of
spawning a second thread.

usage: patch_rpx_stack.py file.rpx [stack_size_bytes]
"""
import struct
import sys
import zlib

SHT_RPL_CRCS = 0x80000003
SHT_RPL_FILEINFO = 0x80000004
SHF_RPL_ZLIB = 0x08000000
FILEINFO_STACK_OFFSET = 0x2C   # version, textSize, textAlign, dataSize, dataAlign, loadSize, loadAlign,
                               # tempSize, trampAdjust, sdaBase, sda2Base, stackSize, heapSize


def main():
    path = sys.argv[1]
    stack = int(sys.argv[2], 0) if len(sys.argv) > 2 else 8 * 1024 * 1024
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        sys.exit("not a 32-bit big-endian ELF/RPX")
    e_shoff, = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum = struct.unpack_from(">HH", data, 0x2E)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size = struct.unpack_from(">IIIIII", data, off)
        sections.append((typ, flags, offset, size))
    fi = next((i for i, s in enumerate(sections) if s[0] == SHT_RPL_FILEINFO), None)
    crc = next((i for i, s in enumerate(sections) if s[0] == SHT_RPL_CRCS), None)
    if fi is None:
        sys.exit("no FILEINFO section")
    typ, flags, offset, size = sections[fi]
    if flags & SHF_RPL_ZLIB:
        sys.exit("FILEINFO is compressed (unexpected)")
    old, = struct.unpack_from(">I", data, offset + FILEINFO_STACK_OFFSET)
    if old != 0x10000:
        sys.exit(f"unexpected stack size 0x{old:X} at FILEINFO+0x{FILEINFO_STACK_OFFSET:X}; refusing to patch")
    struct.pack_into(">I", data, offset + FILEINFO_STACK_OFFSET, stack)
    if crc is not None:
        _, _, coff, csize = sections[crc]
        new_crc = zlib.crc32(bytes(data[offset:offset + size])) & 0xFFFFFFFF
        struct.pack_into(">I", data, coff + fi * 4, new_crc)
    open(path, "wb").write(data)
    print(f"{path}: main thread stack 0x{old:X} -> 0x{stack:X}")


if __name__ == "__main__":
    main()
