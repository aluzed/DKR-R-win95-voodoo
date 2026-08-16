#!/usr/bin/env python3
"""E02-S01 - surveys the Windows 95 exports that do nothing.

    tools/win95/find_stubs.py KERNEL32.DLL USER32.DLL ...
    tools/win95/find_stubs.py --write KERNEL32.DLL      # updates exports/stubs/

An API absent from the export table is a *loud* problem: Windows 95 refuses to
load the program and names the symbol. That is what `check_imports.py` checks.

An API that is **exported but empty** is a silent problem, and therefore worse.
The link succeeds, the load succeeds, the import check is satisfied - and the
function does nothing. That is how `CreateSemaphoreW` nearly carried off the
whole of `ultramodern`'s scheduler: `moodycamel::LightweightSemaphore` calls it,
receives a null handle, and neither its wait nor its signal works afterwards. See
docs/research/win95-blockers.md.

## How a stub is recognised

Windows 95's empty entries share a fixed shape, which can therefore be
recognised mechanically rather than guessed at:

    33 c0              xor  eax,eax     ; return value = 0 (failure)
    b1 XX              mov  cl,index    ; stub number
    e9 XX XX XX XX     jmp  tail        ; common tail

and the common tail sets `ERROR_CALL_NOT_IMPLEMENTED` (120) through
`SetLastError`.

The pattern is recognised here over the first nine bytes of each named export's
code. But "the pattern looks like a stub" would only be an impression, and a
false positive would break everybody's build: the survey is therefore
**verified**, not merely recognised.

The verification is the convergence of the jumps. The stubs of one DLL all jump
to a single common tail - 179 to `0x1319` for KERNEL32, 162 to `0x62c6` for
USER32, 62 to `0x98ea` for GDI32, 176 to `0x1356` for ADVAPI32. One address per
DLL, without exception. The tool requires it and refuses to write a survey that
does not show it.

Further proof when one wants it: several stubs **share the same entry address**.
`LoadLibraryExW` and `MoveFileExW` are at the same one, `CreateEventW` and
`CreateSemaphoreW` too. Two functions with radically different behaviour only
share code when neither has any.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from pe_symbols import PE                                    # noqa: E402

STUBS_DIR = pathlib.Path(__file__).resolve().parent / "exports" / "stubs"

RED, GREEN, YELLOW, OFF = "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[0m"


def find_stubs(path):
    """Returns ([(name, rva, target)], number of named exports).

    `target` is the address the stub jumps to. It is recorded and not discarded:
    it is what turns "the pattern looks like a stub" into "the N stubs of this
    DLL all jump to the same place", that is, into a verification rather than a
    resemblance. See `main`.
    """
    pe = PE(str(path))
    data, ddir = pe.d, pe.ddir

    rva, _ = struct.unpack_from("<II", data, ddir)
    if not rva:
        return [], 0
    e = pe.off(rva)
    n_functions, n_names = struct.unpack_from("<II", data, e + 0x14)
    a_functions, a_names, a_ordinals = struct.unpack_from("<III", data, e + 0x1C)
    off_f, off_n, off_o = pe.off(a_functions), pe.off(a_names), pe.off(a_ordinals)

    stubs = []
    for i in range(n_names):
        name = pe.cstr(pe.off(struct.unpack_from("<I", data, off_n + 4 * i)[0]))
        ordinal = struct.unpack_from("<H", data, off_o + 2 * i)[0]
        if ordinal >= n_functions:
            # Truncated or unreadable table: say so, rather than read past it.
            print(f"  {YELLOW}skipped{OFF} {name}: ordinal {ordinal} outside the "
                  f"{n_functions} entries")
            continue
        func_rva = struct.unpack_from("<I", data, off_f + 4 * ordinal)[0]
        off = pe.off(func_rva)
        if off is None:
            continue
        code = data[off:off + 9]
        # xor eax,eax ; mov cl,imm8 ; jmp rel32
        if (len(code) >= 9 and code[0:2] == b"\x33\xc0"
                and code[2] == 0xB1 and code[4] == 0xE9):
            rel = struct.unpack_from("<i", code, 5)[0]
            stubs.append((name, func_rva, func_rva + 9 + rel))
    return stubs, n_names


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dlls", nargs="+", help="DLLs extracted from the target machine")
    ap.add_argument("--write", action="store_true",
                    help=f"writes the survey into {STUBS_DIR}")
    args = ap.parse_args(argv)

    status = 0
    for dll in args.dlls:
        path = pathlib.Path(dll)
        stem = path.stem.upper()
        stubs, total = find_stubs(path)

        share = (100.0 * len(stubs) / total) if total else 0.0
        colour = RED if share > 50 else (YELLOW if stubs else GREEN)
        print(f"{colour}{stem}{OFF}: {len(stubs)} stubs out of {total} "
              f"named exports ({share:.0f} %)")

        # The convergence of the jumps is what makes the survey trustworthy. A
        # `STUB` fails the build (check_imports.py): a single false positive
        # would block everybody. On the test machine's DLLs, the stubs of one DLL
        # **all** jump to a single address - 179 to 0x1319 for KERNEL32, 162 to
        # 0x62c6 for USER32. Requiring that convergence turns the argument "no
        # plausible false positive" into a verified property.
        targets = sorted({t for _, _, t in stubs})
        if len(targets) > 1:
            status = 1
            print(f"  {RED}REFUSED{OFF}: {len(targets)} distinct jump targets "
                  f"({', '.join(hex(t) for t in targets)})")
            print(f"          The pattern therefore no longer designates a "
                  f"single tail, and the survey is no longer safe.")
            print(f"          Write nothing: check the disassembly before "
                  f"trusting this list.")
            continue
        if targets:
            print(f"  common tail: {hex(targets[0])}")

        if args.write:
            STUBS_DIR.mkdir(parents=True, exist_ok=True)
            target = STUBS_DIR / f"{stem}.txt"
            if stubs:
                target.write_text("".join(f"{n}\n" for n, _, _ in sorted(stubs)))
                print(f"  wrote {target}")
            elif target.exists():
                target.unlink()
                print(f"  removed {target} (no stub)")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
