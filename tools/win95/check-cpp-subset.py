#!/usr/bin/env python3
"""E01-S02 - refuses the standard headers forbidden on the Windows 95 target.

    tools/win95/check-cpp-subset.py platform/win95 runtime-recomp/src
    tools/win95/check-cpp-subset.py --self-test

There is **no C++ standard to restrict**: GCC 13 implements all of C++20 for
`i686-w64-mingw32`. What is forbidden is library facilities that bring symbols
Windows 95 does not export into the import table - and under Windows 95, a
missing import stops the process from starting, even if the function is never
called.

The count of missing symbols attached to each header is measured, not presumed:
see `tools/win95/probes/build-probes.sh` and `docs/CPP-SUBSET.md`.

Without an automatic check, a forbidden include reappears with the first
contribution and is only discovered on launching on the target machine.
"""
import argparse
import pathlib
import re
import sys
import tempfile

# header -> (number of missing symbols, replacement)
FORBIDDEN = {
    "thread":             (6,  "CreateThread, through the E02-S01 layer"),
    "mutex":              (6,  "CRITICAL_SECTION, through the E02-S01 layer"),
    "shared_mutex":       (6,  "CRITICAL_SECTION, through the E02-S01 layer"),
    "condition_variable": (6,  "Win32 events, through the E02-S01 layer"),
    "future":             (6,  "the E02-S01 layer"),
    "latch":              (6,  "the E02-S01 layer"),
    "barrier":            (6,  "the E02-S01 layer"),
    "semaphore":          (6,  "the E02-S01 layer"),
    "stop_token":         (6,  "the E02-S01 layer"),
    # <filesystem> is NOT here, and that is measured: see FILESYSTEM_OPERATIONS.
    "syncstream":         (6,  "not applicable on this target"),
}

# --- <filesystem>: the include is free, the operations are not ----------------
#
# This file banned `<filesystem>` outright for a long time, attributing thirteen
# missing symbols to it. **Measurement says otherwise** (E02-S05, probes in
# `docs/research/win95-filesystem.md`):
#
#     #include <filesystem> alone           0 blocking symbols
#     a std::filesystem::path object        1 - LoadLibraryW, and that is a stub
#     a call to exists()                   17 - of which 7 genuinely missing
#
# The distinction is the one that counts: a stub lets the binary load, a missing
# symbol stops it. A `path` is therefore usable under Windows 95, and it was
# verified on the machine - construction, parent_path, filename, extension,
# concatenation, all correct.
#
# Banning the header would therefore have forced 250 uses of the type to be
# rewritten for no gain, and let the problem look solved while the ~140
# operation calls, the only ones that count, remained.
#
# So we watch the operations, and them alone.
FILESYSTEM_OPERATIONS = (
    "absolute", "canonical", "copy", "copy_file", "create_directories",
    "create_directory", "current_path", "directory_iterator", "exists",
    "file_size", "is_directory", "is_regular_file", "is_symlink",
    "last_write_time", "recursive_directory_iterator", "remove", "remove_all",
    "rename", "space", "status", "symlink_status", "temp_directory_path",
    "weakly_canonical",
)
RX_FS_OPERATION = re.compile(
    r'\bstd::filesystem::(' + "|".join(FILESYSTEM_OPERATIONS) + r')\b')

# --- Streams opened on a `path` -----------------------------------------------
#
# Under MinGW, `std::filesystem::path::value_type` is `wchar_t`. Handing a `path`
# to a stream constructor therefore opens the file through `_wfopen` - the wide C
# library - and Windows 95 exports that family as stubs: the binary loads, and
# every open fails in silence.
#
# It is the third category of unavailable API, and the most expensive to
# diagnose: the import check says nothing, since the symbol is right there. The
# defect showed up as a save suite dying on "Could not create the temporary save
# file" while the same code passed on the host. Measured:
# tools/win95/witnesses/wide_stream_probe.cpp.
#
#     ofstream(path)             FAILS
#     ofstream(path.string())    OK
#
# `path.string()` is narrow everywhere and returns the same bytes elsewhere: the
# fix costs the targets that already worked nothing.
RX_STREAM = re.compile(r'\bstd::(?:basic_)?[io]?fstream\s*(?:\w+\s*)?[({]')

def _first_argument(code, start):
    """The first argument, with balanced parentheses.

    A naive split on the comma would cut `p.string()` in half and lead to the
    wrong conclusion that the open is wide - the exact error this rule is meant
    to prevent."""
    depth = 0
    for j in range(start, len(code)):
        c = code[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return code[start:j]
            depth -= 1
        elif c == "," and depth == 0:
            return code[start:j]
    return code[start:]

def stream_opens_on_path(code):
    """Returns the offending argument, or None if the open is narrow."""
    m = RX_STREAM.search(code)
    if not m:
        return None
    arg = _first_argument(code, m.end()).strip()
    if not arg:
        return None
    # A function declaration, not an open: `std::ifstream f(const path&`
    if arg.startswith("const ") or "&" in arg or "*" in arg:
        return None
    # Already narrow: a literal, `.string()`, `.c_str()`, or an argv.
    if (arg.startswith('"') or arg.endswith(".string()") or
            arg.endswith(".c_str()") or arg.startswith("argv")):
        return None
    return arg


# --- Synchronisation types: the uses, not only the includes -------------------
#
# This file watched `#include <thread>` and `#include <mutex>`, and that is not
# enough: `librecomp` includes them nowhere directly - they arrive transitively -
# and `recomp.cpp` nonetheless built the game thread with `std::thread`. Nothing
# protested. The binary loaded under Windows 95 and died at startup on
# std::system_error, "Resource temporarily unavailable": pthread_create failing
# behind the standard library.
#
# **A check that reads includes cannot see a use.** That is exactly the lesson
# <filesystem> had already given, where the watch bears on the operations and not
# on the header. We apply the same rule here.
SYNC_TYPES = (
    "thread", "jthread", "mutex", "recursive_mutex", "timed_mutex",
    "shared_mutex", "lock_guard", "unique_lock", "scoped_lock", "shared_lock",
    "condition_variable", "condition_variable_any", "call_once", "once_flag",
    "async", "future", "promise", "packaged_task", "this_thread",
)
RX_SYNC_USE = re.compile(r'\bstd::(' + "|".join(SYNC_TYPES) + r')\b')

SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".cxx")
RX_INCLUDE = re.compile(r'^\s*#\s*include\s*<([A-Za-z0-9_./]+)>')

# Line-by-line waiver, for the one legitimate case: an include placed in a
# preprocessor branch the Windows 95 target never compiles.
#
# E02-S02's indirection point is one - it includes <thread>, <mutex> and
# <condition_variable> in its `#else`, the modern targets' branch. This checker
# reads text and not the preprocessor's state; without a waiver it would refuse a
# correct file, and the habit would then be to disable it, which costs far more.
#
# The justification is mandatory and its minimum length enforced, as for the
# import check's `exceptions.json`: a waiver with no written reason is the start
# of a list where the tool gets silenced.
RX_ALLOW = re.compile(r'DKR-WIN95-ALLOW\s*:\s*(.+?)\s*(?:\*/)?\s*$')
ALLOW_MIN_JUSTIFICATION = 30

RED, GREEN, YELLOW, BLUE, OFF = (
    "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[1;34m", "\033[0m")


def strip_comments(line, in_block):
    """Returns (code with comments removed, still inside a block?).

    Deliberately simple: no strings and no twisted cases. A misrecognised comment
    would at worst miss a report on a line that contained a real one next to a
    false one, which has never happened; doing it properly would need a lexer,
    for no gain."""
    out = []
    i = 0
    while i < len(line):
        if in_block:
            end = line.find("*/", i)
            if end == -1:
                return "".join(out), True
            i, in_block = end + 2, False
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        out.append(line[i])
        i += 1
    return "".join(out), in_block


def strip_strings(code):
    """Removes string and character literals.

    `strip_comments` was not enough: `threading.cpp` carries the message
    "a std::mutex would have deadlocked here", which tripped the alarm. A check
    that punishes explaining what one has replaced discourages the explanation -
    the opposite of what this repository wants."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        if c in "\"'":
            quote = c
            i += 1
            while i < n and code[i] != quote:
                i += 2 if code[i] == "\\" else 1
            i += 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out)


def scan(paths, quiet=False):
    """Returns the list of offending (file, line, header) triples."""
    bad = []
    allowed = []
    files = 0
    for p in paths:
        root = pathlib.Path(p)
        candidates = ([root] if root.is_file()
                      else [f for f in root.rglob("*") if f.suffix in SOURCE_SUFFIXES])
        for f in candidates:
            if f.suffix not in SOURCE_SUFFIXES:
                continue
            files += 1
            try:
                text = f.read_text("latin-1", errors="ignore")
            except OSError:
                continue
            lines = text.splitlines()
            in_block_comment = False
            for i, line in enumerate(lines, 1):
                # The code alone, comments removed. Without this,
                # **documenting** that `std::filesystem::exists` was replaced
                # trips the alarm, which discourages writing it down - exactly
                # the opposite of what this repository wants to encourage.
                code, in_block_comment = strip_comments(line, in_block_comment)
                code = strip_strings(code)
                fs = RX_FS_OPERATION.search(code)
                if fs:
                    allow = RX_ALLOW.search(lines[i - 2]) if i >= 2 else None
                    why = allow.group(1).strip() if allow else ""
                    if allow and len(why) >= ALLOW_MIN_JUSTIFICATION:
                        allowed.append((f, i, "filesystem::" + fs.group(1), why))
                    else:
                        bad.append((f, i, "filesystem::" + fs.group(1)))
                sync = RX_SYNC_USE.search(code)
                if sync:
                    allow = RX_ALLOW.search(lines[i - 2]) if i >= 2 else None
                    why = allow.group(1).strip() if allow else ""
                    if allow and len(why) >= ALLOW_MIN_JUSTIFICATION:
                        allowed.append((f, i, "sync::" + sync.group(1), why))
                    else:
                        bad.append((f, i, "sync::" + sync.group(1)))
                arg = stream_opens_on_path(code)
                if arg:
                    allow = RX_ALLOW.search(lines[i - 2]) if i >= 2 else None
                    why = allow.group(1).strip() if allow else ""
                    if allow and len(why) >= ALLOW_MIN_JUSTIFICATION:
                        allowed.append((f, i, "stream on path <" + arg + ">", why))
                    else:
                        bad.append((f, i, "stream on path <" + arg + ">"))
                m = RX_INCLUDE.match(line)
                if not (m and m.group(1) in FORBIDDEN):
                    continue
                # The waiver goes on the line just above.
                allow = RX_ALLOW.search(lines[i - 2]) if i >= 2 else None
                if allow:
                    why = allow.group(1).strip()
                    if len(why) < ALLOW_MIN_JUSTIFICATION:
                        print(f"  {RED}WAIVER REFUSED{OFF}  {f}:{i - 1}")
                        print(f"            justification too short "
                              f"({len(why)} < {ALLOW_MIN_JUSTIFICATION} characters)")
                        bad.append((f, i, m.group(1)))
                    else:
                        allowed.append((f, i, m.group(1), why))
                    continue
                bad.append((f, i, m.group(1)))
    if not quiet:
        print(f"{BLUE}==>{OFF} {files} file(s) examined")
        for f, line, header, why in allowed:
            print(f"  {YELLOW}ALLOWED{OFF}  {f}:{line} <{header}> - {why}")
    return bad


def report(bad):
    for f, line, header in bad:
        if header.startswith("filesystem::"):
            print(f"  {RED}FORBIDDEN{OFF}  {f}:{line}")
            print(f"            `std::{header}` - a file-system operation")
            print(f"            replacement: platform/win95/fileio.h (E02-S05)")
            continue
        if header.startswith("sync::"):
            name = header.split("::", 1)[1]
            print(f"  {RED}FORBIDDEN{OFF}  {f}:{line}")
            print(f"            `std::{name}` - a synchronisation type")
            print(f"            <thread> and <mutex> pull in six missing symbols,")
            print(f"            and the include may be transitive: it is the use")
            print(f"            that is watched, not the #include line.")
            print(f"            replacement: dkr::sync (E02-S02)")
            continue
        if header.startswith("stream on path"):
            print(f"  {RED}FORBIDDEN{OFF}  {f}:{line}")
            print(f"            {header} - MinGW then opens through _wfopen, and")
            print(f"            Windows 95 exports the wide family as stubs:")
            print(f"            the binary loads and the open fails.")
            print(f"            replacement: pass `.string()` (E02-S05)")
            continue
        n, replacement = FORBIDDEN[header]
        print(f"  {RED}FORBIDDEN{OFF}  {f}:{line}")
        print(f"            <{header}> - {n} symbols missing from Windows 95")
        print(f"            replacement: {replacement}")


def self_test():
    """A broken checker and a satisfied checker keep quiet the same way: we hand
    it a forbidden include and a permitted one."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "clean.cpp").write_text(
            "#include <vector>\n#include <span>\n#include <format>\n"
            "#include <atomic>\n#include <chrono>\nint main(){return 0;}\n")
        (tmp / "dirty.cpp").write_text(
            "#include <vector>\n#include <thread>\nint main(){return 0;}\n")
        # A file-system operation: forbidden, where the include and the `path`
        # type are not. That is the distinction this checker missed for a long
        # time, so it has to be tested in both directions.
        (tmp / "fs_type.cpp").write_text(
            "#include <filesystem>\n"
            "static std::filesystem::path p{\"a\"};\n"
            "int main(){ return (int)p.string().size(); }\n")
        (tmp / "fs_call.cpp").write_text(
            "#include <filesystem>\n"
            "int main(){ return (int)std::filesystem::exists(\"a\"); }\n")
        # Valid waiver: a branch not compiled on the target, reason written.
        (tmp / "waiver.cpp").write_text(
            "// DKR-WIN95-ALLOW: modern-target branch, never compiled here\n"
            "#include <thread>\nint main(){return 0;}\n")
        # Refused waiver: a reason too short to say anything at all.
        (tmp / "chatty.cpp").write_text(
            "// DKR-WIN95-ALLOW: because\n"
            "#include <thread>\nint main(){return 0;}\n")

        print(f"{BLUE}==>{OFF} clean witness: span, format, atomic, chrono")
        bad = scan([tmp / "clean.cpp"], quiet=True)
        if bad:
            report(bad)
            print(f"{RED}the clean witness is refused - the checker is too strict{OFF}")
            return 1
        print(f"  {GREEN}accepted{OFF}")

        print(f"{BLUE}==>{OFF} dirty witness: <thread>")
        bad = scan([tmp / "dirty.cpp"], quiet=True)
        if not bad:
            print(f"{RED}the dirty witness is accepted - the checker detects nothing{OFF}")
            return 1
        report(bad)
        print(f"  {GREEN}correctly refused{OFF}")

        print(f"{BLUE}==>{OFF} <filesystem>: the path type is allowed")
        bad = scan([tmp / "fs_type.cpp"], quiet=True)
        if bad:
            report(bad)
            print(f"{RED}std::filesystem::path is refused - yet it is usable "
                  f"under Windows 95, measured on the machine{OFF}")
            return 1
        print(f"  {GREEN}accepted{OFF}")

        print(f"{BLUE}==>{OFF} <filesystem>: an operation is refused")
        bad = scan([tmp / "fs_call.cpp"], quiet=True)
        if not bad:
            print(f"{RED}std::filesystem::exists is accepted - it pulls in seven "
                  f"missing symbols and the binary would not load{OFF}")
            return 1
        report(bad)
        print(f"  {GREEN}correctly refused{OFF}")

        print(f"{BLUE}==>{OFF} justified waiver: a branch that is not compiled")
        bad = scan([tmp / "waiver.cpp"], quiet=True)
        if bad:
            report(bad)
            print(f"{RED}the justified waiver is refused{OFF}")
            return 1
        print(f"  {GREEN}accepted{OFF}")

        print(f"{BLUE}==>{OFF} waiver with no serious reason")
        bad = scan([tmp / "chatty.cpp"], quiet=True)
        if not bad:
            print(f"{RED}a waiver with no justification is accepted - the list "
                  f"will become the place where the tool gets silenced{OFF}")
            return 1
        print(f"  {GREEN}correctly refused{OFF}")
    print(f"{BLUE}==>{OFF} {GREEN}the checker works{OFF}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="files or directories to examine")
    ap.add_argument("--self-test", action="store_true",
                    help="test the checker by injection")
    # A ratchet, for the components known to still carry dated debt.
    # `ultramodern` is down to one include - <filesystem>, which E02-S05 must
    # remove. Without this setting the check would either be disabled on
    # `ultramodern` or block it wrongly; with it, the debt is a number, it cannot
    # grow, and the day it falls to zero the number gets updated in the CMake
    # rather than sitting there with nobody noticing.
    ap.add_argument("--max", type=int, default=0, metavar="N",
                    help="tolerate at most N forbidden includes (default 0)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.paths:
        ap.print_help()
        return 2

    bad = scan(args.paths)
    if bad:
        report(bad)
        if len(bad) <= args.max:
            print(f"  {YELLOW}{len(bad)} forbidden include(s){OFF}, "
                  f"tolerated up to {args.max} - known debt, it must not grow.")
            return 0
        print(f"  {RED}{len(bad)} forbidden include(s){OFF}"
              + (f", beyond the {args.max} tolerated" if args.max else "")
              + " - see docs/CPP-SUBSET.md")
        return 1
    if args.max:
        # The ratchet has done its job: it must be loosened, otherwise it stops
        # protecting against reintroduction.
        print(f"  {GREEN}no forbidden include{OFF} - the tolerance of "
              f"{args.max} has no reason to remain, remove it from the CMake.")
        return 0
    print(f"  {GREEN}no forbidden include{OFF}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
