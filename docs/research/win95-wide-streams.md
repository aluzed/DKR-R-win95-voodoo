# `std::ofstream(path)` does not work under Windows 95

A measurement from
[E02-S05](../stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md),
taken on the test machine on 13 August 2026.

## The report

`tools/win95/witnesses/wide_stream_probe.cpp`, run on the machine:

```text
  ofstream(path)                     : FAILED
  ofstream(path.string())            : OK
  ofstream(narrow literal)           : OK
  narrow fopen                       : OK
  ifstream(path)                     : FAILED
  ifstream(path.string())            : OK
```

## Why

Under MinGW, `std::filesystem::path::value_type` is `wchar_t`. Passing a `path` to
a stream constructor therefore selects the wide overload, which opens the file
through `_wfopen` — and Windows 9x exports that whole family as **stubs**: the
symbol is there, the binary loads, and the open fails.

It is the third category of unavailable API, and the most expensive to diagnose:

| | Does the binary load? | Does the import check see it? |
|---|---|---|
| **absent** symbol | no | yes, it names it |
| **stubbed** symbol | yes | yes, if it is declared |
| **present symbol that refuses** | yes | **no** |

`_wfopen` belongs to the second. The import check does not complain since the
symbol exists; nothing distinguishes the call that succeeds from the one that
fails, except execution.

## How it showed itself

Not through a clear message. The `save_manager_tests` suite, which passes on the
host, was dying on the machine:

```text
reset failed: Could not create the temporary save file.
Assertion failed: false, file runtime-recomp/tests/save_manager_tests.cpp, line 68
```

A `std::ofstream` that does not open, with no usable error code. The reasoning led
to the right place, but **this platform has already disproved five of this
repository's assumptions**: the probe was therefore written before the fix.

## What it changed

`path.string()` is narrow everywhere and returns the same bytes elsewhere: the fix
costs nothing to the targets that already worked.

| | Sites |
|---|---:|
| game sources and trial suites | 20 |
| `librecomp`'s core (patch 0019) | 5 |
| `librecomp`'s mod system — out of scope | 3 |

`check-cpp-subset.py` now refuses a stream constructed on anything other than a
narrow string. Its self-test exercises it in both directions, and two written
waivers cover the cases a textual check cannot settle — a variable named
`temporary` which is already a `std::string`.

## And a second stub, on the same path

With the streams opening correctly, the suite went further and then failed
differently (the machine's system messages are in French; the text is translated
here):

```text
reset failed: Could not activate the imported save:
              This function is only valid in Win32 mode
```

That is error 120, `ERROR_CALL_NOT_IMPLEMENTED`: **`MoveFileExW`**.
`save_manager::ReplaceFileAtomic` was calling it directly under
`#if defined(_WIN32)`.

E02-S05 had already measured that `MoveFileExA` refuses under Windows 95 — it is
the durable-write sequence's reason for being — but the direct call escaped the
seam. It now takes the fallback, and the function's name is contradicted in a
comment: **on this target, the replacement is not atomic**, and that is why the
caller takes a backup copy first.

## And `<fstream>` itself would not load

Found along the way, and more serious still: the mere inclusion of `<fstream>`
made the binary unloadable. libstdc++'s `basic_file.o` imports `__imp___fstat64`,
which Windows 95's MSVCRT does not export — it has only the original `_fstat`
family.

Thirteen of the project's files include `<fstream>`, among them `librecomp`'s
`recomp.cpp`, `pi.cpp` and `sp.cpp`: without a fix, the game would not have linked
for this target.

`platform/win95/compat.c` therefore supplies `_fstat64`, built on `GetFileType`
and `GetFileSize`. What libstdc++ uses is narrow — `st_mode` to know whether the
descriptor denotes an ordinary file, `st_size` to say how many bytes remain to be
read — and the rest of the structure is zeroed rather than filled by guesswork: a
false date would look like data.

## Reproducing

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o WPROBE.EXE tools/win95/witnesses/wide_stream_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive

scripts/Push-To-Win95-VM.sh WPROBE.EXE     # then run it, read D:\WPROBE.TXT
```
