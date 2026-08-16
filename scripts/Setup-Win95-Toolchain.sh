#!/usr/bin/env bash
# Installs, without root privileges, the tools needed to generate the recompiled
# sources and to run E00-S03 / E00-S04's measurements under Linux.
#
# Nothing is installed system-wide: everything goes into a user prefix, added to
# the PATH by the line printed at the end of the run.
#
# This script exists because the upstream preparation path requires Windows,
# Visual Studio and WSL2 (docs/BUILDING.md), whereas the Win95 target is built by
# cross-compilation from Linux. See E01-S06.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
CMAKE_VERSION="3.31.6"
NINJA_VERSION="1.12.1"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

have curl || die "curl is required"
have tar  || die "tar is required"
have dpkg-deb || die "dpkg-deb is required to extract the packages without root"

mkdir -p "$PREFIX/bin" "$PREFIX/opt"
tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT

# --- binutils MIPS -----------------------------------------------------------
# The Ubuntu package and its shared libraries are extracted into the prefix;
# wrappers set LD_LIBRARY_PATH so that the binaries find each other.
if [[ ! -x "$PREFIX/bin/mips-linux-gnu-as" ]]; then
  say "binutils MIPS"
  ( cd "$tmp" && apt-get download binutils-mips-linux-gnu binutils-common >/dev/null 2>&1 ) \
    || die "apt-get download failed; install binutils-mips-linux-gnu some other way"
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
  say "MIPS binutils already present"
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
  say "cmake already present"
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
  say "ninja already present"
fi

# --- uv ----------------------------------------------------------------------
# The decomp creates its Python environment with `python3 -m venv`, which requires
# the python3-venv package, hence apt, hence root. uv creates the same environment
# without ensurepip and without elevation.
if [[ ! -x "$PREFIX/bin/uv" ]]; then
  say "uv"
  curl -fsSL https://astral.sh/uv/install.sh | env UV_INSTALL_DIR="$PREFIX/bin" sh >/dev/null 2>&1
else
  say "uv already present"
fi

# --- verification ------------------------------------------------------------
export PATH="$PREFIX/bin:$PATH"
say "Verification"
printf '  %-24s %s\n' "cmake"              "$(cmake --version | head -1)"
printf '  %-24s %s\n' "ninja"              "$(ninja --version)"
printf '  %-24s %s\n' "mips-linux-gnu-as"  "$(mips-linux-gnu-as --version | head -1)"
printf '  %-24s %s\n' "uv"                 "$(uv --version)"
printf '  %-24s %s\n' "gcc -m32"           "$(echo 'int main(void){return 0;}' > "$tmp/t.c" && gcc -m32 "$tmp/t.c" -o "$tmp/t" 2>/dev/null && echo "working" || echo "ABSENT - install gcc-multilib")"

cat <<EOF

$(say "Ready")

Add the prefix to the PATH:

  export PATH="$PREFIX/bin:\$PATH"

Then, to produce the recompiled sources from Linux:

  1. build the reference ELF in the decomp (make setup / extract / -j)
  2. scripts/generate_recomp_toml.py --elf ... --rom ... --policy ... --output ...
  3. extern/n64-modern-runtime/N64Recomp/build-linux/N64Recomp <toml>

See docs/research/cpu-budget.md for the full procedure and its measurements.
EOF
