# What `<filesystem>` really costs under Windows 95

Measurements from
[E02-S05](../stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md), taken
on the test machine.

This repository banned `<filesystem>` wholesale, attributing thirteen absent
symbols to it. **The rule was wrong**, and expensive: it would have forced 250 uses
of a type that works perfectly to be rewritten.

## Three probes, three different answers

| What the program does | Blocking symbols | Does the binary load? |
|---|---:|---|
| `#include <filesystem>` alone | **0** | yes |
| a `std::filesystem::path` object | **1** — `LoadLibraryW` | **yes** — it is a stub |
| a call to `exists()` | **17** | **no** — 7 are absent |

The distinction between the last two rows is the one that decides everything, and
it is the one [E02-S01](win95-blockers.md)'s stub survey allows one to make:

- a **stub** is exported and does nothing. The loader is content, the program
  starts.
- an **absent** symbol stops the process from starting, and Windows names the
  symbol in an error box.

The seventeen in detail:

```
STUB     CreateFileW  DeleteFileW  GetDiskFreeSpaceExW  GetFileAttributesW
         GetFullPathNameW  GetTempPathW  GetVolumeInformationW  LoadLibraryW
         MoveFileExW  RemoveDirectoryW
MISSING  CreateHardLinkW  FindFirstVolumeW  FindNextVolumeW  FindVolumeClose
         GetFileSizeEx  MSVCRT:_fstat64  MSVCRT:_wstat64
```

## `std::filesystem::path` works, verified on the machine

The type pulls in only one stub, so the binary loads. What remained was whether
what `libstdc++` makes of it holds. `FSPATH.EXE`'s report on Windows 95:

```text
path                : D:\GAME\SAVE.DAT
parent_path         : D:\GAME
filename            : SAVE.DAT
extension           : .DAT
concatenation       : D:\GAME\SAVE.DAT\OTHER.DAT
verdict             : USABLE
```

Construction, decomposition, concatenation: all correct. `path` is string
manipulation, and string manipulation asks nothing of the system.

## What that changes in the work

The inventory of uses, over `ultramodern`, `librecomp` and `runtime-recomp/src`:

| | Occurrences | To do |
|---|---:|---|
| `std::filesystem::path` — the **type** | **250** | **nothing** |
| operations — `remove`, `exists`, `create_directories`, `copy_file`, `rename`, `is_directory`, `directory_iterator`, `file_size`, `absolute`… | ~140 | to route through `platform/win95/fileio.h` |

Banning the header would therefore have made us rewrite the 250 for no gain, **and**
led us to believe the problem solved while the 140 that matter remained. That is
the opposite of what is needed: we would have paid dearly to be wrong.

The subset checker now watches the **operations**, and them alone. Its self-test
exercises it in both directions: a `path` must pass, an `exists()` must be refused
— because a checker that is too strict ends up disabled, and one that is too lax is
good for nothing.

An immediate effect: `ultramodern` **no longer has a single forbidden include**,
and its `--max 1` ratchet has been removed — the checker flagged it itself. There
remain 23 reports in `librecomp` and 112 in the game's sources, which are E02-S05's
and E02-S02's real work.

## Two traps met while wiring the seam

Neither concerns `<filesystem>` itself, and both are worth writing down because
they nearly led to a wrong conclusion.

### The instruction-set verifier was accusing wrongly

A binary using `std::filesystem::path` was refused with two instructions "outside
the Pentium II set": `movaps %xmm0,(%eax)` and `movnti %eax,(%edx)`.

They were not code. The linker places the exception tables —
`.gcc_except_table` — **inside `.text`**, and `objdump -d` disassembles them like
the rest; data bytes there decode into instructions the processor never executes.

The false positive is not benign: it fails a correct build, and the natural
reaction to a guard rail that cries wolf is to disable it.
`check-instruction-set.sh` now follows the current symbol and ignores the data
regions. Its self-test, which injects real SSE, still refuses.

That trap had nearly gone unnoticed in the other direction too: the first `path`
probe had only been subjected to the **import** check, not the instruction one. It
ran on the emulated machine, which proved nothing about a real Pentium II.

### `std::random_device` does not work under Windows 95

The presence of `std::filesystem::path` in a binary brings libstdc++'s
`std::random_device::_M_getentropy` into it, which calls libmsvcrt's `rand_s`,
which calls **`LoadLibraryW`** then `GetProcAddress` to reach the system's
generator.

`LoadLibraryW` is a stub. `rand_s` therefore obtains a null function pointer, and
**calling `std::random_device` would jump into it**.

The import itself is harmless — a stub does not prevent loading — and manipulating
a `path` does not touch it, which the probe confirms on the machine. But the
conclusion is worth keeping for later: on this target, randomness must come from
elsewhere. No code in the project uses `random_device` today.

## Reproducing

```sh
# the three probes
printf '#include <filesystem>\nint main(){return 0;}\n' > p1.cpp
printf '#include <filesystem>\nstatic std::filesystem::path p;\nint main(){return (int)p.string().size();}\n' > p2.cpp
printf '#include <filesystem>\nint main(){return (int)std::filesystem::exists("a");}\n' > p3.cpp

i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 -o p1.exe p1.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive

python3 tools/win95/check_imports.py p1.exe p2.exe p3.exe
```

## What extending it to the game's sources taught — 13 August 2026

`librecomp`'s four operations did not suffice for the game's 59 sites. On adding
`is_regular_file`, `file_size`, `rename`, `absolute` and the enumeration, three
things came to light, two of which no code review would have given.

### `GetFileAttributesExA` is **absent**, not stubbed

It was the natural choice for `file_size`: it returns attributes and size without
opening the file. Windows 95 does not export it — like `GetFileSizeEx`. These are
not stubs: the loader refuses to start the whole process.

The import check stopped it before the machine did, which is exactly its role. The
lesson is general: **an `...A` API is not guaranteed by the mere fact of being
`...A`.** That pair was added by Windows 98, and nothing in its name says so.

The replacement is `FindFirstFileA`, which returns the size without opening the
file either — hence with no descriptor to leak and no sharing conflict, which was
the reason for the original choice.

### `DeleteFileA` on a directory does not say what one thinks

`dkr_file_remove` tried `DeleteFileA`, then fell back on `RemoveDirectoryA` if the
path was a directory. On Windows 95, `DeleteFileA`'s failure on a directory went
through the "already absent" branch and the function **returned success without
deleting anything**.

The defect showed itself not through a crash but through a false failure elsewhere:
the trial suite's preliminary cleanup did not operate, and the next run found the
previous one's directory tree. The transfer disk's timestamps pointed at it — they
had stayed at the previous day's time.

The fix asks for the type **before** acting. One more call, and no remaining way to
confuse "nothing to do" with "I did not know how".

### `create_directories` does not return "it is there"

It returns "I created at least one". On a directory already present,
`std::filesystem` returns **false**; the Windows 95 layer returned **true**,
because to it a directory already present is legitimately a success.

Both contracts are right separately, and that is what makes the discrepancy
pernicious: the trial suite validated it, since it only asked for `true`. It is the
same trap as `remove` on an absent file, described at the head of the suite — and it
had been set twice without being seen.

**A seam whose two branches differ on a return value is worse than no seam at
all**: the code works on the host and behaves otherwise on the target. The trials
now bear on the effect as much as on the returned value.

For the same reason, `file_size`, `rename` and `absolute` are wrapped on both
sides: their forms without `error_code` **throw** in the standard library, where the
target cannot. No call site loses by it — they all already use the `error_code`
form.

### In passing: the test machine was receiving wrong paths

`Drive-Win95-VM.sh type` went through `xdotool type`, without translation. The
guest being AZERTY, `D:\FSSEAM.EXE` arrived there as `DM"FSSEQ?:EXE` — and Windows
answered "file not found", which one readily blames on the binary.

`tools/win95/azerty_keys.py` already knew how to correct that, but was not wired
in. It is now, because no Windows path is written without a ":" or a "\".

### State

27 checks, all green on the emulated machine as on the host, the same source
compiled for both. What is established is not "the operations work" but "they
behave like the ones they replace".

## Wiring the game's sources — 13 August 2026

The 96 operation sites in the game's sources now go through `dkr::fs`.
Eighty-seven rewrote themselves mechanically; the other nine required a decision,
and that is where the work was.

### `copy_options::none` is not `overwrite_existing`

Six of the eight `copy_file` calls overwrote; **two refused to** — importing a CRT
filter or a texture pack must not silently replace the one that already bears that
name. The seam offered only the first form.

Both are now named, `copy_file_overwrite` and `copy_file_no_overwrite`, rather than
passing around an option set of which nobody uses more than two values. The refusal
is made by the system — `CopyFileA(…, TRUE)` on the target, `fopen` in "wbx" on the
vehicle — and not by a prior `exists`: between the test and the copy there is an
interval.

### An empty list does not say why

`list_directory` returned an empty list both for an empty directory and for an
unreadable one. One call site distinguishes the two in order to tell the player —
"T.T. could not read this location". Hence a form with an `error_code`.

### What was not reproduced, and why

`symlink_status` returns a `file_status`: a type, its accessors, its categories.
Its single call site expresses itself just as well in operations on the path, and
the code's order — refuse the link **before** testing the presence — is preserved,
because a broken link does not "exist".

`is_symlink` always returns false on the target. That is not a surrender: Windows
95 has neither symbolic links nor NTFS junctions — those only arrive with Windows
2000.

### What stays unverifiable here, and what was put in its place

Four files — `runtime_ui`, `runtime_texture_packs`, `runtime_crt_overlay`,
`runtime_rice_texture_import` — only compile with RT64, absent from this
repository. Windows 95 never builds them anyway: RT64 requires D3D12, Vulkan or
Metal.

Their calls therefore cannot be tried by compiling them. What carries the real
risk, on the other hand, is: `test_fileio_signatures.cpp` restates every call with
its argument types and compiles in both branches. That is how the missing overloads
were found — `absolute(p, ec)`, `is_regular_file(p, ec)`, `current_path(ec)` — whose
absence only shows at compile time.

**That check does not replace compiling those files**, and it will have to be done
when RT64 is present. It covers the only thing that could be covered without it.

### State

39 checks on the emulated machine, as many on both of the host's branches, plus the
signatures. All 17 of the game's sources that the Windows 95 target builds compile.
There remain in the game's sources only 11 includes of `<mutex>` and `<thread>`,
which belong to E02-S02.

## Save interchange, in both directions — 14 August 2026

The criterion asked that a save produced by the modern build be read by the Windows
95 one, and conversely. The result is stronger: **both builds produce the same
bytes.**

```text
2673ca1aa7ecf15e5751ae5b894f6d6ca57a6c831b15d9bb1da975db4dae47bc  ADVHOST.BIN
2673ca1aa7ecf15e5751ae5b894f6d6ca57a6c831b15d9bb1da975db4dae47bc  ADVWIN.BIN
```

The arrangement has two halves, and that is what makes it conclusive:

- **the producing half**, `save_interchange.cpp`, writes an adventure save and the
  same source is compiled for both targets;
- **the consuming half** already existed — `dkr_save_codec_tests` takes a file as an
  argument, decodes it, re-encodes it, and demands equality **byte for byte**. That
  is stronger than a "the decode succeeds": it also catches encoding differences,
  which are precisely what a change of platform risks introducing.

The content written is not a blank save. A blank save is mostly made of zeros, and
zeros survive just about any conversion error. The 16- and 32-bit fields therefore
carry **asymmetric** patterns — `0x1234` and not `0x1221` — because a byte swap on a
symmetric value does not show, and that is the main risk when the same structure is
encoded by two different compilers.

### And persistence across a reboot

Written in one session, the machine shut down cleanly, read back in the next: 512
bytes intact, a round trip with no discrepancy. It is the last of the first
criterion's three parts — write, read back, persist.
