#!/usr/bin/env python3
"""Inspect a FAT16 image's clean-shutdown flag, and clear it knowingly.

E09-S01. Windows 95 clears bit 15 of FAT entry 1 while a volume is mounted and
sets it again on a clean dismount. On this target crashes are the normal case,
so the transfer disk is routinely left flagged dirty, and mtools then refuses it
with `Error reading FAT` -- a message that names neither the cause nor the
remedy, and that reads exactly like a corrupt image.

    tools/win95/fat_volume.py IMAGE                 report
    tools/win95/fat_volume.py IMAGE --clear-dirty   report, then clear if sound
    tools/win95/fat_volume.py --self-test           exercise the walk

## Why it walks the tree before clearing

Clearing the flag asserts "this volume is sound". That assertion has to be
earned, or the flag becomes a formality that hides the one case it exists for.
So the walk answers two different questions, and they carry opposite verdicts:

  - **lost clusters** -- allocated in the FAT, reachable from no directory
    entry. The harmless residue of a write interrupted partway. ScanDisk would
    reclaim the space; nothing reads them, and leaving them costs only disk.
  - **cross-linked clusters** -- claimed by two files at once. Real corruption:
    one of the two files already holds the other's data, and no flag clearing
    repairs that. There the answer is to restore the image.

The distinction matters more than it looks. A first version of this walk counted
3124 lost clusters on a healthy volume because it read only the root directory
and never recursed into the eleven subdirectories holding 127 of the files. The
true figure was 31. A walk that does not recurse does not measure what it
reports.
"""

import argparse
import struct
import sys


class Fat16:
    """The subset of FAT16 this needs: the BPB, the FAT, and the directory tree."""

    def __init__(self, data, part_offset=0):
        self.data = data
        self.base = part_offset
        bs = data[self.base:self.base + 512]
        if bs[510:512] != b"\x55\xaa":
            raise ValueError("no boot signature at offset %d" % part_offset)
        self.bps = struct.unpack_from("<H", bs, 11)[0]
        self.spc = bs[13]
        self.resv = struct.unpack_from("<H", bs, 14)[0]
        self.nfat = bs[16]
        self.rootent = struct.unpack_from("<H", bs, 17)[0]
        tot16 = struct.unpack_from("<H", bs, 19)[0]
        self.spf = struct.unpack_from("<H", bs, 22)[0]
        tot32 = struct.unpack_from("<I", bs, 32)[0]
        self.total = tot16 or tot32
        if not (self.bps and self.spc and self.spf and self.total):
            raise ValueError("implausible BPB: not a FAT16 volume")
        self.fat_off = self.base + self.resv * self.bps
        self.root_off = self.fat_off + self.nfat * self.spf * self.bps
        self.data_off = self.root_off + self.rootent * 32
        used = self.resv + self.nfat * self.spf + (self.rootent * 32) // self.bps
        self.nclus = (self.total - used) // self.spc

    # --- the FAT itself ---------------------------------------------------

    def fat(self, index=0):
        off = self.fat_off + index * self.spf * self.bps
        return self.data[off:off + self.spf * self.bps]

    def entry(self, n, index=0):
        return struct.unpack_from("<H", self.fat(index), n * 2)[0]

    def fats_agree(self):
        first = self.fat(0)
        return all(self.fat(i) == first for i in range(1, self.nfat))

    def is_dirty(self):
        """Bit 15 of entry 1 cleared means the volume was not dismounted."""
        return (self.entry(1) & 0x8000) == 0

    # --- the tree ---------------------------------------------------------

    def chain(self, start):
        out, seen, c = [], set(), start
        while 2 <= c < self.nclus + 2 and c not in seen:
            out.append(c)
            seen.add(c)
            c = self.entry(c)
        return out

    def cluster(self, c):
        off = self.data_off + (c - 2) * self.spc * self.bps
        return self.data[off:off + self.spc * self.bps]

    def walk(self):
        """Return (files, dirs, owner, cross). `owner` maps cluster -> path."""
        owner, cross, counts = {}, [], {"files": 0, "dirs": 0}

        def claim(path, start):
            for c in self.chain(start):
                if c in owner:
                    cross.append((path, c, owner[c]))
                else:
                    owner[c] = path

        def entries(blob):
            for i in range(len(blob) // 32):
                e = blob[i * 32:(i + 1) * 32]
                if e[0] == 0x00:
                    return
                # Deleted, long-name fragment, volume label: none is a file.
                if e[0] == 0xE5 or e[11] == 0x0F or (e[11] & 0x08):
                    continue
                name = e[:11].decode("ascii", "replace").strip()
                if name in (".", ".."):
                    continue
                yield (name,
                       struct.unpack_from("<H", e, 26)[0],
                       bool(e[11] & 0x10))

        def descend(blob, path):
            subs = []
            for name, start, isdir in entries(blob):
                full = path + "/" + name
                claim(full, start)
                if isdir:
                    counts["dirs"] += 1
                    subs.append((start, full))
                else:
                    counts["files"] += 1
            for start, full in subs:
                if start:
                    descend(b"".join(self.cluster(c) for c in self.chain(start)),
                            full)

        descend(self.data[self.root_off:self.root_off + self.rootent * 32], "")
        return counts["files"], counts["dirs"], owner, cross

    def lost_clusters(self, owner):
        allocated = {c for c in range(2, self.nclus + 2) if self.entry(c) != 0}
        return sorted(allocated - set(owner))


def report(fs):
    files, dirs, owner, cross = fs.walk()
    lost = fs.lost_clusters(owner)
    kb = len(lost) * fs.spc * fs.bps // 1024
    print("  volume       : FAT16, %d clusters of %d KB"
          % (fs.nclus, fs.spc * fs.bps // 1024))
    print("  clean flag   : %s" % ("DIRTY" if fs.is_dirty() else "clean"))
    print("  FAT copies   : %s" % ("agree" if fs.fats_agree() else "DIFFER"))
    print("  tree         : %d files, %d directories, %d clusters referenced"
          % (files, dirs, len(owner)))
    print("  lost         : %d clusters (%d KB)" % (len(lost), kb))
    print("  cross-linked : %s" % (len(cross) if cross else "none"))
    for path, c, other in cross[:5]:
        print("      cluster %d claimed by %s and %s" % (c, path, other))
    return cross


def clear_dirty(path, fs):
    """Set bit 15 of entry 1 back, in every copy of the FAT."""
    with open(path, "r+b") as f:
        for i in range(fs.nfat):
            off = fs.fat_off + i * fs.spf * fs.bps + 2
            f.seek(off)
            value = struct.unpack("<H", f.read(2))[0] | 0x8000
            f.seek(off)
            f.write(struct.pack("<H", value))


def self_test():
    """Build a volume in memory, damage it deliberately, check both verdicts."""
    bps, spc, resv, nfat, rootent, spf, total = 512, 1, 1, 2, 16, 4, 256
    bs = bytearray(bps)
    bs[0:3] = b"\xeb\x3c\x90"
    struct.pack_into("<H", bs, 11, bps)
    bs[13] = spc
    struct.pack_into("<H", bs, 14, resv)
    bs[16] = nfat
    struct.pack_into("<H", bs, 17, rootent)
    struct.pack_into("<H", bs, 19, total)
    struct.pack_into("<H", bs, 22, spf)
    bs[510:512] = b"\x55\xaa"

    fat = bytearray(spf * bps)
    struct.pack_into("<H", fat, 0, 0xFFF8)
    struct.pack_into("<H", fat, 2, 0xFFFF)          # clean
    struct.pack_into("<H", fat, 2 * 2, 0xFFFF)      # FILE: one cluster, ends
    struct.pack_into("<H", fat, 3 * 2, 0xFFFF)      # allocated, unreferenced

    root = bytearray(rootent * 32)
    root[0:11] = b"FILE    BIN"
    root[11] = 0x20
    struct.pack_into("<H", root, 26, 2)
    struct.pack_into("<I", root, 28, bps)

    img = bytes(bs) + bytes(fat) * nfat + bytes(root)
    img += bytes(total * bps - len(img))

    fs = Fat16(img)
    ok = True

    def want(what, got, expected):
        nonlocal ok
        good = got == expected
        ok = ok and good
        print("  %s %s (%r)" % ("ok   " if good else "FAIL ", what, got))

    files, dirs, owner, cross = fs.walk()
    want("one file found", files, 1)
    want("its cluster is referenced", sorted(owner), [2])
    want("cluster 3 is reported lost", fs.lost_clusters(owner), [3])
    want("no cross-link on a sound volume", cross, [])
    want("the clean flag reads clean", fs.is_dirty(), False)

    # Dirty it: clear bit 15 of entry 1, as Windows 95 does on mount.
    dirty = bytearray(img)
    struct.pack_into("<H", dirty, bps + 2, 0x7FFF)
    want("a cleared bit 15 reads dirty", Fat16(bytes(dirty)).is_dirty(), True)

    # Cross-link: a second entry pointing at the same start cluster. This is the
    # case that must never be waved through, so it is the one worth an assertion.
    linked = bytearray(img)
    doff = bps + spf * bps * nfat
    linked[doff + 32:doff + 43] = b"OTHER   BIN"
    linked[doff + 32 + 11] = 0x20
    struct.pack_into("<H", linked, doff + 32 + 26, 2)
    struct.pack_into("<I", linked, doff + 32 + 28, bps)
    _, _, _, cross2 = Fat16(bytes(linked)).walk()
    want("two files sharing a cluster cross-link", len(cross2), 1)

    # A FAT copy that differs must be seen: it is the other real corruption.
    split = bytearray(img)
    struct.pack_into("<H", split, bps + spf * bps + 4, 0x1234)
    want("differing FAT copies are seen", Fat16(bytes(split)).fats_agree(), False)

    print("  %s" % ("the tool works" if ok else "the tool is broken"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", nargs="?", help="disk image")
    ap.add_argument("--offset", type=int, default=63 * 512,
                    help="partition offset in bytes (default: sector 63)")
    ap.add_argument("--clear-dirty", action="store_true",
                    help="clear the flag if the walk finds no cross-link")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()

    if a.self_test:
        return self_test()
    if not a.image:
        ap.error("an image is required")

    with open(a.image, "rb") as f:
        data = f.read()
    fs = Fat16(data, a.offset)

    print("%s" % a.image)
    cross = report(fs)

    if not a.clear_dirty:
        return 0
    if not fs.is_dirty():
        print("  nothing to do: the volume is already flagged clean")
        return 0
    if cross or not fs.fats_agree():
        print("  REFUSED: this volume is corrupt, not merely dirty.\n"
              "  Clearing the flag would assert a soundness the walk denies.\n"
              "  Restore the image instead.", file=sys.stderr)
        return 1
    clear_dirty(a.image, fs)
    print("  cleared: the volume is flagged clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
