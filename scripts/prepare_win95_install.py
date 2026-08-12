#!/usr/bin/env python3
"""E09-S01 — Prépare l'installation de Windows 95 dans la machine de test.

Extrait la source d'installation de l'ISO fournie par l'utilisateur vers un
disque dur partitionné, et prépare une disquette de démarrage FreeDOS. Poser la
source sur un disque plutôt que sur le CD supprime toute dépendance à un pilote
CD-ROM sous DOS, qui est le point le plus fragile d'une installation Windows 95
en émulation.

Aucun fichier Microsoft n'est téléchargé : tout vient de l'ISO de l'utilisateur.

    scripts/prepare_win95_install.py --iso /chemin/vers/W95.iso
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
PART_START_LBA = 63          # géométrie classique : la partition commence après la piste 0
PART_TYPE_FAT16 = 0x06       # FAT16 > 32 Mio, CHS

# --- géométrie : le piège de ce script ---------------------------------------
#
# Un BIOS d'époque ne sait adresser que 1024 cylindres. Au-delà, il applique une
# translation : il double le nombre de têtes jusqu'à ce que le compte de
# cylindres repasse sous la limite, et présente CETTE géométrie à INT 13h. Le
# POST l'annonce — « LBA » pour un disque translaté, « CHS » pour un disque qui
# tient dans les limites.
#
# Le code d'amorçage de Windows 95 convertit les adresses logiques en CHS avec le
# nombre de têtes inscrit dans le BPB du secteur de démarrage. Si ce nombre ne
# correspond pas à celui que le BIOS présente, chaque lecture tombe à côté : la
# machine charge n'importe quoi et se fige **sans message**, après le POST.
#
# On construit donc les disques directement dans la géométrie translatée, de
# sorte que le BPB et le BIOS soient d'accord dès le départ.
MAX_BIOS_CYLINDERS = 1024


def translated_heads(total_sectors: int, spt: int, heads: int) -> int:
    """Nombre de têtes que le BIOS présentera pour ce disque."""
    while total_sectors // (heads * spt) > MAX_BIOS_CYLINDERS:
        heads *= 2
    return heads

# Le dossier \WIN95 des CD OSR2.5 contient aussi Internet Explorer, MSN, AOL et
# quantité d'extras. Seuls les fichiers d'installation nous intéressent : 46 Mio
# au lieu de 124, ce qui tient dans un disque de 128 Mio.
#
# Attention : l'installation RÉCLAME certains de ces extras pendant la copie —
# `aol30fr.exe` sur le CD français, par exemple. Elle ne s'arrête pas pour
# autant : le bouton « Ignorer le fichier » de sa boîte de dialogue permet de
# poursuivre, et le composant simplement n'est pas installé. C'est sans
# conséquence pour un banc de test, mais il faut le savoir avant de croire à un
# disque source incomplet. `--complete` copie tout et évite la question, au prix
# d'un disque de 256 Mio.
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
    print(f"\033[1;31merreur:\033[0m {msg}", file=sys.stderr)
    raise SystemExit(1)


def chs(lba: int, heads: int, spt: int) -> bytes:
    """Encode un LBA en CHS sur trois octets, saturé à la valeur maximale."""
    cyl, rem = divmod(lba, heads * spt)
    head, sec = divmod(rem, spt)
    if cyl > 1023:
        cyl, head, sec = 1023, heads - 1, spt - 1
    return bytes([head, ((cyl >> 2) & 0xC0) | ((sec + 1) & 0x3F), cyl & 0xFF])


def write_mbr(path: pathlib.Path, cylinders: int, heads: int, spt: int,
              active: bool = False) -> int:
    """Écrit une table de partition contenant une unique partition FAT16.

    Pas de code d'amorçage dans le MBR : c'est l'installation de Windows qui
    écrira le sien sur le disque système. En revanche le fanion « active » doit
    être posé dès maintenant sur ce disque, sans quoi le MBR n'aura aucune
    partition à charger et la machine ne démarrera pas une fois Windows installé.
    Retourne le nombre de secteurs de la partition.
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
        f.write(entry + bytes(48))          # les trois autres entrées sont vides
        f.seek(0x1FE)
        f.write(b"\x55\xAA")
    return part_sectors


def make_disk(path: pathlib.Path, cylinders: int, heads: int, spt: int,
              label: str, mtools: pathlib.Path, force: bool,
              active: bool = False) -> None:
    total_sectors = cylinders * heads * spt
    size = total_sectors * SECTOR
    if path.exists() and not force:
        say(f"{path.name} déjà présent ({size // 1024 // 1024} Mio) — conservé")
        return

    # On adopte la géométrie que le BIOS présentera, pas la géométrie physique :
    # voir la note en tête de fichier. La taille du disque ne change pas.
    effective_heads = translated_heads(total_sectors, spt, heads)
    if effective_heads != heads:
        cylinders = total_sectors // (effective_heads * spt)
        total_sectors = cylinders * effective_heads * spt
        size = total_sectors * SECTOR
        heads = effective_heads
        say(f"{path.name} dépasse {MAX_BIOS_CYLINDERS} cylindres : "
            f"géométrie translatée en {cylinders}/{heads}/{spt}, comme le BIOS")

    say(f"Création de {path.name} ({size // 1024 // 1024} Mio, CHS {cylinders}/{heads}/{spt})")
    with open(path, "wb") as f:
        f.truncate(size)
    part_sectors = write_mbr(path, cylinders, heads, spt, active)
    if part_sectors * SECTOR > 2 * 1024**3:
        die(f"{path.name} dépasse la limite FAT16 de 2 Gio ; réduisez sa géométrie")
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
        die("pycdlib est requis : pip install pycdlib (ou uv pip install pycdlib)")
    dest.mkdir(parents=True, exist_ok=True)
    cd = pycdlib.PyCdlib()
    cd.open(str(iso))
    try:
        children = list(cd.list_children(iso_path="/WIN95"))
    except Exception:
        die(f"{iso} ne contient pas de dossier /WIN95 — est-ce bien un CD Windows 95 ?")
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
        die("INSTALL.EXE introuvable dans /WIN95 : cette ISO n'est pas une source "
            "d'installation Windows 95 utilisable")
    return count


def fetch_freedos(dest: pathlib.Path) -> None:
    if dest.exists():
        say("Disquette FreeDOS déjà présente")
        return
    say("Récupération de la disquette de démarrage FreeDOS 1.4")
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
                    help="ISO d'installation de Windows 95 fournie par l'utilisateur")
    ap.add_argument("--prefix", type=pathlib.Path,
                    default=pathlib.Path(os.environ.get(
                        "DKR_WIN95_PREFIX", pathlib.Path.home() / ".local/dkr-win95")))
    ap.add_argument("--vm", default=os.environ.get("DKR_WIN95_VM", "dkr-p2-voodoo2"))
    ap.add_argument("--force", action="store_true",
                    help="recrée les images disque même si elles existent")
    ap.add_argument("--complete", action="store_true",
                    help="copie tout /WIN95 (124 Mio) au lieu des seuls fichiers "
                         "d'installation ; évite les demandes de fichier manquant "
                         "pendant la copie, au prix d'un disque source plus gros")
    args = ap.parse_args()

    if not args.iso.is_file():
        die(f"ISO introuvable : {args.iso}")
    vm = args.prefix / "vm" / args.vm
    if not vm.is_dir():
        die(f"machine absente : {vm}\nLancez d'abord scripts/Setup-Win95-TestVM.sh")
    mtools = args.prefix / "bin"
    if not (mtools / "mformat").exists():
        die("mtools absent. Lancez scripts/Setup-Win95-TestVM.sh")

    # Disque système : partitionné, formaté et marqué actif depuis l'hôte, ce qui
    # évite un passage par FDISK et FORMAT dans l'invité — et le redémarrage que
    # FDISK impose entre les deux. L'installation de Windows y écrit son secteur
    # d'amorçage.
    make_disk(vm / "win95.img", 2080, 16, 63, "WIN95", mtools, args.force,
              active=True)
    # Disque de transfert hôte -> invité.
    make_disk(vm / "transfer.img", 1024, 16, 63, "DKRXFER", mtools, args.force)
    # Source d'installation.
    source_cylinders = 522 if args.complete else 261      # 256 Mio ou 128 Mio
    make_disk(vm / "install.img", source_cylinders, 16, 63, "W95SRC", mtools, args.force)

    say("Extraction de la source d'installation depuis l'ISO")
    with tempfile.TemporaryDirectory() as tmpdir:
        staging = pathlib.Path(tmpdir) / "WIN95"
        count = extract_win95(args.iso, staging, args.complete)
        total = sum(p.stat().st_size for p in staging.iterdir())
        say(f"{count} fichiers, {total / 1024 / 1024:.1f} Mio")

        image = f"{vm / 'install.img'}@@{PART_START_LBA * SECTOR}"
        subprocess.run([str(mtools / "mmd"), "-i", image, "::/WIN95"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say("Copie vers le disque d'installation")
        subprocess.run([str(mtools / "mcopy"), "-i", image, "-s", "-o",
                        *[str(p) for p in sorted(staging.iterdir())], "::/WIN95/"],
                       check=True, stdout=subprocess.DEVNULL)

    fetch_freedos(vm / "freedos-boot.img")

    say("Vérification du disque d'installation")
    subprocess.run([str(mtools / "mdir"), "-i",
                    f"{vm / 'install.img'}@@{PART_START_LBA * SECTOR}", "::/WIN95"],
                   check=True)

    print(f"""
\033[1;34m==>\033[0m Prêt à installer

  Disque C:  {vm / 'win95.img'}     système, vide, prêt
  Disque D:  {vm / 'transfer.img'}  transfert hôte <-> invité
  Disque E:  {vm / 'install.img'}   source Windows 95
  Disquette  {vm / 'freedos-boot.img'}  démarrage FreeDOS

C: est déjà partitionné, formaté et actif : ni FDISK ni FORMAT ne sont
nécessaires. Lancez la machine, puis dans l'invite FreeDOS :

  E:\\WIN95\\INSTALL.EXE

Une fois Windows installé, installez les pilotes 3dfx, puis figez l'état :

  scripts/Run-Win95-VM.sh --snapshot
""")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
