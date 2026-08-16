#!/usr/bin/env python3
"""E09-S01 - prepares the Windows 95 installation in the test machine.

Extracts the installation source from the user's ISO onto a partitioned hard disk,
and prepares a FreeDOS boot floppy. Putting the source on a disk rather than on
the CD removes any dependence on a CD-ROM driver under DOS, which is the most
fragile point of an emulated Windows 95 installation.

No Microsoft file is downloaded: everything comes from the user's ISO.

    scripts/prepare_win95_install.py --iso /path/to/W95.iso
"""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

SECTOR = 512
PART_START_LBA = 63          # classic geometry: the partition starts after track 0
PART_TYPE_FAT16 = 0x06       # FAT16 > 32 MiB, CHS

# --- geometry: this script's trap --------------------------------------------
#
# A period BIOS can only address 1024 cylinders. Beyond that it applies a
# translation: it doubles the head count until the cylinder count falls back under
# the limit, and presents THAT geometry to INT 13h. The POST announces it - "LBA"
# for a translated disk, "CHS" for one that fits within the limits.
#
# Windows 95's boot code converts logical addresses into CHS using the head count
# written in the boot sector's BPB. If that number does not match the one the BIOS
# presents, every read lands beside its target: the machine loads anything at all
# and freezes **without a message**, after the POST.
#
# So we build the disks directly in the translated geometry, so that the BPB and
# the BIOS agree from the start.
MAX_BIOS_CYLINDERS = 1024


def translated_heads(total_sectors: int, spt: int, heads: int) -> int:
    """The number of heads the BIOS will present for this disk."""
    while total_sectors // (heads * spt) > MAX_BIOS_CYLINDERS:
        heads *= 2
    return heads

# The \WIN95 folder of OSR2.5 CDs also contains Internet Explorer, MSN, AOL and a
# quantity of extras. Only the installation files interest us: 46 MiB instead of
# 124, which fits on a 128 MiB disk.
#
# Careful: the installation DOES ASK for some of those extras during the copy -
# `aol30fr.exe` on the French CD, for instance. It does not stop for all that: the
# "skip file" button in its dialog allows one to carry on, and the component is
# simply not installed. That is of no consequence for a test bench, but it must be
# known before believing the source disk incomplete. `--complete` copies everything
# and avoids the question, at the price of a 256 MiB disk.
EXTRA_PREFIXES = (
    "IE4", "ICW", "MSN", "AOL", "NM2", "MAILNEWS", "SWDIR", "SWFLASH", "SWINST",
    "VDOLIVE", "MSCHAT", "MSAGENT", "MSVBVM", "JAVI", "IEJAVA", "IELPK", "WPIE4",
    "NSIE4", "R32MSIE", "CS3KIT", "VRML2C", "MSWALLET", "SASETUP", "FPESETUP",
    "AXA", "AMOV4IE", "DXMINI", "DXDDEX", "IR50", "MSGMS", "MSMUS", "SETUPNT",
    "CHL", "ACTSETUP", "BRANDING", "MOS",
)

FREEDOS_URL = ("https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/"
               "distributions/1.4/FD14-FloppyEdition.zip")
FREEDOS_MEMBER = "144m/x86BOOT.img"


def say(msg: str) -> None:
    print(f"\033[1;34m==>\033[0m {msg}")


def die(msg: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"\033[1;31merror:\033[0m {msg}", file=sys.stderr)
    raise SystemExit(1)


def chs(lba: int, heads: int, spt: int) -> bytes:
    """Encodes an LBA as CHS in three bytes, saturated at the maximum value."""
    cyl, rem = divmod(lba, heads * spt)
    head, sec = divmod(rem, spt)
    if cyl > 1023:
        cyl, head, sec = 1023, heads - 1, spt - 1
    return bytes([head, ((cyl >> 2) & 0xC0) | ((sec + 1) & 0x3F), cyl & 0xFF])


def write_mbr(path: pathlib.Path, cylinders: int, heads: int, spt: int,
              active: bool = False) -> int:
    """Writes a partition table containing a single FAT16 partition.

    No boot code in the MBR: it is the Windows installation that will write its own
    on the system disk. The "active" flag, on the other hand, must be set on that
    disk now, without which the MBR will have no partition to load and the machine
    will not boot once Windows is installed. Returns the partition's sector count.
    """
    total = cylinders * heads * spt
    part_sectors = total - PART_START_LBA
    entry = (
        bytes([0x80 if active else 0x00])
        + chs(PART_START_LBA, heads, spt)
        + bytes([PART_TYPE_FAT16])
        + chs(total - 1, heads, spt)
        + struct.pack("<II", PART_START_LBA, part_sectors)
    )
    assert len(entry) == 16
    with open(path, "r+b") as f:
        f.seek(0x1BE)
        f.write(entry + bytes(48))          # the other three entries are empty
        f.seek(0x1FE)
        f.write(b"\x55\xAA")
    return part_sectors


def make_disk(path: pathlib.Path, cylinders: int, heads: int, spt: int,
              label: str, mtools: pathlib.Path, force: bool,
              active: bool = False) -> None:
    total_sectors = cylinders * heads * spt
    size = total_sectors * SECTOR
    if path.exists() and not force:
        say(f"{path.name} already present ({size // 1024 // 1024} MiB) - kept")
        return

    # We adopt the geometry the BIOS will present, not the physical one: see the
    # note at the head of the file. The disk's size does not change.
    effective_heads = translated_heads(total_sectors, spt, heads)
    if effective_heads != heads:
        cylinders = total_sectors // (effective_heads * spt)
        total_sectors = cylinders * effective_heads * spt
        size = total_sectors * SECTOR
        heads = effective_heads
        say(f"{path.name} exceeds {MAX_BIOS_CYLINDERS} cylinders: "
            f"geometry translated to {cylinders}/{heads}/{spt}, like the BIOS")

    say(f"Creating {path.name} ({size // 1024 // 1024} MiB, CHS {cylinders}/{heads}/{spt})")
    with open(path, "wb") as f:
        f.truncate(size)
    part_sectors = write_mbr(path, cylinders, heads, spt, active)
    if part_sectors * SECTOR > 2 * 1024**3:
        die(f"{path.name} exceeds FAT16's 2 GiB limit; reduce its geometry")
    subprocess.run(
        [str(mtools / "mformat"), "-i", f"{path}@@{PART_START_LBA * SECTOR}",
         "-h", str(heads), "-s", str(spt), "-T", str(part_sectors),
         "-v", label, "::"],
        check=True, stdout=subprocess.DEVNULL,
    )


def extract_win95(iso: pathlib.Path, dest: pathlib.Path, complete: bool = False) -> int:
    try:
        import pycdlib  # type: ignore
    except ImportError:
        die("pycdlib is required: pip install pycdlib (or uv pip install pycdlib)")
    dest.mkdir(parents=True, exist_ok=True)
    cd = pycdlib.PyCdlib()
    cd.open(str(iso))
    try:
        children = list(cd.list_children(iso_path="/WIN95"))
    except Exception:
        die(f"{iso} contains no /WIN95 folder - is this really a Windows 95 CD?")
    count = 0
    for child in children:
        if child is None or child.is_dot() or child.is_dotdot() or child.is_dir():
            continue
        name = child.file_identifier().decode("ascii", "replace").split(";")[0]
        if not complete and name.upper().startswith(EXTRA_PREFIXES):
            continue
        with open(dest / name, "wb") as out:
            cd.get_file_from_iso_fp(out, iso_path=f"/WIN95/{name};1")
        count += 1
    cd.close()
    if not any(p.name.upper() == "INSTALL.EXE" for p in dest.iterdir()):
        die("INSTALL.EXE not found in /WIN95: this ISO is not a usable Windows 95 "
            "installation source")
    return count


def fetch_freedos(dest: pathlib.Path) -> None:
    if dest.exists():
        say("FreeDOS floppy already present")
        return
    say("Fetching the FreeDOS 1.4 boot floppy")
    with tempfile.NamedTemporaryFile(suffix=".zip") as tmp:
        with urllib.request.urlopen(FREEDOS_URL, timeout=180) as response:
            shutil.copyfileobj(response, tmp)
        tmp.flush()
        with zipfile.ZipFile(tmp.name) as archive:
            dest.write_bytes(archive.read(FREEDOS_MEMBER))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", required=True, type=pathlib.Path,
                    help="the user's Windows 95 installation ISO")
    ap.add_argument("--prefix", type=pathlib.Path,
                    default=pathlib.Path(os.environ.get(
                        "DKR_WIN95_PREFIX", pathlib.Path.home() / ".local/dkr-win95")))
    ap.add_argument("--vm", default=os.environ.get("DKR_WIN95_VM", "dkr-p2-voodoo2"))
    ap.add_argument("--force", action="store_true",
                    help="recreate the disk images even if they exist")
    ap.add_argument("--complete", action="store_true",
                    help="copy the whole of /WIN95 (124 MiB) instead of the "
                         "installation files alone; avoids the missing-file prompts "
                         "during the copy, at the price of a larger source disk")
    args = ap.parse_args()

    if not args.iso.is_file():
        die(f"ISO not found: {args.iso}")
    vm = args.prefix / "vm" / args.vm
    if not vm.is_dir():
        die(f"machine absent: {vm}\nRun scripts/Setup-Win95-TestVM.sh first")
    mtools = args.prefix / "bin"
    if not (mtools / "mformat").exists():
        die("mtools absent. Run scripts/Setup-Win95-TestVM.sh")

    # System disk: partitioned, formatted and marked active from the host, which
    # avoids a pass through FDISK and FORMAT in the guest - and the reboot FDISK
    # imposes between the two. The Windows installation writes its boot sector
    # there.
    make_disk(vm / "win95.img", 2080, 16, 63, "WIN95", mtools, args.force,
              active=True)
    # Host -> guest transfer disk.
    make_disk(vm / "transfer.img", 1024, 16, 63, "DKRXFER", mtools, args.force)
    # Installation source.
    source_cylinders = 522 if args.complete else 261      # 256 MiB or 128 MiB
    make_disk(vm / "install.img", source_cylinders, 16, 63, "W95SRC", mtools, args.force)

    say("Extracting the installation source from the ISO")
    with tempfile.TemporaryDirectory() as tmpdir:
        staging = pathlib.Path(tmpdir) / "WIN95"
        count = extract_win95(args.iso, staging, args.complete)
        total = sum(p.stat().st_size for p in staging.iterdir())
        say(f"{count} files, {total / 1024 / 1024:.1f} MiB")

        image = f"{vm / 'install.img'}@@{PART_START_LBA * SECTOR}"
        subprocess.run([str(mtools / "mmd"), "-i", image, "::/WIN95"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say("Copying to the installation disk")
        subprocess.run([str(mtools / "mcopy"), "-i", image, "-s", "-o",
                        *[str(p) for p in sorted(staging.iterdir())], "::/WIN95/"],
                       check=True, stdout=subprocess.DEVNULL)

    fetch_freedos(vm / "freedos-boot.img")

    say("Verifying the installation disk")
    subprocess.run([str(mtools / "mdir"), "-i",
                    f"{vm / 'install.img'}@@{PART_START_LBA * SECTOR}", "::/WIN95"],
                   check=True)

    print(f"""
\033[1;34m==>\033[0m Ready to install

  Disk C:    {vm / 'win95.img'}     system, empty, ready
  Disk D:    {vm / 'transfer.img'}  host <-> guest transfer
  Disk E:    {vm / 'install.img'}   Windows 95 source
  Floppy     {vm / 'freedos-boot.img'}  FreeDOS boot

C: is already partitioned, formatted and active: neither FDISK nor FORMAT is
needed. Start the machine, then at the FreeDOS prompt:

  E:\\WIN95\\INSTALL.EXE

Once Windows is installed, install the 3dfx drivers, then freeze the state:

  scripts/Run-Win95-VM.sh --snapshot
""")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
