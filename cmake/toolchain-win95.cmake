# E01-S01 — chaîne de compilation croisée vers Windows 95 / Pentium II.
#
#   cmake -S runtime-recomp -B build/win95 -G Ninja \
#         --toolchain cmake/toolchain-win95.cmake \
#         -DDKR_RUNTIME_TARGET_WIN95=ON
#
# Le choix du compilateur vient de l'ADR 0001 (E00-S02) : mingw-w64 GCC 13,
# modèle de threads `posix`, CRT lié statiquement, avec le pont `win95compat`.
# Open Watcom reste le repli documenté par l'ADR, non retenu ici parce que son
# C++98 imposerait de réécrire `ultramodern` et `librecomp`.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_VERSION   4.0)          # Windows 95
set(CMAKE_SYSTEM_PROCESSOR i686)

set(DKR_WIN95_PREFIX "$ENV{DKR_WIN95_PREFIX}" CACHE PATH
    "Préfixe d'installation sans droits de l'outillage Win95")
if(DKR_WIN95_PREFIX STREQUAL "")
    set(DKR_WIN95_PREFIX "$ENV{HOME}/.local/dkr-win95")
endif()
set(DKR_WIN95_MINGW_BIN "${DKR_WIN95_PREFIX}/opt/mingw/usr/bin")

# Le suffixe `-posix` n'est pas cosmétique : il sélectionne winpthreads plutôt
# que le modèle `win32`, dont les variables de condition sont celles de Vista.
# Les deux modèles manquent d'autant de symboles, mais ceux du modèle `posix`
# s'écrivent en quelques lignes — voir ADR 0001.
set(DKR_WIN95_TRIPLE  i686-w64-mingw32)
set(DKR_WIN95_SUFFIX  -posix)

find_program(CMAKE_C_COMPILER   "${DKR_WIN95_TRIPLE}-gcc${DKR_WIN95_SUFFIX}"
             HINTS "${DKR_WIN95_MINGW_BIN}" REQUIRED)
find_program(CMAKE_CXX_COMPILER "${DKR_WIN95_TRIPLE}-g++${DKR_WIN95_SUFFIX}"
             HINTS "${DKR_WIN95_MINGW_BIN}" REQUIRED)
find_program(CMAKE_RC_COMPILER  "${DKR_WIN95_TRIPLE}-windres"
             HINTS "${DKR_WIN95_MINGW_BIN}")
find_program(CMAKE_AR           "${DKR_WIN95_TRIPLE}-ar"
             HINTS "${DKR_WIN95_MINGW_BIN}")
find_program(CMAKE_RANLIB       "${DKR_WIN95_TRIPLE}-ranlib"
             HINTS "${DKR_WIN95_MINGW_BIN}")
find_program(CMAKE_OBJDUMP      "${DKR_WIN95_TRIPLE}-objdump"
             HINTS "${DKR_WIN95_MINGW_BIN}")

set(CMAKE_FIND_ROOT_PATH "${DKR_WIN95_PREFIX}/opt/mingw/usr/${DKR_WIN95_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- Jeu d'instructions ------------------------------------------------------
#
# Le Pentium II n'a ni SSE ni SSE2 ; un compilateur moderne en 32 bits émet
# pourtant du SSE2 par défaut pour l'arithmétique flottante. `-mfpmath=387`
# force la pile x87.
#
# Ces options ne suffisent pas à elles seules : elles ne couvrent que le code
# compilé ici, pas le démarrage du CRT ni la bibliothèque standard. C'est
# `tools/win95/check-instruction-set.sh`, branché après le lien, qui apporte la
# preuve — et il est vérifié par injection de SSE.
set(DKR_WIN95_ARCH_FLAGS
    "-march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2")

# --- Version de Windows ------------------------------------------------------
#
# 0x0400 masque les API postérieures à Windows 95 dans les en-têtes mingw.
# Vérifié : `InitializeConditionVariable` disparaît bien de la sortie du
# préprocesseur.
#
# Mais en C, appeler une fonction non déclarée n'est encore qu'un avertissement
# avec GCC 13. Sans `-Werror=implicit-function-declaration`, une API de Vista
# passerait la compilation pour échouer au chargement. En C++ l'erreur est
# native.
set(DKR_WIN95_DEFINES "-D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -DDKR_TARGET_WIN95=1")

set(CMAKE_C_FLAGS_INIT
    "${DKR_WIN95_ARCH_FLAGS} ${DKR_WIN95_DEFINES} -Werror=implicit-function-declaration")
set(CMAKE_CXX_FLAGS_INIT
    "${DKR_WIN95_ARCH_FLAGS} ${DKR_WIN95_DEFINES}")

# --- Édition de liens --------------------------------------------------------
#
# Statique sans exception. `libgcc_s_dw2-1.dll` n'existe pas sous Windows 95, et
# `MSVCRT.DLL` n'est pas présent dans la première génération du système : il
# arrive avec OSR2 ou Internet Explorer. En dépendre reviendrait à faire dépendre
# le jeu d'une version d'IE.
set(DKR_WIN95_LINK_STATIC "-static -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${DKR_WIN95_LINK_STATIC}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${DKR_WIN95_LINK_STATIC}")

set(DKR_WIN95_TOOLCHAIN ON CACHE BOOL "Compilation croisée vers Windows 95" FORCE)
