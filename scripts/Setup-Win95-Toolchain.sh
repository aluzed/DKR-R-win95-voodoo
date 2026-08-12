#!/usr/bin/env bash
# Installe, sans droits root, les outils nécessaires à la génération des sources
# recompilées et aux mesures de E00-S03 / E00-S04 sous Linux.
#
# Rien n'est installé à l'échelle du système : tout va dans un préfixe
# utilisateur, ajouté au PATH par la ligne affichée en fin d'exécution.
#
# Ce script existe parce que le chemin de préparation amont exige Windows,
# Visual Studio et WSL2 (docs/BUILDING.md), alors que la cible Win95 se construit
# par compilation croisée depuis Linux. Voir E01-S06.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
CMAKE_VERSION="3.31.6"
NINJA_VERSION="1.12.1"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

have curl || die "curl est requis"
have tar  || die "tar est requis"
have dpkg-deb || die "dpkg-deb est requis pour extraire les paquets sans root"

mkdir -p "$PREFIX/bin" "$PREFIX/opt"
tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT

# --- binutils MIPS -----------------------------------------------------------
# Le paquet Ubuntu et ses bibliothèques partagées sont extraits dans le préfixe ;
# des enveloppes fixent LD_LIBRARY_PATH pour que les binaires se retrouvent.
if [[ ! -x "$PREFIX/bin/mips-linux-gnu-as" ]]; then
  say "binutils MIPS"
  ( cd "$tmp" && apt-get download binutils-mips-linux-gnu binutils-common >/dev/null 2>&1 ) \
    || die "apt-get download a échoué ; installez binutils-mips-linux-gnu autrement"
  dpkg-deb -x "$tmp"/binutils-mips-linux-gnu_*.deb "$PREFIX/opt/mips-binutils"
  dpkg-deb -x "$tmp"/binutils-common_*.deb         "$PREFIX/opt/mips-binutils"
  for t in as ld objcopy objdump nm readelf ar ranlib strip size addr2line; do
    cat > "$PREFIX/bin/mips-linux-gnu-$t" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="$PREFIX/opt/mips-binutils/usr/lib/x86_64-linux-gnu:\$LD_LIBRARY_PATH"
exec "$PREFIX/opt/mips-binutils/usr/bin/mips-linux-gnu-$t" "\$@"
EOF
    chmod +x "$PREFIX/bin/mips-linux-gnu-$t"
  done
else
  say "binutils MIPS déjà présents"
fi

# --- cmake -------------------------------------------------------------------
if [[ ! -x "$PREFIX/opt/cmake/bin/cmake" ]]; then
  say "cmake $CMAKE_VERSION"
  curl -fsSL -o "$tmp/cmake.tar.gz" \
    "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz"
  tar xzf "$tmp/cmake.tar.gz" -C "$PREFIX/opt"
  mv "$PREFIX/opt/cmake-$CMAKE_VERSION-linux-x86_64" "$PREFIX/opt/cmake"
  ln -sf "$PREFIX/opt/cmake/bin/cmake" "$PREFIX/bin/cmake"
  ln -sf "$PREFIX/opt/cmake/bin/ctest" "$PREFIX/bin/ctest"
else
  say "cmake déjà présent"
fi

# --- ninja -------------------------------------------------------------------
if [[ ! -x "$PREFIX/bin/ninja" ]]; then
  say "ninja $NINJA_VERSION"
  curl -fsSL -o "$tmp/ninja.zip" \
    "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-linux.zip"
  python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
    "$tmp/ninja.zip" "$PREFIX/bin"
  chmod +x "$PREFIX/bin/ninja"
else
  say "ninja déjà présent"
fi

# --- uv ----------------------------------------------------------------------
# Le decomp crée son environnement Python avec `python3 -m venv`, qui exige le
# paquet python3-venv, donc apt, donc root. uv crée le même environnement sans
# ensurepip et sans élévation.
if [[ ! -x "$PREFIX/bin/uv" ]]; then
  say "uv"
  curl -fsSL https://astral.sh/uv/install.sh | env UV_INSTALL_DIR="$PREFIX/bin" sh >/dev/null 2>&1
else
  say "uv déjà présent"
fi

# --- vérification ------------------------------------------------------------
export PATH="$PREFIX/bin:$PATH"
say "Vérification"
printf '  %-24s %s\n' "cmake"              "$(cmake --version | head -1)"
printf '  %-24s %s\n' "ninja"              "$(ninja --version)"
printf '  %-24s %s\n' "mips-linux-gnu-as"  "$(mips-linux-gnu-as --version | head -1)"
printf '  %-24s %s\n' "uv"                 "$(uv --version)"
printf '  %-24s %s\n' "gcc -m32"           "$(echo 'int main(void){return 0;}' > "$tmp/t.c" && gcc -m32 "$tmp/t.c" -o "$tmp/t" 2>/dev/null && echo "fonctionnel" || echo "ABSENT — installez gcc-multilib")"

cat <<EOF

$(say "Prêt")

Ajoutez le préfixe au PATH :

  export PATH="$PREFIX/bin:\$PATH"

Puis, pour produire les sources recompilées depuis Linux :

  1. construire l'ELF de référence dans le decomp (make setup / extract / -j)
  2. scripts/generate_recomp_toml.py --elf … --rom … --policy … --output …
  3. extern/n64-modern-runtime/N64Recomp/build-linux/N64Recomp <toml>

Voir docs/research/cpu-budget.md pour la marche complète et ses mesures.
EOF
