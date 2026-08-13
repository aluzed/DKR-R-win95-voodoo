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

set(DKR_WIN95_TOOLS    "${DKRPORT_ROOT}/tools/win95")
set(DKR_WIN95_PLATFORM "${DKRPORT_ROOT}/platform/win95")

# Le contrôle des imports est écrit en Python et n'a aucune dépendance : il lit
# la table d'imports du PE lui-même.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

message(STATUS "Cible Windows 95 : ${CMAKE_C_COMPILER}")
message(STATUS "  jeu d'instructions : Pentium II, sans SSE, virgule flottante x87")
message(STATUS "  API : _WIN32_WINNT=0x0400")

# --- Sous-ensemble C++ (E01-S02) ---------------------------------------------
#
# Il n'y a pas de norme à restreindre : GCC 13 implémente tout C++20 pour cette
# cible. Ce qui est interdit, ce sont des facilités de bibliothèque dont la
# simple inclusion fait apparaître dans la table d'imports des symboles que
# Windows 95 n'exporte pas — `<thread>`, `<mutex>`, `<filesystem>` et leurs
# voisins. Voir docs/CPP-SUBSET.md.
#
# Le contrôle est en **pré-build** et non en post-lien : une inclusion interdite
# compile parfaitement, et ne se manifeste qu'au chargement sur la machine
# cible. Le compilateur ne dira rien.
#
# Il ne porte pour l'instant que sur les sources de la cible. `ultramodern`
# compte encore 9 inclusions interdites : c'est exactement le travail de
# E02-S01, et l'y soumettre aujourd'hui ne ferait qu'échouer sans rien apprendre.
add_custom_target(dkr_win95_cpp_subset ALL
    COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check-cpp-subset.py"
            "${DKR_WIN95_PLATFORM}"
    COMMENT "Contrôle du sous-ensemble C++ autorisé"
    VERBATIM)

# --- Pont de compatibilité ---------------------------------------------------
#
# Les six fonctions que la bibliothèque standard de GCC 13 réclame et que
# KERNEL32 de Windows 95 n'exporte pas. Voir ADR 0001 : sans elles le binaire ne
# se charge pas, et Windows nomme le symbole manquant dans une boîte d'erreur.
add_library(win95compat STATIC
    "${DKR_WIN95_PLATFORM}/compat.c"
    "${DKR_WIN95_PLATFORM}/tick64.c"
    "${DKR_WIN95_PLATFORM}/startup.c")
target_include_directories(win95compat PUBLIC "${DKR_WIN95_PLATFORM}")
# `IsDebuggerPresent` et consorts sont déclarées `dllimport` par windows.h ; les
# redéfinir est précisément le but de ce fichier.
target_compile_options(win95compat PRIVATE -Wno-attributes)
# Le contrôle du sous-ensemble passe avant toute compilation.
add_dependencies(win95compat dkr_win95_cpp_subset)

# `--whole-archive` est obligatoire, pas prudentiel : l'archive n'est consultée
# qu'au moment où elle apparaît sur la ligne de commande, et `libwinpthread`
# n'introduit ses références qu'ensuite. Sans cela, `libkernel32.a` — placée en
# dernier par les specs du compilateur — l'emporte, et les symboles pointent
# vers des fonctions que Windows 95 n'a pas.
target_link_options(win95compat INTERFACE
    "-Wl,--whole-archive" "$<TARGET_FILE:win95compat>" "-Wl,--no-whole-archive")

# --- Couche de fils et de synchronisation (E02-S01) --------------------------
#
# Bibliothèque distincte de `win95compat`, et non fusionnée avec elle, pour une
# raison mécanique : celle-ci est en C++, et les témoins en C de E01-S03 se lient
# avec le compilateur C. Les fusionner obligerait ces derniers à traîner
# libstdc++ sans en avoir l'usage.
#
# La dépendance va dans l'autre sens : la couche s'appuie sur `win95compat` pour
# les cinq fonctions de section critique et pour le journal de démarrage.
add_library(win95threading STATIC "${DKR_WIN95_PLATFORM}/threading.cpp")
target_include_directories(win95threading PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95threading PUBLIC win95compat)
add_dependencies(win95threading dkr_win95_cpp_subset)

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

    # Second garde-fou, celui des symboles (E01-S04). Sous Windows 95, le
    # chargeur résout tous les imports au démarrage : un symbole absent empêche
    # le processus de démarrer, même si la fonction n'est jamais appelée. Rien
    # ne le signale au lien, et la découverte coûte un aller-retour vers la
    # machine de test.
    #
    # `--objects` sur le répertoire de build permet de nommer l'objet fautif :
    # la table d'imports du PE ne conserve pas cette information, elle est
    # perdue au lien.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check_imports.py"
                --objects "${CMAKE_BINARY_DIR}" "$<TARGET_FILE:${target}>"
        COMMENT "Vérification des imports contre les exports de Windows 95 : ${target}"
        VERBATIM)
endfunction()

# --- Témoin ------------------------------------------------------------------
#
# Deux fils, une section critique, un événement, RTTI et exceptions : la forme
# exacte du modèle d'exécution dont `ultramodern` a besoin. C'est le témoin T3b
# de E00-S02, qui affiche 1000/1000 sur la machine de test.
set(DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/t3b.cpp")

# Second témoin : celui de E01-S03, qui exerce la couche entière — démarrage,
# journal, filtre d'exceptions, contrôle de version, les six API manquantes,
# l'horloge 64 bits et deux fils en contention.
add_executable(DKRWin95Platform "${DKR_WIN95_PLATFORM}/witness.c")
target_link_libraries(DKRWin95Platform PRIVATE win95compat user32)
set_target_properties(DKRWin95Platform PROPERTIES
    OUTPUT_NAME "PLATFORM"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Platform)

# Troisième témoin : l'épreuve de la couche de fils (E02-S01). C'est
# **exactement la même source** que la suite exécutée sur l'hôte — deux fichiers
# distincts finiraient par diverger, et c'est justement sur la cible que les
# différences comptent.
#
#   scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE
#   THREADS.EXE                    la suite
#   THREADS.EXE --stress 600       l'endurance de dix minutes
add_executable(DKRWin95Threads "${DKR_WIN95_PLATFORM}/tests/test_threading.cpp")
target_link_libraries(DKRWin95Threads PRIVATE win95threading)
set_target_properties(DKRWin95Threads PROPERTIES
    OUTPUT_NAME "THREADS"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Threads)

# Les tests qui tournent sur l'hôte. Deux suites, pour deux raisons :
#
#   Tick64     (E01-S03) le rebouclage de GetTickCount est une fonction pure, et
#              attendre 49,7 jours n'est pas un protocole de test.
#   Threading  (E02-S01) la même source que THREADS.EXE, sur le véhicule POSIX.
#              Passer ici ne prouve rien de la cible — c'est pourquoi le binaire
#              cible est aussi exécuté sur la machine — mais raccourcit le cycle
#              de mise au point de plusieurs minutes à une seconde.
#
# Les deux tournent avec le compilateur de l'hôte et non celui de la cible.
enable_testing()
add_test(NAME DKRWin95Tick64
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" tick64)
add_test(NAME DKRWin95Threading
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" threading)

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

# Épreuve du contrôle des imports, symétrique de la précédente. Cette unité de
# compilation importe `GetTickCount64`, qui est de Vista : le contrôle post-lien
# doit faire échouer le build en la nommant, et en nommant l'objet fautif.
#
#   cmake ... -DDKR_WIN95_SELFTEST_IMPORT=ON && cmake --build ...   # doit échouer
option(DKR_WIN95_SELFTEST_IMPORT "Importer une API de Vista pour éprouver le contrôle" OFF)
if(DKR_WIN95_SELFTEST_IMPORT)
    list(APPEND DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/import_canary.c")
    message(WARNING "DKR_WIN95_SELFTEST_IMPORT actif : le build DOIT échouer au contrôle des imports")
endif()

add_executable(DKRWin95Witness ${DKR_WIN95_WITNESS_SOURCES})
target_link_libraries(DKRWin95Witness PRIVATE win95compat user32)
set_target_properties(DKRWin95Witness PROPERTIES
    OUTPUT_NAME "WITNESS"
    SUFFIX ".EXE"                      # 8.3, pour être lançable depuis DOS
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Witness)

message(STATUS "Cible Windows 95 configurée : win95compat, DKRWin95Witness")
