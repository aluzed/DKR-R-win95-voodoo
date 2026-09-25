#!/usr/bin/env python3
"""E08-S01 - turns a DKR_TRACE_SAMPLER capture (D:\\SAMPLES.BIN) into a profile.

    sampler_report.py SAMPLES.BIN build/win95/bin/DKRR.EXE [--top N] [--thread]
                      [--module DKRR.EXE]

--module names the executable as the target loaded it, when the file given
here was renamed.

Every record is one thread's EIP at one millisecond. Samples in the executable
are attributed to functions through its own symbols (the target is not
stripped); samples elsewhere to the module that contains them. A thread blocked
in the kernel is sampled too, usually in KERNEL32: the module view shows that as
such rather than hiding it, since "blocked" and "burning time in KERNEL32" are
not told apart by an address.
"""
import bisect
import collections
import os
import struct
import subprocess
import sys


def read(path):
    data = open(path, "rb").read()
    assert data[:4] == b"DKRS", "not a sampler capture"
    pos, modules, records = 4, [], []
    while pos < len(data):
        tag = data[pos:pos + 1]
        (n,) = struct.unpack_from("<I", data, pos + 1)
        pos += 5
        if tag == b"M":
            text = data[pos:pos + n].decode("latin-1")
            pos += n
            modules = []
            for line in text.splitlines():
                base, size, name = line.split(" ", 2)
                modules.append((int(base, 16), int(size, 16), name))
        elif tag == b"R":
            for i in range(n):
                records.append(struct.unpack_from("<II", data, pos + 8 * i))
            pos += 8 * n
        else:
            break   # a truncated tail: the game was closed mid-write
    return modules, records


def symbols(exe):
    nm = os.environ.get("NM", "i686-w64-mingw32-nm")
    out = subprocess.run([nm, "-C", "--defined-only", exe], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in "tT":
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def main():
    path, exe = sys.argv[1], sys.argv[2]
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
    modules, records = read(path)
    if "--all" not in sys.argv:
        # A thread asleep in the kernel keeps the same EIP from one sample to the
        # next -- and on Windows 95 that EIP is often not in KERNEL32 but at the
        # return address after the call into the wait (dkr_condvar_wait,
        # dkr_mutex_lock). A thread that computes moves. Keep only samples whose
        # EIP differs from that thread's previous one; --all keeps everything.
        last, moving = {}, []
        for eip, thread in records:
            if last.get(thread) != eip:
                moving.append((eip, thread))
            last[thread] = eip
        print(f"{len(records)} samples, {len(moving)} moving (a thread's EIP changed since its last sample)")
        records = moving
    addrs, names = symbols(exe)
    exe_name = (sys.argv[sys.argv.index("--module") + 1] if "--module" in sys.argv
                else "DKRR.EXE").upper()

    def where(eip):
        for base, size, name in modules:
            if base <= eip < base + size:
                if name.upper() == exe_name:
                    i = bisect.bisect_right(addrs, eip) - 1
                    return name, names[i] if i >= 0 else "?"
                return name, name
        return "?", "?"

    by_module, by_function = collections.Counter(), collections.Counter()
    by_thread = collections.defaultdict(collections.Counter)
    for eip, thread in records:
        module, function = where(eip)
        by_module[module] += 1
        by_function[(module, function)] += 1
        by_thread[thread][(module, function)] += 1
    total = len(records)
    print(f"{total} samples, {len(by_thread)} threads")
    print("\nby module:")
    for m, c in by_module.most_common():
        print(f"  {100 * c / total:5.1f}%  {m}")
    exe_total = sum(c for (m, _), c in by_function.items() if m.upper() == exe_name)
    print(f"\n{exe_name}, top {top} functions ({exe_total} samples):")
    for (m, f), c in by_function.most_common():
        if m.upper() != exe_name:
            continue
        print(f"  {100 * c / exe_total:5.1f}%  {f[:100]}")
        top -= 1
        if top == 0:
            break
    if "--hot" in sys.argv:
        # The hottest addresses inside one function, to read against objdump -d.
        wanted = sys.argv[sys.argv.index("--hot") + 1]
        hot = collections.Counter()
        for eip, _ in records:
            module, function = where(eip)
            if function == wanted:
                hot[eip] += 1
        n = sum(hot.values())
        print(f"\nhottest addresses in {wanted} ({n} samples):")
        for eip, c in hot.most_common(12):
            print(f"  {eip:08X}  {100 * c / max(n, 1):5.1f}%")
    if "--thread" in sys.argv:
        for t, counter in sorted(by_thread.items()):
            n = sum(counter.values())
            print(f"\nthread {t}: {n} samples")
            for (m, f), c in counter.most_common(6):
                print(f"  {100 * c / n:5.1f}%  {m}: {f[:80]}")


if __name__ == "__main__":
    main()
