#!/usr/bin/env python3
"""E03-S03 - prints the audio command list of captured DKR audio tasks.

The task sits at DMEM 0xFC0 as the runtime copied it, native 32-bit words:
data_ptr at +0x30, data_size at +0x34. RDRAM is stored as the runtime holds
it, one native 32-bit word per big-endian N64 word, so an aligned word reads
straight. Usage: dump_alist.py AUD000.BIN [--summary]
"""
import struct
import sys
from collections import Counter

NAMES = ["SPNOOP", "ADPCM", "CLEARBUFF", "ENVMIXER", "LOADBUFF", "RESAMPLE",
         "SAVEBUFF", "SEGMENT", "SETBUFF", "SETVOL", "DMEMMOVE", "LOADADPCM",
         "MIXER", "INTERLEAVE", "POLEF", "SETLOOP"]


def load(path):
    with open(path, "rb") as f:
        magic, version, ucode, size = struct.unpack("<4I", f.read(16))
        assert magic == 0x41524B44 and version == 1, path
        dmem = f.read(0x1000)
        rdram = f.read(size)
    return dmem, rdram


def word(buf, addr):
    return struct.unpack_from("<I", buf, addr)[0]


def commands(dmem, rdram):
    ptr = word(dmem, 0xFC0 + 0x30) & 0xFFFFFF
    size = word(dmem, 0xFC0 + 0x34)
    for i in range(size // 8):
        yield word(rdram, ptr + i * 8), word(rdram, ptr + i * 8 + 4)


def main():
    summary = "--summary" in sys.argv
    for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
        dmem, rdram = load(path)
        cmds = list(commands(dmem, rdram))
        counts = Counter(NAMES[(w0 >> 24) & 0xF] for w0, _ in cmds)
        flags = Counter((NAMES[(w0 >> 24) & 0xF], (w0 >> 16) & 0xFF) for w0, _ in cmds)
        print(f"{path}: {len(cmds)} commands  " +
              " ".join(f"{k}={v}" for k, v in counts.most_common()))
        if summary:
            print("  flags: " + " ".join(f"{n}/{f:02X}={c}" for (n, f), c in sorted(flags.items())))
            continue
        for w0, w1 in cmds:
            print(f"  {NAMES[(w0 >> 24) & 0xF]:10s} {w0:08X} {w1:08X}")


if __name__ == "__main__":
    main()
