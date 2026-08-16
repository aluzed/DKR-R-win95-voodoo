# The game starts under Windows 95

A report from 13 August 2026, on the test machine — Pentium II 400 MHz, Windows 95
OSR2.

```text
[boot][input] keyboard: WASD=stick arrows=d-pad Space=A Shift=B Z=Z Enter=Start
                        IJKL=C Q=L E=R
The diagnostic runtime requires a ROM path.
```

The program loads, initialises its input layer, displays its keyboard
configuration and stops on the expected message: no ROM was supplied to it. That
is not a crash — it is the nominal path up to the point where a piece of data is
missing.

## What the link cost

55 translation units — 37 recompiled files, the RSP microcode, the game's 17
sources — plus the live recompiler and its dependencies.

| | Symbols required |
|---|---:|
| first attempt | 30 |
| o1heap, absent from the build | −3 |
| miniz, likewise — **and four files, not one** | −6 |
| DbgHelp, which Windows 95 does not have | −8 |
| live recompiler, N64Recomp and rabbitizer | −13 |

Then the import check found five more, invisible to the linker because it was
resolving them from mingw's import libraries: `_wfopen_s`, `_wfreopen_s`,
`_strtoi64`, `_strtoui64`, `GetModuleHandleExW`.

**No unresolved symbol came from the platform layer, from `ultramodern`, from the
game's sources or from the recompiled code.** That was the question this link
asked, and the answer is the best possible one.

## Three traps, all of the same family

### `strtoll` hides `_strtoi64`

`_strtoi64` was first written by calling `strtoll`. Under mingw, `strtoll` **is** a
forwarder to `_strtoi64` imported from MSVCRT: the definition was circular. It
compiled, it linked, and the missing symbol reappeared in the import table with
nothing to flag it.

The parsing is therefore written by hand, and `strtoll`/`strtoull` are supplied on
top — without which `mod_manifest.cpp`, which calls them, brought the import back
in through the service entrance.

### `std::thread` without `#include <thread>`

The subset checker read the includes. `librecomp` includes `<thread>` nowhere
directly — it arrives transitively — and yet `recomp.cpp` constructed **the game's
thread** with `std::thread`. Nothing protested.

The binary loaded and died at startup:

```text
terminate called after throwing an instance of 'std::system_error'
  what():  Resource temporarily unavailable
```

It is `pthread_create` that fails behind the standard library — the same cause as
the `GetHandleInformation` stub documented since E02-S01.

**A check that reads the includes cannot see a use.** That is exactly the lesson
`<filesystem>` had already given, where the watch bears on the operations and not
on the header. The checker now watches the types themselves.

### The thread bridge did not accept pointers to members

Its variadic constructor called directly, with this comment: "no call from
`ultramodern` is a pointer to a member". That was true of `ultramodern`.
`librecomp` starts a thread on
`&ModContext::dirty_mod_configuration_thread_process`, and the direct form does not
compile for it.

`std::invoke` is what `std::thread` uses, and it is the contract that had to be
reproduced. The original assumption saved one header — `<functional>` is pure
library and adds no import — at the price of a divergence of contract. The wrong
side of the bargain.

## What remains stubbed, and what that costs

Six symbols are exported and empty. None prevents loading; each is justified in
`tools/win95/exports/exceptions.json`.

| Symbol | Origin | Consequence |
|---|---|---|
| `GetProcessTimes`, `GetThreadTimes`, `GetSystemTimeAdjustment` | libwinpthread's `clock.o`, pulled in by `<chrono>` | none — only the process-clock identifiers lead there, and nobody asks for them |
| `MoveFileExW` | libstdc++'s `fs_ops.o` | none — no code calls `std::filesystem::rename` here any more |
| `LoadLibraryExW` | `mods.cpp` | native mods do not load — out of scope |
| `WriteConsoleW` | `fmt`, through `N64Recomp` | the live recompiler's diagnostics do not appear |

`CreateProcessW` was among them: the quick restart would have failed silently. It
has moved to `CreateProcessA`, which does the same thing everywhere.

## What this does not prove

The game **starts**. It has not yet run with a ROM, and nothing that follows the
loading is tried: neither the game loop, nor the Voodoo rendering, nor the sound,
nor the pacing. The live recompiler is linked and has never been run on this
machine — it allocates a page and writes code into it, which remains to be seen
under Windows 95.

## The build is reproducible

The first boot came from a chain of commands in a working directory. That was not
a deliverable: the binary existed, the project did not know how to remake it.

Three targets were added to `cmake/win95-target.cmake`:

| Target | Contents |
|---|---|
| `win95liverecomp` | the core of `N64Recomp`, `sljit`, `rabbitizer`, the live generator |
| `win95recompiled` | the 37 recompiled files and the RSP microcode |
| `DKRWin95Game` | the game's 17 sources, linked as `DKRR.EXE` |

A complete build from scratch: **34 seconds**, both guard rails included, and the
binary behaves on the machine exactly like the one linked by hand.

### Two traps in the wiring

**The compatibility archive was added twice.** `win95compat` adds itself, first and
under `--whole-archive`, through its interface options. Naming it a second time
gave "multiple definitions".

**`file(GLOB)` at a fixed depth left sources behind.** `rabbitizer`'s are spread
over two levels; the `src/*/*.c` pattern missed thirteen of them, and the linker
asked for `RabbitizerInstruction_getRaw` and thirty or so others. `GLOB_RECURSE`.

### And a trap that did not come from the wiring

CMake leaves `CMAKE_BUILD_TYPE` empty by default. On modern targets that is an
annoyance; here it is a silent trap:

| | Size of `DKRR.EXE` |
|---|---:|
| with no build type | **20.6 MB** |
| `Release` | 8.5 MB |

And the size is not the worst of it. The heart of this port is MIPS recompiled
into C, whose per-instruction cost decides everything on a 400 MHz Pentium II.
Unoptimised it would not be "slower": it would be unplayable, with nothing to
announce it. The target therefore imposes `Release` when the caller has chosen
nothing, and says so at configuration time.

## The wiring found a defect the manual build was hiding

The two save suites were also built by hand. Once put through CMake, they printed
their four phases on the machine then **crashed** — a general protection fault.

The cause is in `Release` mode, which defines `NDEBUG`. Those suites are made of
assertions, and they put their calls **inside** the assertions:

```cpp
assert(dkr::runtime::saves::backup_adventure(backup, error));
```

Under `NDEBUG`, the call disappears with the assertion. The save is never written,
the state is never built, and the final cleanup works on nothing.

**A trial suite that passes by testing nothing is the worst of results.** Here it
did not even pass, and that is what made it visible: the crash is a gift. Both
targets are therefore compiled with `-UNDEBUG`.

Worth noting for what follows: the danger stays in the source. Those 35 assertions
with side effects are harmless as long as nobody builds those suites with `NDEBUG`
— which is exactly what an ordinary `Release` does, on any platform.

## What the target now builds

Eleven executables, all subjected to both guard rails after the link:

```text
CLOCKT  DKRR  FILEIOT  FSSEAM  PLATFORM  SAVECDC
SAVEMGR  THRCPP  THREADS  WITNESS  WPROBE
```

and five suites registered in CTest, which pass in 20 seconds.
