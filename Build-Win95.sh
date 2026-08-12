#!/usr/bin/env bash
# E01-S01 — construit la cible Windows 95 / 3dfx Voodoo.
#
#   ./Build-Win95.sh
#   DKR_WIN95_BUILD_DIR=/tmp/w95 ./Build-Win95.sh
#
# Sur le modèle de Build-Linux.sh, avec les mêmes vérifications de prérequis en
# tête de script : sur cette cible, un outil manquant se manifeste autrement par
# une erreur de compilation obscure une minute plus tard.
#
# Ce que ce script construit aujourd'hui : le pont de compatibilité et le
# témoin. Le jeu lui-même n'est pas encore compilable pour cette cible —
# `ultramodern` et `librecomp` attendent E01-S02 et E01-S03, le code recompilé
# attend E01-S05.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DKR_WIN95_BUILD_DIR:-${project_root}/build/win95}"
prefix="${DKR_WIN95_PREFIX:-${HOME}/.local/dkr-win95}"
export PATH="${prefix}/bin:${prefix}/opt/mingw/usr/bin:${PATH}"

fail() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }
say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# --- Prérequis ---------------------------------------------------------------

command -v cmake >/dev/null || fail \
  "cmake est absent. Installez-le sans droits avec scripts/Setup-Win95-Toolchain.sh"
command -v ninja >/dev/null || fail \
  "ninja est absent. Installez-le sans droits avec scripts/Setup-Win95-Toolchain.sh"

for tool in i686-w64-mingw32-gcc-posix i686-w64-mingw32-g++-posix i686-w64-mingw32-objdump; do
  command -v "$tool" >/dev/null || fail \
    "${tool} est absent. La cible exige mingw-w64 i686, modèle de threads posix :
    apt-get download g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix \\
                     gcc-mingw-w64-i686-posix-runtime
    puis dpkg-deb -x chaque paquet dans ${prefix}/opt/mingw/
  Le suffixe -posix n'est pas facultatif : voir docs/adr/0001-toolchain.md."
done

[[ -f "${project_root}/cmake/toolchain-win95.cmake" ]] || fail \
  "cmake/toolchain-win95.cmake est absent."
[[ -x "${project_root}/tools/win95/check-instruction-set.sh" ]] || fail \
  "tools/win95/check-instruction-set.sh est absent ou non exécutable."

# Le vérificateur est éprouvé avant d'être employé : un vérificateur cassé et un
# vérificateur satisfait se taisent de la même manière.
say "Épreuve du vérificateur de jeu d'instructions"
"${project_root}/tools/win95/check-instruction-set.sh" --self-test >/dev/null \
  || fail "le vérificateur de jeu d'instructions ne détecte pas le SSE injecté."

# --- Configuration et compilation --------------------------------------------

say "Configuration (${build_dir})"
cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  --toolchain "${project_root}/cmake/toolchain-win95.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RUNTIME_TARGET_WIN95=ON

say "Compilation"
cmake --build "${build_dir}" --parallel

# --- Contrôles de sortie -----------------------------------------------------
#
# Le contrôle du jeu d'instructions est déjà passé, en étape post-lien de la
# cible. Reste celui des imports : sous Windows 95 le chargeur résout tous les
# imports au démarrage, donc un symbole absent est fatal même si la fonction
# n'est jamais appelée — et rien ne le signale au lien.

witness="${build_dir}/bin/WITNESS.EXE"
[[ -f "${witness}" ]] || fail "témoin absent après compilation : ${witness}"

# Les deux contrôles sont déjà passés en étape post-lien de chaque cible ; on les
# rejoue ici sur le binaire final, parce que c'est celui-là qui sera copié sur la
# machine et que le script doit pouvoir être lancé sur un build existant.
say "Contrôle des imports contre les exports réels de Windows 95"
python3 "${project_root}/tools/win95/check_imports.py" \
        --objects "${build_dir}" "${witness}" \
  || fail "le témoin réclame des symboles absents de Windows 95."

printf '\n\033[1;32mCible Windows 95 construite.\033[0m\n'
printf 'Témoin : %s\n' "${witness}"
printf 'À exécuter sur la machine de test :\n'
printf '  scripts/Push-To-Win95-VM.sh %s\n' "${witness}"
