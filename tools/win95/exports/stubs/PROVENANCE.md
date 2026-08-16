# Provenance of the empty-export survey

These lists record the symbols Windows 95 **exports without implementing**. They
complement `../` — which answers "does this symbol exist?" — by answering "and
does it do anything?".

Deliverable of [E02-S01](../../../../docs/stories/E02-system/E02-S01-threading-and-synchronisation-layer.md).

## Why this list exists

An **absent** symbol is a loud problem: Windows 95 refuses to load the program
and names the DLL and the symbol. That is what `check_imports.py` checks, and it
is E01-S04's guard rail.

An **exported and empty** symbol is silent, and therefore worse. The link
succeeds, the load succeeds, the import check is satisfied — and the function
does nothing, while setting an `ERROR_CALL_NOT_IMPLEMENTED` nobody reads.

That is how `CreateSemaphoreW` nearly carried off the whole of `ultramodern`'s
scheduler: `moodycamel::LightweightSemaphore` calls it, receives a null handle,
and neither its wait nor its signal works afterwards — the wait stops blocking,
the signal loops forever and freezes the machine. Details in
[`docs/research/win95-blockers.md`](../../../../docs/research/win95-blockers.md).

## How a stub is recognised

By its shape, in the disassembly — not by its name, and not from documentation:

```asm
33 c0              xor  eax,eax     ; return value = 0 (failure)
b1 XX              mov  cl,index    ; stub number
e9 XX XX XX XX     jmp  tail        ; common tail -> SetLastError(120)
```

There is no plausible false positive: no real function begins by setting its
return value to zero in order to jump straight elsewhere.

Further proof when one wants it: several stubs **share the same address**.
`LoadLibraryExW` and `MoveFileExW` are at the same one, `CreateEventW` and
`CreateSemaphoreW` too. Two functions with radically different behaviour only
share code when neither has any.

## The survey

Same DLLs, same machine and same date as `../PROVENANCE.md` — French Windows 95
OSR2, extracted on 2026-08-12.

| DLL | Stubs | Named exports | Share |
|---|---:|---:|---:|
| `ADVAPI32` | 176 | 224 | **79 %** |
| `KERNEL32` | 179 | 682 | 26 % |
| `USER32` | 162 | 580 | 28 % |
| `GDI32` | 62 | 330 | 19 % |
| `MSVCRT` | 0 | 756 | 0 % |
| `WINMM` | 0 | 182 | 0 % |

Two lessons beyond the case that prompted the survey:

- **`ADVAPI32` is 79 % decorative.** Any ticket aiming at it — registry,
  security, services — must check every entry before relying on it.
- **`MSVCRT` and `WINMM` have no stub at all.** The audio output of
  [E06-S03](../../../../docs/stories/E06-platform/E06-S03-audio-output.md)
  through `waveOut` will not meet this trap.

The pattern is not peculiar to the `...W` variants: `BackupRead`,
`CreateNamedPipeA`, `CreateIoCompletionPort`, `GetBinaryTypeA` and `FoldStringA`
are stubs too, without being Unicode APIs. That is why the survey is **measured
and not deduced from the name's suffix**.

## What this survey cannot see

There is a **third** category, which neither the export table nor this survey
catches, and it must be known before trusting either.

`MoveFileExA` is exported, has **real code** — the same prologue as `MoveFileA`,
with its SEH chain — and therefore does not appear above. It nonetheless fails at
run time with `ERROR_CALL_NOT_IMPLEMENTED`: it is a function that decides to
refuse, not an empty entry. Measured by
`tools/win95/witnesses/fileio_probe.cpp` on the machine (E02-S05).

A summary of the three categories, and of what reveals each:

| Category | Example | What reveals it |
|---|---|---|
| Absent from the export table | `TryEnterCriticalSection` | the import check — loud, the program does not start |
| Exported, empty entry | `CreateSemaphoreW` | this survey, by the pattern in the disassembly |
| Exported, real code, refuses | `MoveFileExA` | **nothing but execution** |

The third follows from no static analysis at all. That is why this repository
runs probes on the machine rather than reasoning over tables, and why a ticket
that assumes an API is available must check before building on it.

## Regenerating

```sh
tools/win95/find_stubs.py --write /path/to/KERNEL32.DLL USER32.DLL ...
```

The DLLs are those extracted from the test machine by
`tools/win95/check_imports.py --refresh`. To be redone if the reference system
changes — and then this file must be updated with it.
