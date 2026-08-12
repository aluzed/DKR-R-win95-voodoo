#!/usr/bin/env bash
# E09-S01 — Monte l'environnement de test emule : 86Box, jeu de BIOS, machine
# Pentium II / Voodoo 2 conforme a la cible, et disque de transfert hote<->invite.
#
# N'installe rien a l'echelle du systeme : tout va dans un prefixe utilisateur.
# Le media d'installation de Windows 95 n'est PAS telecharge : c'est un logiciel
# proprietaire de Microsoft, que l'utilisateur doit fournir lui-meme.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
VM="$PREFIX/vm/$VM_NAME"
BOX_VERSION="v6.0"
BOX_BUILD="b9001"

# Cible arretee par l'ADR 0002 (E00-S05). Modifiable pour eprouver les replis.
#
# ATTENTION : les trois reglages Voodoo ci-dessous sont ecrits dans 86box.cfg,
# mais 86Box ne les a PAS appliques lors du premier montage de cette machine —
# il a conserve le texte tel quel tout en emulant une Voodoo 1 avec 2 Mo + 2 Mo.
# Constate a l'ecran : le dialogue de reglages affichait « Graphique 3dfx
# Voodoo » alors que le fichier disait type = 1, et Glide repondait « expected
# Voodoo, none detected ».
#
# Il faut donc VERIFIER le modele une fois, par le dialogue :
#
#   86Box -S   ->   Affichage -> Graphique Voodoo 1 ou 2 -> Configurer
#
# et y choisir « 3Dfx Voodoo 2 », 4 Mo de tampon d'images, 4 Mo de textures.
# Apres ce passage, les memes valeurs dans le fichier sont honorees.
CPU_FAMILY="${DKR_WIN95_CPU_FAMILY:-pentium2_deschutes}"
CPU_SPEED="${DKR_WIN95_CPU_SPEED:-400000000}"
MEM_KB="${DKR_WIN95_MEM_KB:-65536}"          # 64 Mo
VOODOO_TYPE="${DKR_WIN95_VOODOO_TYPE:-1}"    # 0 = Voodoo Graphics, 1 = Voodoo 2
VOODOO_FB_MB="${DKR_WIN95_VOODOO_FB:-4}"
VOODOO_TEX_MB="${DKR_WIN95_VOODOO_TEX:-4}"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "$1 est requis mais absent"; }
need curl
need tar
need python3

mkdir -p "$PREFIX/bin" "$PREFIX/opt" "$VM"

# --- 86Box -------------------------------------------------------------------
if [[ ! -x "$PREFIX/opt/86box/squashfs-root/AppRun" ]]; then
  say "Recuperation de 86Box $BOX_VERSION"
  mkdir -p "$PREFIX/opt/86box"
  curl -fsSL -o "$PREFIX/opt/86box/86Box.AppImage" \
    "https://github.com/86Box/86Box/releases/download/$BOX_VERSION/86Box-Linux-x86_64-$BOX_BUILD.AppImage"
  chmod +x "$PREFIX/opt/86box/86Box.AppImage"
  # Extraction plutot que montage : FUSE est souvent absent des postes de build.
  ( cd "$PREFIX/opt/86box" && ./86Box.AppImage --appimage-extract >/dev/null )
else
  say "86Box deja present"
fi

# --- jeu de BIOS -------------------------------------------------------------
if [[ ! -d "$PREFIX/opt/86box/roms/machines" ]]; then
  say "Recuperation du jeu de BIOS 86Box"
  mkdir -p "$PREFIX/opt/86box/roms"
  tmp="$(mktemp -d)"
  curl -fsSL -o "$tmp/roms.tar.gz" https://github.com/86Box/roms/archive/refs/heads/master.tar.gz
  tar xzf "$tmp/roms.tar.gz" -C "$tmp"
  cp -r "$tmp/roms-master/." "$PREFIX/opt/86box/roms/"
  rm -rf "$tmp"
else
  say "Jeu de BIOS deja present"
fi
[[ -d "$PREFIX/opt/86box/roms/machines/p2bls" ]] \
  || die "Le jeu de BIOS ne contient pas la carte mere p2bls (Asus P2B-LS, 440BX)"

# --- mtools, pour ecrire dans le disque de transfert sans droits root --------
if [[ ! -x "$PREFIX/bin/mcopy" ]]; then
  say "Installation locale de mtools"
  tmp="$(mktemp -d)"
  ( cd "$tmp" && apt-get download mtools >/dev/null 2>&1 ) \
    || die "apt-get download mtools a echoue ; installez mtools autrement"
  dpkg-deb -x "$tmp"/mtools_*.deb "$PREFIX/opt/mtools"
  for t in mcopy mformat mmd mdir mdel mtype; do
    ln -sf "$PREFIX/opt/mtools/usr/bin/$t" "$PREFIX/bin/$t"
  done
  rm -rf "$tmp"
fi

# --- images disque -----------------------------------------------------------
# Creees, partitionnees et formatees par scripts/prepare_win95_install.py, qui a
# besoin de l'ISO de l'utilisateur pour remplir le disque source.
#
# Un disque dur DOS exige une table de partition : un volume FAT brut, sans MBR,
# n'est tout simplement pas vu par DOS. C'est pour cela que la creation des
# images n'est pas faite ici.
if [[ ! -f "$VM/install.img" ]]; then
  say "Images disque non preparees — etape suivante :"
  echo "    scripts/prepare_win95_install.py --iso /chemin/vers/W95.iso"
fi

# --- configuration de la machine --------------------------------------------
say "Ecriture de $VM/86box.cfg"
cat > "$VM/86box.cfg" <<EOF
# Machine de test du portage DKR-R vers Windows 95 + 3dfx.
# Generee par scripts/Setup-Win95-TestVM.sh — voir docs/TEST-ENVIRONMENT.md.
# Cible arretee par l'ADR 0002 (E00-S05).

[General]
vid_renderer = qt_software
vid_resize = 0
window_remember = 0
confirm_exit = 0
confirm_save = 0

[Machine]
machine = p2bls
cpu_family = $CPU_FAMILY
cpu_speed = $CPU_SPEED
cpu_multi = 4.0
cpu_use_dynarec = 1
fpu_type = internal
mem_size = $MEM_KB
time_sync = local

[Video]
gfxcard = virge375_pci
voodoo = 1

[3dfx Voodoo Graphics #1]
type = $VOODOO_TYPE
framebuffer_memory = $VOODOO_FB_MB
texture_memory = $VOODOO_TEX_MB
bilinear = 1
dithersub = 0
render_threads = 2
sli = 0

[Input devices]
mouse_type = ps2
joystick_type = 2axis_2button

[Sound]
sndcard = sb16
sound_type = float
opl_type = ymfm

[Storage controllers]
hdc = internal
fdc = internal

[Hard disks]
hdd_01_parameters = 63, 16, 2080, 0, ide
hdd_01_fn = win95.img
hdd_01_ide_channel = 0:0
hdd_02_parameters = 63, 16, 1024, 0, ide
hdd_02_fn = transfer.img
hdd_02_ide_channel = 0:1
hdd_03_parameters = 63, 16, 261, 0, ide
hdd_03_fn = install.img
hdd_03_ide_channel = 1:1

[Floppy and CD-ROM drives]
fdd_01_type = 35_2hd
fdd_01_fn = freedos-boot.img
cdrom_01_parameters = 8, ide
cdrom_01_ide_channel = 1:0
cdrom_01_image_path =

[Network]
net_01_card = none
EOF

cat <<EOF

$(say "Environnement pret")

  86Box       : $PREFIX/opt/86box/squashfs-root/AppRun
  BIOS        : $PREFIX/opt/86box/roms
  Machine     : $VM
  Disque C:   : $VM/win95.img    (systeme, a installer)
  Disque D:   : $VM/transfer.img (transfert hote <-> invite)
  Disque E:   : $VM/install.img   (source Windows 95)
  Disquette   : $VM/freedos-boot.img (demarrage FreeDOS)

Etape suivante — elle a besoin de VOTRE media Windows 95, qui est un logiciel
proprietaire de Microsoft et n'est pas telecharge par ces scripts :

  scripts/prepare_win95_install.py --iso /chemin/vers/W95.iso
  scripts/Run-Win95-VM.sh

Puis, dans l'invite FreeDOS : FDISK, redemarrer, FORMAT C: /U, et
E:\WIN95\INSTALL.EXE. Ensuite les pilotes 3dfx, puis --snapshot.

Voir docs/TEST-ENVIRONMENT.md.
EOF
