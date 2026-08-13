# E01-S01 — cible de build Windows 95.
#
# Inclus depuis `runtime-recomp/CMakeLists.txt` quand `DKR_RUNTIME_TARGET_WIN95`
# est actif, juste après la déclaration des options — et le CMakeLists rend la
# main immédiatement après. Les cibles Windows, Linux et macOS ne traversent
# donc qu'un seul `if()` faux : elles sont inchangées par construction, ce qui
# est la garantie exigée par l'ADR 0004.
#
# Périmètre. E01-S01 a livré la chaîne de compilation ; E01-S03 le pont de
# compatibilité ; E02-S01 la couche de fils ; E02-S02 y a reposé `ultramodern`,
# qui compile désormais ici en entier. Restent hors de portée `librecomp`
# (E02-S05 pour son système de fichiers) et le code recompilé (E01-S05) : le jeu
# ne se lie donc pas encore pour cette cible.

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

# --- Type de construction ----------------------------------------------------
#
# CMake laisse `CMAKE_BUILD_TYPE` vide par défaut, ce qui donne un binaire sans
# optimisation. Sur les cibles modernes c'est un désagrément ; ici c'est un
# piège, et il est silencieux.
#
# Mesure : sans type, `DKRR.EXE` fait 20,6 Mo ; en Release, 8,5 Mo. Et la taille
# n'est pas le pire — le cœur de ce portage est du MIPS recompilé en C, dont le
# coût par instruction décide de tout sur un Pentium II à 400 MHz (E00-S03).
# Non optimisé, il ne serait pas « plus lent » : il serait injouable, sans que
# rien ne l'annonce.
#
# On choisit donc pour l'appelant qui n'a rien choisi, et on le dit.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de construction" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE non défini : Release imposé "
                   "(sans optimisation, la cible est injouable)")
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
# Il porte sur les sources de la cible ; `ultramodern` a son propre contrôle
# plus bas, avec un cliquet, depuis que E02-S02 a fait tomber ses inclusions
# interdites de neuf à une.
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

# --- Base de temps (E02-S03) -------------------------------------------------
#
# Séparée de `win95threading` parce qu'elle a une dépendance de plus — `winmm`,
# pour `timeGetTime` et `timeBeginPeriod` — et qu'il n'y a pas de raison de la
# faire porter aux témoins qui n'ont besoin que des fils.
add_library(win95clock STATIC "${DKR_WIN95_PLATFORM}/clock.cpp")
target_include_directories(win95clock PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95clock PUBLIC win95compat winmm)
add_dependencies(win95clock dkr_win95_cpp_subset)

# --- Écriture de fichiers (E02-S05) ------------------------------------------
#
# Ne dépend que de `win95compat`, comme la couche de fils : la séquence
# d'écriture durable n'appelle que des API `...A` présentes et implémentées.
add_library(win95fileio STATIC "${DKR_WIN95_PLATFORM}/fileio.cpp")
target_include_directories(win95fileio PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95fileio PUBLIC win95compat)
add_dependencies(win95fileio dkr_win95_cpp_subset)

# --- ultramodern (E02-S02) ---------------------------------------------------
#
# Le patch 0015 route les cinq primitives de `ultramodern` — `thread`, `mutex`,
# `condition_variable`, `lock_guard`, `unique_lock` — par un point d'indirection
# que la cible remplit ici avec la couche de E02-S01.
#
# Le chemin d'inclusion est `win95/threading.hpp` et non `threading.hpp`, avec
# `platform` sur le chemin de recherche : `ultramodern` a lui-même un fichier
# nommé `threading.hpp`, et une inclusion entre guillemets consulte d'abord le
# répertoire du fichier qui inclut. Le nom court le faisait donc retomber sur
# lui-même, avec pour seul symptôme des centaines d'erreurs sur des types
# absents.
#
# **Ce n'est pas encore chargeable sous Windows 95**, et il faut le dire :
# `ultramodern.hpp` inclut toujours `<filesystem>`, qui réclame à lui seul
# treize symboles absents du système. C'est le travail de E02-S05. Cette cible
# établit la compilation et la substitution des primitives, pas le chargement.
file(GLOB DKR_WIN95_ULTRAMODERN_SOURCES
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern/src/*.cpp")

add_library(win95ultramodern STATIC ${DKR_WIN95_ULTRAMODERN_SOURCES})
target_include_directories(win95ultramodern PUBLIC
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/concurrentqueue"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/include"
    "${DKR_WIN95_PLATFORM}/include-shim"
    "${DKRPORT_ROOT}/platform")
target_compile_definitions(win95ultramodern PRIVATE
    NOMINMAX
    "ULTRAMODERN_PLATFORM_THREADING_HEADER=\"win95/threading.hpp\""
    "ULTRAMODERN_PLATFORM_THREADING_NS=dkr::win95")
target_link_libraries(win95ultramodern PUBLIC win95threading)
add_dependencies(win95ultramodern dkr_win95_cpp_subset)

# Le contrôle du sous-ensemble C++ porte désormais aussi sur `ultramodern` : il
# n'aurait rien appris tant que neuf inclusions interdites y subsistaient ; il
# en reste une, et elle est attribuée.
#
# Le cliquet `--max 1` a été retiré : `ultramodern` n'a plus aucune inclusion
# interdite. Il en restait une, `<filesystem>`, et elle a disparu non pas en
# réécrivant du code mais parce que la mesure a montré que la règle était
# fausse — l'inclusion et le type `std::filesystem::path` ne coûtent rien sous
# Windows 95, seules les opérations coûtent. Voir docs/research/win95-filesystem.md.
#
# C'est le cliquet qui l'a signalé, en disant que sa tolérance n'avait plus lieu
# d'être. Un seuil qu'on ne desserre jamais cesse de protéger.
add_custom_target(dkr_win95_cpp_subset_ultramodern ALL
    COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check-cpp-subset.py"
            "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern"
    COMMENT "Contrôle du sous-ensemble C++ : ultramodern"
    VERBATIM)

# --- librecomp (E01-S05) -----------------------------------------------------
#
# Le patch 0016 lève les deux hypothèses 64 bits de `librecomp` : le hachage de
# `HookDefinition`, qui empaquetait trois valeurs dans un `size_t` en exigeant
# qu'il fasse 64 bits, et le trampoline de `patch_func`, qui ne connaissait que
# x86_64 et ARM64. Les 26 unités de traduction compilent depuis.
#
# `miniz_export.h` vient de la cale : c'est un en-tête que le CMake de miniz
# fabrique à la configuration, et cette cible ne construit pas miniz par son
# CMake.
#
# **Ceci ne rend pas le jeu chargeable.** `librecomp` porte encore
# `<filesystem>` en abondance — c'est le travail de E02-S05 — et cette cible
# établit la compilation, pas le chargement.
file(GLOB DKR_WIN95_LIBRECOMP_SOURCES
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/src/*.cpp")

# Les deux bibliothèques tierces que `librecomp` réclame à l'édition de liens, et
# que la construction de cette cible laissait de côté tant qu'elle n'établissait
# que la compilation.
#
#   o1heap  l'allocateur du tas N64, employé par `heap.cpp` — cœur, indispensable
#   miniz   la lecture d'archives ZIP, employée par le seul système de mods.
#           Quatre fichiers et non un : `miniz.c` seul ne porte pas l'API zip,
#           qui vit dans `miniz_zip.c` — l'objet ne faisait que 11 Ko et le
#           lieur réclamait toujours `mz_zip_reader_*`.
#
# Elles sont compilées en C ici, et non par leur propre CMake : celui de miniz
# fabriquerait `miniz_export.h` à la configuration, d'où la cale.
list(APPEND DKR_WIN95_LIBRECOMP_SOURCES
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/o1heap/o1heap/o1heap.c"
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/miniz/miniz.c"
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/miniz/miniz_zip.c"
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/miniz/miniz_tinfl.c"
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/miniz/miniz_tdef.c")

add_library(win95librecomp STATIC ${DKR_WIN95_LIBRECOMP_SOURCES})
target_include_directories(win95librecomp PUBLIC
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/include/librecomp"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/src"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/lib/rabbitizer/cplusplus/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/lib/rabbitizer/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/concurrentqueue"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/o1heap"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/miniz"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/o1heap"
    "${DKR_WIN95_PLATFORM}/include-shim"
    "${DKRPORT_ROOT}/include")
# Le patch 0018 route les opérations de fichiers du cœur de `librecomp` par un
# point d'indirection, que la cible remplit ici avec la couche de E02-S05.
#
# Le type `std::filesystem::path` n'est **pas** remplacé, et c'est mesuré : sous
# Windows 95 il ne coûte rien et fonctionne. Seules les opérations coûtent.
#
# Le patch 0020 fait de même pour la synchronisation. La différence entre les
# deux mérite d'être notée : pour les fichiers, seules les *opérations* sont
# détournées et le type reste ; ici ce sont les types eux-mêmes qui ne passent
# pas, l'inclusion de `<mutex>` suffisant à empêcher le chargement. Le nom
# change donc à chaque déclaration.
target_compile_definitions(win95librecomp PRIVATE
    NOMINMAX
    "LIBRECOMP_PLATFORM_FILEIO_HEADER=\"win95/fileio.hpp\""
    "LIBRECOMP_PLATFORM_FILEIO_NS=dkr::fs"
    "LIBRECOMP_PLATFORM_SYNC_HEADER=\"win95/threading.hpp\""
    "LIBRECOMP_PLATFORM_SYNC_NS=dkr::win95"
    DKR_TARGET_WIN95=1)
target_link_libraries(win95librecomp PUBLIC win95ultramodern)
add_dependencies(win95librecomp dkr_win95_cpp_subset)

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

# Quatrième témoin : le pont C++ de E02-S02, exercé dans les formes exactes
# qu'`ultramodern` emploie — construction variadique, détachement immédiat,
# lock_guard sous ses deux formes, et la variable de condition avec ses quatre
# usages réels.
add_executable(DKRWin95ThreadsCpp
    "${DKR_WIN95_PLATFORM}/tests/test_threading_cpp.cpp")
target_link_libraries(DKRWin95ThreadsCpp PRIVATE win95threading)
set_target_properties(DKRWin95ThreadsCpp PROPERTIES
    OUTPUT_NAME "THRCPP"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95ThreadsCpp)

# Cinquième témoin : la base de temps de E02-S03. Le mode `--long` mesure la
# dérive sur une durée choisie — une base qui dérive lentement ne casse rien de
# visible et fausse tous les chronométrages, donc le contrôle doit porter sur
# une durée et non sur un instant.
add_executable(DKRWin95Clock "${DKR_WIN95_PLATFORM}/tests/test_clock.cpp")
target_link_libraries(DKRWin95Clock PRIVATE win95clock)
set_target_properties(DKRWin95Clock PROPERTIES
    OUTPUT_NAME "CLOCKT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Clock)

# Sixième témoin : l'écriture durable de E02-S05. Ce qu'il éprouve n'est pas que
# le fichier s'écrive — c'est ce qui reste sur le disque quand l'écriture est
# interrompue, en fabriquant à la main les états intermédiaires que la séquence
# traverse.
add_executable(DKRWin95FileIO "${DKR_WIN95_PLATFORM}/tests/test_fileio.cpp")
target_link_libraries(DKRWin95FileIO PRIVATE win95fileio)
set_target_properties(DKRWin95FileIO PROPERTIES
    OUTPUT_NAME "FILEIOT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95FileIO)

# Septième témoin : le point d'indirection des opérations de fichiers. Ce qu'il
# éprouve est l'**équivalence** des deux branches — `std::filesystem` sur l'hôte,
# les API `...A` sur la cible — parce qu'un point d'indirection dont les deux
# côtés diffèrent est pire que pas de point d'indirection du tout.
add_executable(DKRWin95FileIOSeam
    "${DKR_WIN95_PLATFORM}/tests/test_fileio_seam.cpp")
target_link_libraries(DKRWin95FileIOSeam PRIVATE win95fileio)
target_compile_definitions(DKRWin95FileIOSeam PRIVATE DKR_TARGET_WIN95=1)
set_target_properties(DKRWin95FileIOSeam PROPERTIES
    OUTPUT_NAME "FSSEAM"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95FileIOSeam)

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
add_test(NAME DKRWin95Clock
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" clock)
add_test(NAME DKRWin95FileIO
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" fileio)

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

# --- Recompilateur à la volée (E02-S06) --------------------------------------
#
# `librecomp` l'appelle depuis `initialize_mods`, et le système de mods est
# compilé pour cette cible même s'il n'y fonctionnera pas : ses symboles doivent
# donc être résolus, sinon rien ne se lie.
#
# C'est un JIT — il écrit du code machine dans une page allouée à l'exécution.
# `sljit` détecte l'absence de SSE2 au démarrage et se rabat sur x87, ce qui le
# rend viable sur un Pentium II. Le contrôle du jeu d'instructions le confirme :
# ses tables SSE2 sont des **données**, jamais exécutées ici.
#
# `fmt` est employée en mode en-tête seul : elle n'a pas besoin d'être construite,
# et cela évite une bibliothèque de plus.
set(DKR_WIN95_N64RECOMP "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp")

# `GLOB_RECURSE` et non `GLOB` : les sources de rabbitizer sont réparties sur
# plusieurs niveaux, et un motif à profondeur fixe en laissait derrière — le
# lieur réclamait alors `RabbitizerInstruction_getRaw` et une trentaine d'autres.
file(GLOB_RECURSE DKR_WIN95_RABBITIZER_C   CONFIGURE_DEPENDS
     "${DKR_WIN95_N64RECOMP}/lib/rabbitizer/src/*.c")
file(GLOB_RECURSE DKR_WIN95_RABBITIZER_CPP CONFIGURE_DEPENDS
     "${DKR_WIN95_N64RECOMP}/lib/rabbitizer/cplusplus/src/*.cpp")

add_library(win95liverecomp STATIC
    "${DKR_WIN95_N64RECOMP}/src/analysis.cpp"
    "${DKR_WIN95_N64RECOMP}/src/operations.cpp"
    "${DKR_WIN95_N64RECOMP}/src/cgenerator.cpp"
    "${DKR_WIN95_N64RECOMP}/src/recompilation.cpp"
    "${DKR_WIN95_N64RECOMP}/src/mod_symbols.cpp"
    "${DKR_WIN95_N64RECOMP}/LiveRecomp/live_generator.cpp"
    "${DKR_WIN95_N64RECOMP}/lib/sljit/sljit_src/sljitLir.c"
    ${DKR_WIN95_RABBITIZER_C}
    ${DKR_WIN95_RABBITIZER_CPP})
target_include_directories(win95liverecomp PUBLIC
    "${DKR_WIN95_N64RECOMP}/include"
    "${DKR_WIN95_N64RECOMP}/lib/sljit/sljit_src"
    "${DKR_WIN95_N64RECOMP}/lib/rabbitizer/include"
    "${DKR_WIN95_N64RECOMP}/lib/rabbitizer/tables"
    "${DKR_WIN95_N64RECOMP}/lib/rabbitizer/cplusplus/include"
    "${DKR_WIN95_N64RECOMP}/lib/fmt/include"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty")
target_compile_definitions(win95liverecomp PUBLIC FMT_HEADER_ONLY=1)
# Code tiers : ses avertissements ne sont pas les nôtres et noieraient les nôtres.
target_compile_options(win95liverecomp PRIVATE -w)

# --- Code recompilé (E01-S05) ------------------------------------------------
file(GLOB DKR_WIN95_RECOMPILED_C   CONFIGURE_DEPENDS
     "${DKRPORT_ROOT}/runtime-recomp/RecompiledFuncs/*.c")
file(GLOB DKR_WIN95_RECOMPILED_RSP CONFIGURE_DEPENDS
     "${DKRPORT_ROOT}/runtime-recomp/RecompiledRSP/*.cpp")
if(NOT DKR_WIN95_RECOMPILED_C)
    message(FATAL_ERROR
        "Aucune sortie de N64Recomp sous runtime-recomp/RecompiledFuncs. "
        "Lancer Prepare-DKR-Runtime puis la recompilation avant de construire le jeu.")
endif()

add_library(win95recompiled STATIC
    ${DKR_WIN95_RECOMPILED_C} ${DKR_WIN95_RECOMPILED_RSP})
target_include_directories(win95recompiled PUBLIC
    "${DKRPORT_ROOT}/runtime-recomp/RecompiledFuncs"
    "${DKRPORT_ROOT}/runtime-recomp/RecompiledRSP")
target_link_libraries(win95recompiled PUBLIC win95librecomp)
target_compile_options(win95recompiled PRIVATE -w)   # code généré

# --- Le jeu (E02-S06) --------------------------------------------------------
#
# Les 17 sources que cette cible construit : celles de `DKR_GAME_SOURCES` moins
# les quatre que seul RT64 compile, puisque RT64 exige D3D12, Vulkan ou Metal.
set(DKR_WIN95_GAME_SOURCES
    audio_equalizer dkr_save_codec game_main game_registration null_renderer
    presentation_identity renderer_snapshot runtime_enhancements
    runtime_audio_controls runtime_input runtime_magic_codes runtime_platform
    runtime_quick_restart save_manager runtime_stubs runtime_telemetry
    virtual_pak)
list(TRANSFORM DKR_WIN95_GAME_SOURCES
     PREPEND "${DKRPORT_ROOT}/runtime-recomp/src/game/")
list(TRANSFORM DKR_WIN95_GAME_SOURCES APPEND ".cpp")

add_executable(DKRWin95Game ${DKR_WIN95_GAME_SOURCES})
target_include_directories(DKRWin95Game PRIVATE
    "${DKRPORT_ROOT}/runtime-recomp/src/game"
    "${DKRPORT_ROOT}/platform"
    "${DKR_WIN95_PLATFORM}"
    "${DKR_WIN95_PLATFORM}/include-shim"
    "${DKRPORT_ROOT}/include")
target_compile_definitions(DKRWin95Game PRIVATE
    NOMINMAX
    DKR_RUNTIME_HAS_RT64=0
    "DKR_RELEASE_VERSION=\"${DKR_RELEASE_VERSION}\""
    DKR_TARGET_WIN95=1)

# `--start-group` : `librecomp` et `ultramodern` se réclament mutuellement, et
# `miniz` vit dans la même archive que ce qui l'appelle. Un lieur à une passe
# laisserait des symboles non résolus selon l'ordre.
target_link_libraries(DKRWin95Game PRIVATE
    -Wl,--start-group
    win95recompiled win95librecomp win95ultramodern win95liverecomp
    win95fileio win95clock win95threading
    -Wl,--end-group)
# `win95compat` n'est pas nommée ici : elle s'ajoute d'elle-même, en tête et sous
# `--whole-archive`, par les options d'interface posées plus haut. La nommer une
# seconde fois duplique l'archive et le lieur refuse — définitions multiples.
set_target_properties(DKRWin95Game PROPERTIES
    OUTPUT_NAME "DKRR"
    SUFFIX ".EXE"                      # 8.3, pour être lançable depuis DOS
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Game)

message(STATUS "Cible Windows 95 configurée : win95compat, DKRWin95Witness, DKRWin95Game")
