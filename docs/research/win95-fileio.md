# What Windows 95 allows for writing a save file

Measurements from
[E02-S05](../stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md), taken
on the test machine. Probe: `tools/win95/witnesses/fileio_probe.cpp`, the report
written on the transfer disk's **FAT16** volume — that is, the file system we care
about.

## The report

```text
1. Replacing an existing file
   MoveFileExA(REPLACE_EXISTING) : FAILS, error 120
   MoveFileA onto an existing target : refuses (expected)

2. Long file names on the FAT16 volume
   creating a long name          : succeeds
   reading back by the same name : succeeds
   equivalent short name         : D:\RACESA~1.DKR
   reading back by the short name: succeeds

3. Location of the executable
   GetModuleFileNameA            : D:\FILEIO.EXE
   current directory             : D:\

4. Refused write
   write to A: (empty)           : refused, error 21

5. Free space on D:
   512 bytes per sector, 16 sectors per cluster
```

## `MoveFileExA`: a third way of being absent

The ticket announced that "`MoveFileEx` with replacement is not available there".
**It was right** — unlike two of its neighbours in E02-S03, which measurement
disproved. But the form of the unavailability is worth looking at, because it
escapes both of the repository's guard rails.

`MoveFileExA` is **exported** by KERNEL32, so the import check lets it through. It
is **not** an empty entry: its code begins with the same prologue as `MoveFileA`,
`sub edx,edx` then the installation of the SEH chain, so the stub survey does not
see it either. It is a real function, which decides to refuse and sets
`ERROR_CALL_NOT_IMPLEMENTED`.

| Category | Example | What reveals it |
|---|---|---|
| Absent from the export table | `TryEnterCriticalSection` | the import check |
| Exported, empty entry | `CreateSemaphoreW` | the stub survey |
| Exported, real code, refuses | **`MoveFileExA`** | **nothing but execution** |

The third follows from no static analysis. That is why this repository runs probes
on the machine rather than reasoning over tables.

## Consequence: there is no atomic replacement

The sequence must therefore be written by hand, and its window accepted:

```
1. write   SAVE.TMP, flush the buffers, close
2. delete  SAVE.BAK
3. rename  SAVE.DAT -> SAVE.BAK
4. rename  SAVE.TMP -> SAVE.DAT
```

Between 3 and 4, the final file does not exist. **The guarantee offered is
therefore not "the last write is never lost", but "a valid save is never lost".**
That is the distinction that counts: losing the last race is annoying, losing the
entire progress is unforgivable.

On read-back, the order of preference is `SAVE.DAT`, then `SAVE.BAK`, and **never**
`SAVE.TMP`: nothing proves it is complete, and DKR-R's format carries no checksum
that would allow it to be verified without modifying it. Loading a truncated save
would be discovered much later and much worse.

`MoveFileA` refuses to overwrite, as documented — hence the prior deletion at step
2.

## Long names work, but 8.3 stays the reference

VFAT is active: the probe's 28-character name is created, and read back by its long
name as by its `RACESA~1.DKR` alias. The alias truncates the extension from `.dkrsave` to
`.DKR`, which would be enough to make two similar names diverge.

The layer corrects nothing and assumes nothing: `dkr_file_name_is_8dot3`
**answers**, and the names it builds itself — `.DAT`, `.TMP`, `.BAK` on a base of
at most eight characters — all fit. A first-generation Windows 95 without VFAT, or
a volume mounted otherwise, therefore stays usable.

## Location

There is no `%APPDATA%` under Windows 95. The save goes next to the executable,
found through `GetModuleFileNameA` — **never** through the current directory:
launched from the Start menu, a program inherits a current directory unrelated to
where it is installed. That the two coincide in the report above is a property of
the test protocol, not of the system.

## Errors

Writing to an empty drive is refused with error 21, `ERROR_NOT_READY` — usable,
and distinct from `ERROR_ACCESS_DENIED` and from `ERROR_DISK_FULL`. The layer
distinguishes them, because "disk full" and "medium write-protected" do not call
for the same action from the player.

## Reproducing

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o FILEIO.EXE tools/win95/witnesses/fileio_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive
scripts/Push-To-Win95-VM.sh FILEIO.EXE
# in the guest: d:\fileio.exe - the report lands in D:\FILEIO.TXT
```

## The full disk, provoked for real — 14 August 2026

The initial report checked that the error codes are **distinct** and carry a text.
That is necessary and it is not sufficient: nothing proved that a genuinely full
medium returns `DKR_FILE_ERR_NO_SPACE` rather than the catch-all
`DKR_FILE_ERR_IO`.

```text
target             : A:\FULL.DAT
size requested     : 2097152 bytes
code returned      : 4              (DKR_FILE_ERR_NO_SPACE)
text               : disk full
```

### Reproducing

The transfer disk has half a gigabyte free, which makes the exercise impractical by
that route. A 1.44 MB floppy fills in a second:

```sh
P=~/.local/dkr-win95; VM=$P/vm/dkr-p2-voodoo2
dd if=/dev/zero of=$VM/full.img bs=1024 count=1440
$P/bin/mformat -i $VM/full.img -f 1440 ::
head -c 1350000 /dev/urandom > padding.dat
$P/bin/mcopy -i $VM/full.img padding.dat ::/PADDING.DAT   # 107 KB left

# then, in 86box.cfg, under [Floppy and CD-ROM drives]:
#   fdd_01_fn = full.img
```

The probe aims at **2 MB**, that is more than the whole volume and not merely more
than the space remaining: a write that only just fitted would prove nothing
reproducible.

The machine's configuration is to be restored afterwards — mounting a full floppy
would change the reference for every other trial.

## The power cut, provoked for real — 14 August 2026

Until now the durable sequence's promise was reasoned about and tried by
simulation. On an emulated machine the real cut is within reach: `kill -9` on the
emulator carries off the guest's disk cache exactly as a pulled plug would.

Protocol: writing numbered and checksummed saves in a loop, cut after 23 seconds —
more than 350 rounds — then reboot and read back.

### What the cut left behind

Windows detected the unclean stop and ran ScanDisk, which found **a single damaged
file** (its message is in French on this machine; translated here):

```text
The file D:\PWRCUT.TMP is damaged. Although the beginning of the file is
probably correct, the file is damaged further on.
```

That is exactly the file the sequence sacrifices. `PWRCUT.DAT` and `PWRCUT.BAK`
were intact.

### The verdict after reboot

```text
read code          : 0 (success)
bytes read back    : 512
round number       : 370
verdict            : valid save, main file
```

Round 370's save survived, checksum included, and it did not even take falling back
on the backup copy.

### What this report proves, and what it does not

It proves that the sequence holds under a real cut, that the damage falls on the
temporary file, and that Windows repairs the volume at the next boot.

**It does not prove that the window is never hit.** It is a draw: the cut fell
during the writing of the `.TMP`, which occupies most of each round's time. The
window — between the rename of `.DAT` to `.BAK` and that of `.TMP` to `.DAT` —
stays narrow by construction, and that is all the platform allows:
`MoveFileExA(REPLACE_EXISTING)` is not implemented there. A draw that fell inside it
would leave the previous save in `.BAK`, and that is precisely the announced
guarantee: **a valid save is never lost**, not "the last write is never lost".

### A remark on the protocol's severity

`kill -9` also carries off what the emulator held in the host's cache, which real
hardware would already have written. The protocol is therefore **at least as
harsh** as a genuine cut, never gentler — the right direction for the error to lie
in. It showed as much: the volume's FAT was unreadable from the host before
ScanDisk repaired it.
