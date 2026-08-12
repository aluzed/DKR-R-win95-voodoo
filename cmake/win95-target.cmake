# E01-S01 — cible de build Windows 95.
#
# Inclus depuis `runtime-recomp/CMakeLists.txt` quand `DKR_RUNTIME_TARGET_WIN95`
# est actif, juste après la déclaration des options — et le CMakeLists rend la
# main immédiatement après. Les cibles Windows, Linux et macOS ne traversent
# donc qu'un seul `if()` faux : elles sont inchangées par construction, ce qui
# est la garantie exigée par l'ADR 0004.
#
# Périmètre volontairement étroit. E01-S01 livre la chaîne de compilation, pas
# la compilation du jeu : `ultramodern` et `librecomp` ne passent pas encore
# (E01-S02, E01-S03), et le code recompilé attend E01-S05. Ce fichier construit
# donc le pont de compatibilité et un témoin, ce qui suffit à prouver que la
# chaîne produit un PE 32 bits exécutable sous Windows 95.

if(NOT DKR_WIN95_TOOLCHAIN)
    message(FATAL_ERROR
        "DKR_RUNTIME_TARGET_WIN95 exige le fichier de toolchain :\n"
        "  cmake -S runtime-recomp -B build/win95 -G Ninja \\\n"
        "        --toolchain cmake/toolchain-win95.cmake \\\n"
        "        -DDKR_RUNTIME_TARGET_WIN95=ON")
endif()

if(DKR_RUNTIME_BUILD_RT64)
    message(FATAL_ERROR
        "RT64 exige D3D12, Vulkan ou Metal : aucun n'existe sous Windows 95.\n"
        "Laisser DKR_RUNTIME_BUILD_RT64 à OFF, comme c'est le défaut.")
endif()

set(DKR_WIN95_TOOLS "${DKRPORT_ROOT}/tools/win95")

message(STATUS "Cible Windows 95 : ${CMAKE_C_COMPILER}")
message(STATUS "  jeu d'instructions : Pentium II, sans SSE, virgule flottante x87")
message(STATUS "  API : _WIN32_WINNT=0x0400")

# --- Pont de compatibilité ---------------------------------------------------
#
# Les six fonctions que la bibliothèque standard de GCC 13 réclame et que
# KERNEL32 de Windows 95 n'exporte pas. Voir ADR 0001 : sans elles le binaire ne
# se charge pas, et Windows nomme le symbole manquant dans une boîte d'erreur.
add_library(win95compat STATIC "${DKR_WIN95_TOOLS}/win95compat/win95compat.c")
target_include_directories(win95compat PUBLIC "${DKR_WIN95_TOOLS}/win95compat")
# `IsDebuggerPresent` et consorts sont déclarées `dllimport` par windows.h ; les
# redéfinir est précisément le but de ce fichier.
target_compile_options(win95compat PRIVATE -Wno-attributes)

# `--whole-archive` est obligatoire, pas prudentiel : l'archive n'est consultée
# qu'au moment où elle apparaît sur la ligne de commande, et `libwinpthread`
# n'introduit ses références qu'ensuite. Sans cela, `libkernel32.a` — placée en
# dernier par les specs du compilateur — l'emporte, et les symboles pointent
# vers des fonctions que Windows 95 n'a pas.
target_link_options(win95compat INTERFACE
    "-Wl,--whole-archive" "$<TARGET_FILE:win95compat>" "-Wl,--no-whole-archive")

# --- Vérification du jeu d'instructions --------------------------------------
#
# Étape obligatoire après le lien, et non outil facultatif : c'est la seule
# preuve que rien d'incompatible ne s'est glissé depuis le CRT ou la
# bibliothèque standard, que `-mno-sse` sur nos sources ne couvre pas.
function(dkr_win95_verify target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${DKR_WIN95_TOOLS}/check-instruction-set.sh" "$<TARGET_FILE:${target}>"
        COMMENT "Vérification du jeu d'instructions Pentium II : ${target}"
        VERBATIM)
endfunction()

# --- Témoin ------------------------------------------------------------------
#
# Deux fils, une section critique, un événement, RTTI et exceptions : la forme
# exacte du modèle d'exécution dont `ultramodern` a besoin. C'est le témoin T3b
# de E00-S02, qui affiche 1000/1000 sur la machine de test.
set(DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/t3b.cpp")

# Épreuve du vérificateur. Avec cette option, une unité de compilation est
# ajoutée et compilée en SSE : le contrôle post-lien doit alors faire échouer
# le build. C'est la seule façon de distinguer un vérificateur qui fonctionne
# d'un vérificateur cassé, les deux se taisant de la même manière.
#
#   cmake ... -DDKR_WIN95_SELFTEST_SSE=ON && cmake --build ...   # doit échouer
option(DKR_WIN95_SELFTEST_SSE "Injecter du SSE pour éprouver le vérificateur" OFF)
if(DKR_WIN95_SELFTEST_SSE)
    list(APPEND DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/sse_canary.c")
    set_source_files_properties("${DKR_WIN95_TOOLS}/witnesses/sse_canary.c"
        PROPERTIES COMPILE_OPTIONS "-msse;-mfpmath=sse")
    message(WARNING "DKR_WIN95_SELFTEST_SSE actif : le build DOIT échouer au contrôle post-lien")
endif()

add_executable(DKRWin95Witness ${DKR_WIN95_WITNESS_SOURCES})
target_link_libraries(DKRWin95Witness PRIVATE win95compat user32)
set_target_properties(DKRWin95Witness PROPERTIES
    OUTPUT_NAME "WITNESS"
    SUFFIX ".EXE"                      # 8.3, pour être lançable depuis DOS
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Witness)

message(STATUS "Cible Windows 95 configurée : win95compat, DKRWin95Witness")
