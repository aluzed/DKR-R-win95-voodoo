# Provenance of the export baseline

These lists are not copied from documentation. Each one is the **real export
table** of a DLL extracted from the test machine, read by
`tools/win95/pe_symbols.py --exports`.

The distinction is not rhetorical: several APIs that period documentation gives
as present under Windows 95 are in fact absent (`TryEnterCriticalSection`,
`InterlockedCompareExchange`), and several that are properly exported do
nothing — the whole Unicode `...W` family of KERNEL32 is a stub that returns 0
and sets `ERROR_CALL_NOT_IMPLEMENTED`. See
[`docs/research/win95-blockers.md`](../../../docs/research/win95-blockers.md).

## Source system

| | |
|---|---|
| System | Windows 95 OSR2, French |
| Installation | `~/.local/dkr-win95/vm/dkr-p2-voodoo2/win95.img`, partition C: |
| Path | `C:\WINDOWS\SYSTEM` |
| Machine | 86Box, Pentium II 400 MHz, 64 MB, 3dfx |
| Extracted on | 2026-08-12 |

## Files

| DLL | Exports | Size | File date |
|---|---:|---:|---|
| `KERNEL32` | 682 | 422,400 B | 1996-08-24 |
| `MSVCRT` | 756 | 280,576 B | **1997-11-03** |
| `USER32` | 580 | 44,544 B | 1996-08-24 |
| `GDI32` | 330 | 131,072 B | 1996-08-24 |
| `ADVAPI32` | 224 | 43,008 B | 1996-08-24 |
| `WINMM` | 182 | 49,152 B | 1996-08-24 |
| `OLE32` | 162 | 558,704 B | 1996-08-24 |
| `SHELL32` | 86 | 831,488 B | 1996-08-24 |
| `WSOCK32` | 75 | 67,072 B | 1996-08-24 |
| `COMCTL32` | 64 | 379,152 B | 1996-08-24 |
| `DDRAW` | 21 | 159,744 B | 1996-08-24 |
| `COMDLG32` | 20 | 93,696 B | 1996-08-24 |
| `VERSION` | 14 | 6,656 B | 1996-08-24 |
| `DSOUND` | 5 | 86,016 B | 1996-08-24 |

That is **3,201 symbols**.

## Two reservations worth knowing

**`MSVCRT.DLL` is not original.** Its date — November 1997 — sets it apart from
the rest, dated 24 August 1996. It arrived with an update, not with the system.
**A first-generation Windows 95 does not have it at all.** That is why ADR 0001
mandates static linking of the CRT: depending on it would amount to making the
game depend on a version of Internet Explorer.

**DirectInput is absent.** This installation carries DirectX 2 — `DDRAW`,
`DSOUND`, `D3DIM`, `D3DRM` are there, but there is **no `DINPUT.DLL` at all**:
DirectInput only arrives with DirectX 3, and only becomes usable for gamepads
with DirectX 5. The gamepad reading of
[E06-S02](../../../docs/stories/E06-platform/E06-S02-keyboard-and-gamepad-input.md)
must therefore go through the multimedia API (`joyGetPosEx` from `WINMM`,
present here), or the package must ship a DirectX update.

`DDRAW` and `DSOUND` are added to the baseline because they exist, not because
the project targets them: rendering goes through Glide (ADR 0002) and audio
output through `waveOut` from `WINMM` (E06-S03).

## Regenerating

```sh
tools/win95/check_imports.py --refresh
```

Re-reads the DLLs from the test machine's disk image. To be redone if the
reference system changes — and then this file must be updated with it.
