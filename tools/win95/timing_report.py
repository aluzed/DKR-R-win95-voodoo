#!/usr/bin/env python3
"""E08-S01 - reads the timing export (DKR_TIMING_EXPORT, timing_export.hpp).

    tools/win95/timing_report.py FRAMES.BIN [--csv out.csv] [--skip-s 60]
    tools/win95/timing_report.py AUDIO.BIN  [--csv out.csv] [--skip-s 60]

Prints the median, 99th percentile and worst of each column, exact rather than
binned as in the log, over the records after --skip-s seconds (to leave the
loading out). --csv writes every record for a spreadsheet or a plot.

FRAMES.BIN, one record per display list:
    t_ms, period_us (0 after a pause), render_us, triangles
AUDIO.BIN, one record per audio task:
    t_ms (the task's end), task_us, reserved, samples_queued (since boot, both channels)
"""
import argparse
import struct
import sys

COLUMNS = {
    "FRAMES": ("t_ms", "period_us", "render_us", "triangles"),
    "AUDIO": ("t_ms", "task_us", "reserved", "samples_queued"),
}


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"DKRT":
        sys.exit(f"{path}: not a timing export")
    version, size = struct.unpack_from("<II", data, 4)
    if version != 1 or size != 16:
        sys.exit(f"{path}: version {version}, record size {size}: unknown")
    body = data[12:]
    count = len(body) // size
    return [struct.unpack_from("<IIII", body, i * size) for i in range(count)]


def percentile(sorted_values, q):
    if not sorted_values:
        return 0
    rank = max(0, -(-len(sorted_values) * q // 100) - 1)
    return sorted_values[rank]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("export")
    ap.add_argument("--csv")
    ap.add_argument("--skip-s", type=float, default=0.0,
                    help="ignore the records of the first seconds")
    args = ap.parse_args()

    kind = "AUDIO" if "AUDIO" in args.export.upper() else "FRAMES"
    names = COLUMNS[kind]
    records = read(args.export)
    if not records:
        sys.exit("no records")

    if args.csv:
        with open(args.csv, "w") as out:
            out.write(",".join(names) + "\n")
            for r in records:
                out.write(",".join(str(v) for v in r) + "\n")

    start = records[0][0] + int(args.skip_s * 1000)
    kept = [r for r in records if r[0] >= start]
    span = (kept[-1][0] - kept[0][0]) / 1000.0 if len(kept) > 1 else 0.0
    print(f"{kind}: {len(records)} records, {len(kept)} kept over {span:.1f} s")
    if kind == "FRAMES":
        periods = [r[1] for r in kept if r[1]]
        if span > 0:
            print(f"  rate: {len(kept) / span:.2f} display lists a second")
        rows = [("period_us", periods), ("render_us", [r[2] for r in kept]),
                ("triangles", [r[3] for r in kept])]
    else:
        rows = [("task_us", [r[1] for r in kept])]
        if span > 0 and len(kept) > 1:
            frames = kept[-1][3] - kept[0][3]
            print(f"  produced: {frames / span:.0f} samples a second")
    for name, values in rows:
        v = sorted(values)
        if not v:
            continue
        print(f"  {name:10} p50={percentile(v, 50):>8} p99={percentile(v, 99):>8} "
              f"worst={v[-1]:>8} mean={sum(v) // len(v):>8}")


if __name__ == "__main__":
    main()
