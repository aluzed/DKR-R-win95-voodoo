#!/usr/bin/env python3
"""E09-S03 - no stream opened on a std::filesystem::path in the Windows 95 build.

A `std::ifstream` or `std::ofstream` constructed from a `path` opens it with
`_wfopen` on this toolchain, and Windows 95 exports the wide CRT empty: the
stream fails without a word, and the reader takes that for an absent file. It
cost a page fault in SAVEMGR.EXE and five silent readers in the game (3 October
2026). `dkr::fs::stream_name(path)` is the remedy.

The check reads the sources `DKRR.EXE` compiles or includes from
`runtime-recomp/src/game`, plus the test sources built for the target, and
flags a stream constructor whose first argument looks like a path: a name
ending in `path` or `_directory`, a call ending in `Path()`, or a `/`
concatenation. An argument already wrapped in `stream_name(...)`, ending in
`.string()` or `.c_str()`, or a string literal passes. A line that is right
for another reason carries `// DKR-WIN95-STREAM-OK` and the reason.

It reads text, not types, so it can be fooled; it caught every case found by
hand when it was written.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GAME = os.path.join(ROOT, "runtime-recomp", "src", "game")


def game_closure():
    cmake = open(os.path.join(ROOT, "cmake", "win95-target.cmake")).read()
    names = re.search(r"set\(DKR_WIN95_GAME_SOURCES(.*?)\)", cmake, re.S).group(1).split()
    roots = [os.path.join(GAME, n + ".cpp") for n in names]
    seen, stack = set(), list(roots)
    while stack:
        path = os.path.normpath(stack.pop())
        if path in seen or not os.path.exists(path):
            continue
        seen.add(path)
        text = open(path, errors="ignore").read()
        for include in re.findall(r'#include\s+"([^"]+)"', text):
            for candidate in (os.path.join(os.path.dirname(path), include),
                              os.path.join(GAME, include)):
                if os.path.exists(candidate):
                    stack.append(candidate)
                    break
    # The test sources the target builds: the portable suites and SAVEMGR's.
    tests = set()
    suites = os.path.join(ROOT, "tools", "tests", "portable-suites.txt")
    for line in open(suites):
        fields = line.split()
        if fields and not fields[0].startswith("#"):
            tests.add(os.path.join(ROOT, "runtime-recomp", "tests", fields[2]))
    for name in ("save_manager_tests.cpp", "dkr_save_codec_tests.cpp"):
        tests.add(os.path.join(ROOT, "runtime-recomp", "tests", name))
    return sorted(p for p in seen if "/src/game/" in p) + sorted(tests)


STREAM = re.compile(r"std::(?:i|o)?fstream\s+\w+\s*[({]\s*([^,;)}]+(?:\([^()]*\))?)")
PATHY = re.compile(r"(path|_directory)\s*$|Path\(\)\s*$|\s/\s|\)\s*/\s*\"")
SAFE = re.compile(r"stream_name\(|\.string\(\)|\.c_str\(\)|^\s*\"")


def main():
    problems = []
    for source in game_closure():
        for number, line in enumerate(open(source, errors="ignore"), 1):
            if "DKR-WIN95-STREAM-OK" in line:
                continue
            for match in STREAM.finditer(line):
                argument = match.group(1).strip()
                if SAFE.search(argument):
                    continue
                if PATHY.search(argument):
                    problems.append(f"{os.path.relpath(source, ROOT)}:{number}: {line.strip()}")
    if problems:
        print("streams opened on a path (wide on Windows 95; use dkr::fs::stream_name):")
        for p in problems:
            print("  " + p)
        return 1
    print("  ok    no stream opened on a path in the Windows 95 build's sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
