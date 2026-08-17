# E01-S01 - the Windows 95 build target.
#
# Included from `runtime-recomp/CMakeLists.txt` when `DKR_RUNTIME_TARGET_WIN95` is
# active, right after the options are declared - and the CMakeLists returns
# immediately afterwards. The Windows, Linux and macOS targets therefore cross a
# single false `if()`: they are unchanged by construction, which is the guarantee
# ADR 0004 demands.
#
# Scope. E01-S01 delivered the toolchain; E01-S03 the compatibility bridge;
# E02-S01 the threading layer; E02-S02 rested `ultramodern` on it, which now
# compiles here in its entirety. Out of reach remain `librecomp` (E02-S05 for its
# file system) and the recompiled code (E01-S05): the game therefore does not link
# for this target yet.

if(NOT DKR_WIN95_TOOLCHAIN)
    message(FATAL_ERROR
        "DKR_RUNTIME_TARGET_WIN95 requires the toolchain file:\n"
        "  cmake -S runtime-recomp -B build/win95 -G Ninja \\\n"
        "        --toolchain cmake/toolchain-win95.cmake \\\n"
        "        -DDKR_RUNTIME_TARGET_WIN95=ON")
endif()

if(DKR_RUNTIME_BUILD_RT64)
    message(FATAL_ERROR
        "RT64 requires D3D12, Vulkan or Metal: none exists under Windows 95.\n"
        "Leave DKR_RUNTIME_BUILD_RT64 OFF, as it is by default.")
endif()

# --- Build type --------------------------------------------------------------
#
# CMake leaves `CMAKE_BUILD_TYPE` empty by default, which gives an unoptimised
# binary. On modern targets that is an annoyance; here it is a trap, and a silent
# one.
#
# Measured: with no type, `DKRR.EXE` is 20.6 MB; in Release, 8.5 MB. And the size
# is not the worst of it - the heart of this port is MIPS recompiled into C, whose
# per-instruction cost decides everything on a 400 MHz Pentium II (E00-S03).
# Unoptimised it would not be "slower": it would be unplayable, with nothing to
# announce it.
#
# So we choose for the caller who chose nothing, and we say so.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set: Release imposed "
                   "(unoptimised, the target is unplayable)")
endif()

set(DKR_WIN95_TOOLS    "${DKRPORT_ROOT}/tools/win95")
set(DKR_WIN95_PLATFORM "${DKRPORT_ROOT}/platform/win95")

# The import check is written in Python and has no dependency: it reads the PE's
# own import table.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

message(STATUS "Windows 95 target: ${CMAKE_C_COMPILER}")
message(STATUS "  instruction set: Pentium II, no SSE, x87 floating point")
message(STATUS "  API: _WIN32_WINNT=0x0400")

# --- The C++ subset (E01-S02) ------------------------------------------------
#
# There is no standard to restrict: GCC 13 implements all of C++20 for this
# target. What is forbidden are library facilities whose mere inclusion makes
# symbols Windows 95 does not export appear in the import table - `<thread>`,
# `<mutex>`, `<filesystem>` and their neighbours. See docs/CPP-SUBSET.md.
#
# The check is a **pre-build** step and not a post-link one: a forbidden include
# compiles perfectly, and only shows itself when loading on the target machine.
# The compiler will say nothing.
#
# It bears on the target's own sources; `ultramodern` has its own check further
# down, with a ratchet, ever since E02-S02 brought its forbidden includes down
# from nine to one.
add_custom_target(dkr_win95_cpp_subset ALL
    COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check-cpp-subset.py"
            "${DKR_WIN95_PLATFORM}"
    COMMENT "Checking the permitted C++ subset"
    VERBATIM)

# --- The compatibility bridge ------------------------------------------------
#
# The six functions GCC 13's standard library requires and that Windows 95's
# KERNEL32 does not export. See ADR 0001: without them the binary does not load,
# and Windows names the missing symbol in an error box.
add_library(win95compat STATIC
    "${DKR_WIN95_PLATFORM}/compat.c"
    "${DKR_WIN95_PLATFORM}/tick64.c"
    "${DKR_WIN95_PLATFORM}/startup.c")
target_include_directories(win95compat PUBLIC "${DKR_WIN95_PLATFORM}")
# `IsDebuggerPresent` and its kind are declared `dllimport` by windows.h;
# redefining them is precisely this file's purpose.
target_compile_options(win95compat PRIVATE -Wno-attributes)
# The subset check runs before any compilation.
add_dependencies(win95compat dkr_win95_cpp_subset)

# `--whole-archive` is mandatory, not a precaution: the archive is only consulted
# at the moment it appears on the command line, and `libwinpthread` only
# introduces its references afterwards. Without it, `libkernel32.a` - placed last
# by the compiler's specs - wins, and the symbols point at functions Windows 95
# does not have.
target_link_options(win95compat INTERFACE
    "-Wl,--whole-archive" "$<TARGET_FILE:win95compat>" "-Wl,--no-whole-archive")

# --- Threading and synchronisation layer (E02-S01) ---------------------------
#
# A library distinct from `win95compat`, and not merged into it, for a mechanical
# reason: this one is C++, and E01-S03's C witnesses link with the C compiler.
# Merging them would force those to drag libstdc++ along with no use for it.
#
# The dependency goes the other way: the layer leans on `win95compat` for the five
# critical-section functions and for the startup log.
add_library(win95threading STATIC "${DKR_WIN95_PLATFORM}/threading.cpp")
target_include_directories(win95threading PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95threading PUBLIC win95compat)
add_dependencies(win95threading dkr_win95_cpp_subset)

# --- Time base (E02-S03) -----------------------------------------------------
#
# Separate from `win95threading` because it has one dependency more - `winmm`, for
# `timeGetTime` and `timeBeginPeriod` - and there is no reason to make the
# witnesses that only need threads carry it.
add_library(win95clock STATIC "${DKR_WIN95_PLATFORM}/clock.cpp")
target_include_directories(win95clock PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95clock PUBLIC win95compat winmm)
add_dependencies(win95clock dkr_win95_cpp_subset)

# --- File writing (E02-S05) --------------------------------------------------
#
# Depends only on `win95compat`, like the threading layer: the durable-write
# sequence calls nothing but `...A` APIs that are present and implemented.
add_library(win95fileio STATIC "${DKR_WIN95_PLATFORM}/fileio.cpp")
target_include_directories(win95fileio PUBLIC "${DKR_WIN95_PLATFORM}")
target_link_libraries(win95fileio PUBLIC win95compat)
add_dependencies(win95fileio dkr_win95_cpp_subset)

# --- ultramodern (E02-S02) ---------------------------------------------------
#
# Patch 0015 routes `ultramodern`'s five primitives - `thread`, `mutex`,
# `condition_variable`, `lock_guard`, `unique_lock` - through a seam that the
# target fills here with E02-S01's layer.
#
# The include path is `win95/threading.hpp` and not `threading.hpp`, with
# `platform` on the search path: `ultramodern` has a file named `threading.hpp` of
# its own, and a quoted include consults the including file's directory first. The
# short name therefore made it fall back on itself, with hundreds of errors about
# missing types as the only symptom.
#
# **This is not loadable under Windows 95 yet**, and that must be said:
# `ultramodern.hpp` still includes `<filesystem>`, which on its own requires
# thirteen symbols the system lacks. That is E02-S05's work. This target
# establishes compilation and the substitution of the primitives, not loading.
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

# The C++ subset check now bears on `ultramodern` too: it would have taught
# nothing while nine forbidden includes remained there; one is left, and it is
# accounted for.
#
# The `--max 1` ratchet has been removed: `ultramodern` no longer has a single
# forbidden include. One remained, `<filesystem>`, and it disappeared not by
# rewriting code but because measurement showed the rule was wrong - the include
# and the `std::filesystem::path` type cost nothing under Windows 95, only the
# operations cost. See docs/research/win95-filesystem.md.
#
# It was the ratchet that flagged it, by saying its tolerance no longer had any
# reason to exist. A threshold one never loosens stops protecting.
add_custom_target(dkr_win95_cpp_subset_ultramodern ALL
    COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check-cpp-subset.py"
            "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern"
    COMMENT "Checking the C++ subset: ultramodern"
    VERBATIM)

# --- librecomp (E01-S05) -----------------------------------------------------
#
# Patch 0016 lifts `librecomp`'s two 64-bit assumptions: `HookDefinition`'s hash,
# which packed three values into a `size_t` while requiring it to be 64 bits, and
# `patch_func`'s trampoline, which knew only x86_64 and ARM64. The 26 translation
# units have compiled since.
#
# `miniz_export.h` comes from the shim: it is a header that miniz's CMake
# manufactures at configuration time, and this target does not build miniz through
# its CMake.
#
# **This does not make the game loadable.** `librecomp` still carries
# `<filesystem>` in abundance - that is E02-S05's work - and this target
# establishes compilation, not loading.
file(GLOB DKR_WIN95_LIBRECOMP_SOURCES
     "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/src/*.cpp")

# The two third-party libraries `librecomp` requires at link time, and which this
# target's build left aside as long as it only established compilation.
#
#   o1heap  the N64 heap allocator, used by `heap.cpp` - core, indispensable
#   miniz   ZIP archive reading, used only by the mod system. Four files and not
#           one: `miniz.c` alone does not carry the zip API, which lives in
#           `miniz_zip.c` - the object was only 11 KB and the linker kept asking
#           for `mz_zip_reader_*`.
#
# They are compiled as C here, and not by their own CMake: miniz's would
# manufacture `miniz_export.h` at configuration time, hence the shim.
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
# Patch 0018 routes the file operations at `librecomp`'s core through a seam,
# which the target fills here with E02-S05's layer.
#
# The `std::filesystem::path` type is **not** replaced, and that is measured:
# under Windows 95 it costs nothing and works. Only the operations cost.
#
# Patch 0020 does the same for synchronisation. The difference between the two is
# worth noting: for files, only the *operations* are diverted and the type stays;
# here it is the types themselves that do not pass, the inclusion of `<mutex>`
# being enough to prevent loading. The name therefore changes at every
# declaration.
target_compile_definitions(win95librecomp PRIVATE
    NOMINMAX
    "LIBRECOMP_PLATFORM_FILEIO_HEADER=\"win95/fileio.hpp\""
    "LIBRECOMP_PLATFORM_FILEIO_NS=dkr::fs"
    "LIBRECOMP_PLATFORM_SYNC_HEADER=\"win95/threading.hpp\""
    "LIBRECOMP_PLATFORM_SYNC_NS=dkr::win95"
    DKR_TARGET_WIN95=1)
target_link_libraries(win95librecomp PUBLIC win95ultramodern)
add_dependencies(win95librecomp dkr_win95_cpp_subset)

# --- Instruction-set verification --------------------------------------------
#
# A mandatory step after linking, and not an optional tool: it is the only proof
# that nothing incompatible slipped in from the CRT or the standard library, which
# `-mno-sse` on our own sources does not cover.
function(dkr_win95_verify target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${DKR_WIN95_TOOLS}/check-instruction-set.sh" "$<TARGET_FILE:${target}>"
        COMMENT "Verifying the Pentium II instruction set: ${target}"
        VERBATIM)

    # The second guard rail, the one on symbols (E01-S04). Under Windows 95 the
    # loader resolves every import at startup: a missing symbol prevents the
    # process from starting, even if the function is never called. Nothing flags
    # it at link time, and finding out costs a round trip to the test machine.
    #
    # `--objects` on the build directory allows the offending object to be named:
    # the PE's import table does not keep that information, it is lost at link
    # time.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${DKR_WIN95_TOOLS}/check_imports.py"
                --objects "${CMAKE_BINARY_DIR}" "$<TARGET_FILE:${target}>"
        COMMENT "Verifying the imports against Windows 95's exports: ${target}"
        VERBATIM)
endfunction()

# --- Witness -----------------------------------------------------------------
#
# Two threads, a critical section, an event, RTTI and exceptions: the exact shape
# of the execution model `ultramodern` needs. This is E00-S02's T3b witness, which
# prints 1000/1000 on the test machine.
set(DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/t3b.cpp")

# Second witness: E01-S03's, which exercises the whole layer - startup, log,
# exception filter, version check, the six missing APIs, the 64-bit clock and two
# threads in contention.
add_executable(DKRWin95Platform "${DKR_WIN95_PLATFORM}/witness.c")
target_link_libraries(DKRWin95Platform PRIVATE win95compat user32)
set_target_properties(DKRWin95Platform PROPERTIES
    OUTPUT_NAME "PLATFORM"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Platform)

# Third witness: the threading layer's trial (E02-S01). It is **exactly the same
# source** as the suite run on the host - two separate files would end up
# diverging, and it is precisely on the target that the differences matter.
#
#   scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE
#   THREADS.EXE                    the suite
#   THREADS.EXE --stress 600       the ten-minute endurance run
add_executable(DKRWin95Threads "${DKR_WIN95_PLATFORM}/tests/test_threading.cpp")
target_link_libraries(DKRWin95Threads PRIVATE win95threading)
set_target_properties(DKRWin95Threads PROPERTIES
    OUTPUT_NAME "THREADS"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Threads)

# Fourth witness: E02-S02's C++ bridge, exercised in the exact forms
# `ultramodern` uses - variadic construction, immediate detach, lock_guard in both
# its forms, and the condition variable with its four real uses.
add_executable(DKRWin95ThreadsCpp
    "${DKR_WIN95_PLATFORM}/tests/test_threading_cpp.cpp")
target_link_libraries(DKRWin95ThreadsCpp PRIVATE win95threading)
set_target_properties(DKRWin95ThreadsCpp PROPERTIES
    OUTPUT_NAME "THRCPP"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95ThreadsCpp)

# Fifth witness: E02-S03's time base. The `--long` mode measures the drift over a
# chosen duration - a base that drifts slowly breaks nothing visible and falsifies
# every timing, so the check must bear on a duration and not on an instant.
add_executable(DKRWin95Clock "${DKR_WIN95_PLATFORM}/tests/test_clock.cpp")
target_link_libraries(DKRWin95Clock PRIVATE win95clock)
set_target_properties(DKRWin95Clock PROPERTIES
    OUTPUT_NAME "CLOCKT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Clock)

# Sixth witness: E02-S05's durable write. What it puts to the test is not that the
# file gets written - it is what is left on the disk when the write is interrupted,
# by building by hand the intermediate states the sequence passes through.
add_executable(DKRWin95FileIO "${DKR_WIN95_PLATFORM}/tests/test_fileio.cpp")
target_link_libraries(DKRWin95FileIO PRIVATE win95fileio)
set_target_properties(DKRWin95FileIO PROPERTIES
    OUTPUT_NAME "FILEIOT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95FileIO)

# Seventh witness: the file-operations seam. What it puts to the test is the
# **equivalence** of the two branches - `std::filesystem` on the host, the `...A`
# APIs on the target - because a seam whose two sides differ is worse than no seam
# at all.
add_executable(DKRWin95FileIOSeam
    "${DKR_WIN95_PLATFORM}/tests/test_fileio_seam.cpp")
target_link_libraries(DKRWin95FileIOSeam PRIVATE win95fileio)
target_compile_definitions(DKRWin95FileIOSeam PRIVATE DKR_TARGET_WIN95=1)
set_target_properties(DKRWin95FileIOSeam PROPERTIES
    OUTPUT_NAME "FSSEAM"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95FileIOSeam)

# --- Save suites, compiled for the target (E02-S05) --------------------------
#
# These two were built by hand, and they are precisely the ones that should not
# have been left that way: they are what closes E02-S05's acceptance criteria, and
# a report one does not know how to reproduce proves nothing durable.
#
# They carry `save_manager.cpp` and `dkr_save_codec.cpp` - game code, not platform
# layer - hence the include paths to `src/game`. They write their verdict to the
# error output, which DOS cannot redirect: on the machine, one launches them from a
# batch file and reads the window. See docs/TEST-ENVIRONMENT.md.
add_executable(DKRWin95SaveManager
    "${DKRPORT_ROOT}/runtime-recomp/tests/save_manager_tests.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/save_manager.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/dkr_save_codec.cpp")
target_include_directories(DKRWin95SaveManager PRIVATE
    "${DKRPORT_ROOT}/runtime-recomp/src/game"
    "${DKRPORT_ROOT}/platform"
    "${DKR_WIN95_PLATFORM}"
    "${DKR_WIN95_PLATFORM}/include-shim")
target_compile_definitions(DKRWin95SaveManager PRIVATE NOMINMAX DKR_TARGET_WIN95=1)
# `-UNDEBUG`: these suites are **made of assertions**, and `Release` defines
# `NDEBUG`, which erases them. It is not merely that they would check nothing any
# more - they put their calls *inside* the assertions:
#
#     assert(dkr::runtime::saves::backup_adventure(backup, error));
#
# Under NDEBUG the call disappears with the assertion. The suite printed its four
# phases then crashed on the machine, the state never having been built. A trial
# suite that passes by testing nothing is the worst of results; here it did not
# even pass, which is what made it visible.
target_compile_options(DKRWin95SaveManager PRIVATE -UNDEBUG)
target_link_libraries(DKRWin95SaveManager PRIVATE win95fileio win95threading)
set_target_properties(DKRWin95SaveManager PROPERTIES
    OUTPUT_NAME "SAVEMGR"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95SaveManager)

add_executable(DKRWin95SaveCodec
    "${DKRPORT_ROOT}/runtime-recomp/tests/dkr_save_codec_tests.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/dkr_save_codec.cpp")
target_include_directories(DKRWin95SaveCodec PRIVATE
    "${DKRPORT_ROOT}/runtime-recomp/src/game")
target_compile_definitions(DKRWin95SaveCodec PRIVATE NOMINMAX DKR_TARGET_WIN95=1)
# Same reason: `dkr_save_codec_tests` is built on `assert` too.
target_compile_options(DKRWin95SaveCodec PRIVATE -UNDEBUG)
target_link_libraries(DKRWin95SaveCodec PRIVATE win95compat)
set_target_properties(DKRWin95SaveCodec PROPERTIES
    OUTPUT_NAME "SAVECDC"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95SaveCodec)

# --- Save interchange (E02-S05) ----------------------------------------------
#
# The producing half of the interchange trial: it writes a non-trivial adventure
# save, and the same source is compiled for both targets. The consuming half is
# `SAVECDC.EXE`, which takes a file as an argument, decodes it, re-encodes it and
# demands equality **byte for byte**.
add_executable(DKRWin95SaveInterchange
    "${DKR_WIN95_TOOLS}/witnesses/save_interchange.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/dkr_save_codec.cpp")
target_include_directories(DKRWin95SaveInterchange PRIVATE
    "${DKRPORT_ROOT}/runtime-recomp/src/game")
target_link_libraries(DKRWin95SaveInterchange PRIVATE win95compat)
set_target_properties(DKRWin95SaveInterchange PROPERTIES
    OUTPUT_NAME "SAVEGEN"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95SaveInterchange)

# --- Render backend interface (E04-S01) --------------------------------------
#
# A design ticket, whose deliverable is a contract - `backend.h`. Built here are
# the empty implementation, which establishes that the interface compiles and links
# without a real backend, and the layout check, which verifies at compile time the
# two promises everything else depends on: the vertex has `GrVertex`'s layout, and
# the state block is comparable by `memcmp`.
add_library(win95renderbackend STATIC
    "${DKRPORT_ROOT}/platform/render/backend_null.c"
    "${DKRPORT_ROOT}/platform/render/backend_layout_check.c")
target_include_directories(win95renderbackend PUBLIC
    "${DKRPORT_ROOT}/platform/render")

# --- Clipping and the scissor window (E04-S05) -------------------------------
#
# 3dfx cards do not clip. Their scissor window is enough at the sides, but a
# triangle with a vertex **behind the camera** cannot be rejected at fragment
# level: its projection is absurd, and the vertex comes out the other side of the
# screen. Only the near plane therefore demands real clipping, and that is what
# makes this stage affordable.
add_library(win95clip STATIC "${DKRPORT_ROOT}/platform/render/clip.c")
target_link_libraries(win95clip PUBLIC win95transform)

add_executable(DKRWin95Clip "${DKRPORT_ROOT}/platform/render/tests/test_clip.c")
target_include_directories(DKRWin95Clip PRIVATE "${DKRPORT_ROOT}/platform")
target_link_libraries(DKRWin95Clip PRIVATE win95clip)
set_target_properties(DKRWin95Clip PROPERTIES
    OUTPUT_NAME "CLIP"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Clip)

# --- Vertex transformation (E04-S03) ----------------------------------------
#
# On the N64 it is the RSP that transforms; here it falls to the processor, as on
# any 3dfx card. **It is the heaviest graphics computation in the port**, and its
# cost is measured on the machine rather than assumed.
add_library(win95transform STATIC "${DKRPORT_ROOT}/platform/render/transform.c")
target_link_libraries(win95transform PUBLIC win95renderbackend)

add_executable(DKRWin95Transform
    "${DKRPORT_ROOT}/platform/render/tests/test_transform.c")
target_include_directories(DKRWin95Transform PRIVATE "${DKRPORT_ROOT}/platform")
target_link_libraries(DKRWin95Transform PRIVATE win95transform)
set_target_properties(DKRWin95Transform PROPERTIES
    OUTPUT_NAME "TRANSFRM"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Transform)

# --- F3DDKR display-list decoder (E04-S02) -----------------------------------
#
# Extracted from `f3ddkr_rt64.cpp`, whose decoding logic belongs to Rare's
# microcode and has nothing to do with RT64. What had to survive the extraction is
# the **range validation**: every read is bounded, and an invalid range produces a
# contained error rather than addressing the host's memory. It protects against a
# modified ROM as much as against a bug in the port - the latter being the more
# likely.
add_library(win95f3ddkr STATIC
    "${DKRPORT_ROOT}/platform/render/f3ddkr.c"
    "${DKRPORT_ROOT}/platform/render/texture.c")
# The chain: the decoder now emits, so it depends on clipping, which itself
# depends on the transformation. The decoder also now translates the RDP state and
# hands it to the backend, so the dependency is real and no longer optional.
target_link_libraries(win95f3ddkr PUBLIC win95clip win95rdpstate)

# The suite injects **deliberately corrupted** display lists. That is what makes
# it possible without a ROM: a corrupted list is written, a real one is captured.
add_executable(DKRWin95F3DDKR
    "${DKRPORT_ROOT}/platform/render/tests/test_f3ddkr.c")
target_include_directories(DKRWin95F3DDKR PRIVATE "${DKRPORT_ROOT}/platform")
target_link_libraries(DKRWin95F3DDKR PRIVATE win95f3ddkr)
set_target_properties(DKRWin95F3DDKR PROPERTIES
    OUTPUT_NAME "F3DDKR"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95F3DDKR)

# --- RDP state decoding (E04-S06) --------------------------------------------
#
# The RDP's combiner is a programmable unit; Glide's is fixed, and the translation
# is the hard point of all of E05. What makes the problem tractable is that **DKR
# declares its settings in static tables**: the set in use is bounded. The decoding
# produces a comparable canonical form, and E05-S03 will match a Glide setup to it
# by a simple lookup.
add_library(win95rdpstate STATIC "${DKRPORT_ROOT}/platform/render/rdp_state.c")
target_link_libraries(win95rdpstate PUBLIC win95renderbackend)

# The test vectors come from the decomp's headers, resolved by
# `tools/win95/gen_combiner_vectors.py` - not from a hand transcription, which
# would go wrong in silence and would then let a wrong decoder pass.
add_executable(DKRWin95RdpState
    "${DKRPORT_ROOT}/platform/render/tests/test_rdp_state.c")
target_include_directories(DKRWin95RdpState PRIVATE
    "${DKRPORT_ROOT}/platform" "${DKRPORT_ROOT}/platform/render/tests")
# `win95combiner` as well, and the reason is worth a line: the collision check
# needs an arbiter for "do these two configurations compute different things".
# Deciding that from the normalised fields would be circular, normalisation being
# what is under test. `dkr_combiner_eval` is not: it is what the image depends on.
# The library is declared further down; CMake resolves the reference either way.
target_link_libraries(DKRWin95RdpState PRIVATE win95rdpstate win95combiner)
set_target_properties(DKRWin95RdpState PROPERTIES
    OUTPUT_NAME "RDPSTATE"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95RdpState)

# --- Reference software rasteriser (E04-S08) ---------------------------------
#
# The oracle. When an image comes out wrong under Glide, one will need to know
# whether the error comes from the decoder or from the backend; with a second
# backend implementing the same interface, the question is settled in one run.
#
# It is allowed to be slow and not allowed to be complicated: its entire value
# lies in the trust placed in it as a reference, and an optimised rasteriser is a
# rasteriser whose own correctness must in turn be checked.
add_library(win95software STATIC "${DKRPORT_ROOT}/platform/render/software.c")
target_link_libraries(win95software PUBLIC win95renderbackend)

add_executable(DKRWin95SoftRaster
    "${DKRPORT_ROOT}/platform/render/tests/test_software.c")
target_include_directories(DKRWin95SoftRaster PRIVATE "${DKRPORT_ROOT}/platform")
target_link_libraries(DKRWin95SoftRaster PRIVATE win95software)
set_target_properties(DKRWin95SoftRaster PROPERTIES
    OUTPUT_NAME "SOFTRAS"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95SoftRaster)

# --- The whole chain, on a synthetic scene (E04) -----------------------------
#
# Five modules fit together: decoder, transformation, clipping, interface,
# rasteriser. What this target establishes is not that the rendering is *right* -
# the game will be needed for that - but that **the chain is continuous**: a
# command written into RDRAM comes out as pixels.
add_executable(DKRWin95Pipeline
    "${DKRPORT_ROOT}/platform/render/tests/test_pipeline.c")
target_include_directories(DKRWin95Pipeline PRIVATE
    "${DKRPORT_ROOT}/platform"
    "${DKRPORT_ROOT}/platform/render/tests")
target_link_libraries(DKRWin95Pipeline PRIVATE win95f3ddkr win95software)
set_target_properties(DKRWin95Pipeline PROPERTIES
    OUTPUT_NAME "PIPELINE"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Pipeline)

# --- Glide bring-up (E05-S01) ------------------------------------------------
#
# The layer that opens the 3dfx card, then the shim presenting it as one of
# E04-S01's `dkr_render_backend`. The two are separate by design: opening the card
# and programming its state registers are two distinct kinds of knowledge, and the
# first half must stay usable for a plain diagnostic.
#
# E05-S03 - the combiner oracle and the correspondence table. The table is
# generated by tools/win95/gen_combiner_table.py from the game source's own
# definitions: thirty-three quadruples copied by hand would invite a typo, and a
# mistake here would only show in the image.
add_library(win95combiner STATIC "${DKRPORT_ROOT}/platform/render/combiner.c")
target_include_directories(win95combiner PUBLIC
    "${DKRPORT_ROOT}/platform" "${DKRPORT_ROOT}/platform/render")
target_link_libraries(win95combiner PUBLIC win95rdpstate)

add_executable(DKRWin95Combiner
    "${DKRPORT_ROOT}/platform/render/tests/test_combiner.c")
target_link_libraries(DKRWin95Combiner PRIVATE win95combiner)
set_target_properties(DKRWin95Combiner PROPERTIES
    OUTPUT_NAME "COMBTEST"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Combiner)

add_library(win95tmu STATIC "${DKRPORT_ROOT}/platform/render/tmu.c")
target_include_directories(win95tmu PUBLIC "${DKRPORT_ROOT}/platform")
target_link_libraries(win95tmu PUBLIC win95compat)

# The allocator is tried entirely on the host: the missing ROM forbids checking it
# in the game, and the card says nothing of what it receives - neither
# grTexDownloadMipMap nor grTexSource returns an error code.
add_executable(DKRWin95Tmu
    "${DKRPORT_ROOT}/platform/render/tests/test_tmu.c")
target_link_libraries(DKRWin95Tmu PRIVATE win95tmu)
set_target_properties(DKRWin95Tmu PROPERTIES
    OUTPUT_NAME "TMUTEST"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Tmu)

add_library(win95glide STATIC
    "${DKRPORT_ROOT}/platform/render/glide.c"
    "${DKRPORT_ROOT}/platform/render/glide_backend.c")
target_include_directories(win95glide PUBLIC
    "${DKRPORT_ROOT}/platform"
    "${DKR_WIN95_PLATFORM}"
    "${DKR_WIN95_PLATFORM}/include-shim")
target_link_libraries(win95glide PUBLIC win95compat win95tmu win95combiner)
# PUBLIC: `backend.h` only declares `dkr_render_backend_glide` on this target, so
# that a host compiling the oracle cannot call it by mistake. The witness must
# therefore see the definition, not merely the library.
target_compile_definitions(win95glide PUBLIC DKR_TARGET_WIN95=1)

# The witness exercises the layer in the order the engine will use it, and measures
# the frame rate over a hundred frames. Its "crash" mode tries the restoration of
# the display after an abnormal stop - on a passthrough Voodoo, a missed close
# leaves the screen black until the machine is rebooted.
add_executable(DKRWin95GlideProbe
    "${DKR_WIN95_TOOLS}/witnesses/glide_backend_probe.c")
target_link_libraries(DKRWin95GlideProbe PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95GlideProbe PROPERTIES
    OUTPUT_NAME "GLIDEBK"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95GlideProbe)

# Replays the render state the game produces and **reads the frame buffer back**.
# Six measurements cleared the decoder; what remains is what the card does with
# the state, and that cannot be seen from the game -- there you observe what you
# send, never what comes out. The probe degrades the state step by step: the
# first case that paints names the culprit.
add_executable(DKRWin95StateProbe
    "${DKR_WIN95_TOOLS}/witnesses/state_probe.c")
target_link_libraries(DKRWin95StateProbe PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95StateProbe PROPERTIES
    OUTPUT_NAME "TEST"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95StateProbe)

# Glide 2.x's constants are written from memory: there is no `glide.h` on this
# machine. A wrong value causes no error - Glide does not validate its
# enumerations - but programs a neighbouring register, and the image's deviation is
# then blamed on the display-list decoder.
#
# This witness exercises each mode in isolation and **reads the frame buffer
# back**, which turns an assumption into a dated fact. It has already caught one
# fault: the w buffer's comparison direction. See
# `docs/research/win95-glide-states.md`.
add_executable(DKRWin95GlideState
    "${DKR_WIN95_TOOLS}/witnesses/glide_state_probe.c")
target_link_libraries(DKRWin95GlideState PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95GlideState PROPERTIES
    OUTPUT_NAME "GLSTATE"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95GlideState)

# The texture chain is silent end to end: neither the download nor the binding
# returns an error code. This witness checks it in the only possible way - by
# reading the image back - with a checkerboard whose four corners carry distinct
# colours, so that swapping s and t shows.
add_executable(DKRWin95GlideTexture
    "${DKR_WIN95_TOOLS}/witnesses/glide_texture_probe.c")
target_link_libraries(DKRWin95GlideTexture PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95GlideTexture PROPERTIES
    OUTPUT_NAME "GLTEX"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95GlideTexture)

# E05-S03 - each configuration's deviation, measured on the card. The ticket warns
# against the temptation to handle the configurations "until it looks about right":
# this witness is the answer to that warning.
add_executable(DKRWin95CombinerProbe
    "${DKR_WIN95_TOOLS}/witnesses/combiner_probe.c")
target_link_libraries(DKRWin95CombinerProbe PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95CombinerProbe PROPERTIES
    OUTPUT_NAME "COMBINER"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95CombinerProbe)

# E05-S04 - the chaining of the two TMUs. grTexCombine's values are measured, not
# assumed: it is the third time in this port that a Glide enumeration written from
# memory turns out to be wrong.
add_executable(DKRWin95Multitex
    "${DKR_WIN95_TOOLS}/witnesses/multitex_probe.c")
target_link_libraries(DKRWin95Multitex PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95Multitex PROPERTIES
    OUTPUT_NAME "MULTITEX"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Multitex)

# E05-S05 - deciding Z against W by measurement, on the worst case rather than on
# reputation. The ticket names the risk: depth fighting shows at a distance and
# must be sought actively.
add_executable(DKRWin95Depth
    "${DKR_WIN95_TOOLS}/witnesses/depth_probe.c")
target_link_libraries(DKRWin95Depth PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95Depth PROPERTIES
    OUTPUT_NAME "DEPTH"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Depth)

# E05-S06 - fog, DKR's most frequent render mode. The witness also measures what
# the ticket did not ask for: the vertex alpha serves the fog *and* the
# transparency, and the two are therefore coupled.
add_executable(DKRWin95Fog
    "${DKR_WIN95_TOOLS}/witnesses/fog_probe.c")
target_link_libraries(DKRWin95Fog PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95Fog PROPERTIES
    OUTPUT_NAME "FOG"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Fog)

# E05-S07 - 2D rectangles. The half-texel offset is determined by experiment, not
# by reasoning: the ticket demands it, and it is right.
add_executable(DKRWin95Rect
    "${DKR_WIN95_TOOLS}/witnesses/rect_probe.c")
target_link_libraries(DKRWin95Rect PRIVATE win95glide win95clip win95clock winmm)
set_target_properties(DKRWin95Rect PROPERTIES
    OUTPUT_NAME "RECT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Rect)

# E05-S08 - wrapping and clamping for every size, and the cost of filtering. Two of
# the ticket's premises fall before the measurement: DKR uses neither mipmaps nor
# mirroring, which the source establishes unambiguously.
add_executable(DKRWin95Sampling
    "${DKR_WIN95_TOOLS}/witnesses/sampling_probe.c")
target_link_libraries(DKRWin95Sampling PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95Sampling PROPERTIES
    OUTPUT_NAME "SAMPLING"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Sampling)

# The measurement that decided the allocator's design: granularity, addressable
# space, the real cost of each size and each format.
add_executable(DKRWin95TmuProbe
    "${DKR_WIN95_TOOLS}/witnesses/tmu_probe.c")
target_link_libraries(DKRWin95TmuProbe PRIVATE win95glide win95clock winmm)
set_target_properties(DKRWin95TmuProbe PROPERTIES
    OUTPUT_NAME "TMU"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95TmuProbe)

# On a machine without a board, `glide2x.dll` displays its own modal box during the
# LoadLibrary. This witness served to establish that no registry signal allows one
# to get ahead of it: the reports with and without a board are identical. It is
# kept because the conclusion is a negative one, and therefore not checkable by
# reading the code alone.
add_executable(DKRWin95GlideRegistry
    "${DKR_WIN95_TOOLS}/witnesses/glide_registry_probe.c")
target_link_libraries(DKRWin95GlideRegistry PRIVATE win95compat advapi32)
set_target_properties(DKRWin95GlideRegistry PROPERTIES
    OUTPUT_NAME "GLREG"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95GlideRegistry)

# E09-S02 - the oracle finally confronted with the hardware. The same synthetic
# scene passes through the reference rasteriser and then the Voodoo, and the two
# images are compared pixel by pixel. That is what reading the frame buffer back
# made possible: without it, the oracle had nothing to compare against.
add_executable(DKRWin95Compare
    "${DKRPORT_ROOT}/platform/render/tests/test_compare.c")
target_link_libraries(DKRWin95Compare PRIVATE
    win95glide win95software win95f3ddkr win95clock winmm)
target_include_directories(DKRWin95Compare PRIVATE
    "${DKRPORT_ROOT}/platform/render/tests")
set_target_properties(DKRWin95Compare PROPERTIES
    OUTPUT_NAME "COMPARE"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Compare)

# --- Power-cut probe (E02-S05) -----------------------------------------------
#
# What the durable-write sequence promises is not "the last write is never lost"
# but "a valid save is never lost". On an emulated machine, the real cut is within
# reach: `kill -9` on the emulator carries off the guest's disk cache exactly as a
# pulled plug would.
add_executable(DKRWin95PowerCut
    "${DKR_WIN95_TOOLS}/witnesses/power_cut_probe.cpp")
target_link_libraries(DKRWin95PowerCut PRIVATE win95fileio)
set_target_properties(DKRWin95PowerCut PROPERTIES
    OUTPUT_NAME "PWRCUT"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95PowerCut)

# --- Disk-full probe (E02-S05) -----------------------------------------------
#
# The trial suite checks that the error codes are distinct and carry a text -
# necessary, not sufficient. Nothing proved that a genuinely full medium returns
# `NO_SPACE` rather than `IO`. This probe asks it, on a 1.44 MB floppy filled in
# advance: the transfer disk has half a gigabyte free, which makes the exercise
# impractical by that route.
add_executable(DKRWin95DiskFull
    "${DKR_WIN95_TOOLS}/witnesses/disk_full_probe.cpp")
target_link_libraries(DKRWin95DiskFull PRIVATE win95fileio)
set_target_properties(DKRWin95DiskFull PROPERTIES
    OUTPUT_NAME "DSKFULL"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95DiskFull)

# --- Probe for streams opened on a `path` (E02-S05) --------------------------
#
# It served once, to establish that `std::ofstream(path)` fails on this target -
# MinGW then goes through `_wfopen`, which Windows 9x stubs out. It is kept and
# built because it is the only executable proof of that claim, and because the
# corresponding rule in the subset checker only makes sense as long as the
# measurement that grounds it stays replayable.
add_executable(DKRWin95WideStreamProbe
    "${DKR_WIN95_TOOLS}/witnesses/wide_stream_probe.cpp")
# `win95compat` is indispensable here as everywhere: `<fstream>` alone requires
# `_fstat64`, which Windows 95's MSVCRT does not export. Without it the probe would
# not load - which the import check said straight away.
target_link_libraries(DKRWin95WideStreamProbe PRIVATE win95compat)
set_target_properties(DKRWin95WideStreamProbe PROPERTIES
    OUTPUT_NAME "WPROBE"
    SUFFIX ".EXE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95WideStreamProbe)

# The tests that run on the host. Two suites, for two reasons:
#
#   Tick64     (E01-S03) GetTickCount's wraparound is a pure function, and waiting
#              49.7 days is not a test protocol.
#   Threading  (E02-S01) the same source as THREADS.EXE, on the POSIX vehicle.
#              Passing here proves nothing about the target - which is why the
#              target binary is run on the machine too - but shortens the
#              development cycle from several minutes to a second.
#
# Both run with the host's compiler and not the target's.
enable_testing()
add_test(NAME DKRWin95Combiner2
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" combiner)
add_test(NAME DKRWin95Tmu2
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" tmu)
add_test(NAME DKRWin95Tick64
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" tick64)
add_test(NAME DKRWin95Threading
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" threading)
add_test(NAME DKRWin95Clock
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" clock)
add_test(NAME DKRWin95FileIO
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" fileio)
# `saves` (E02-S05) builds `save_manager` twice: the modern-target branch, then the
# Windows 95 one on the POSIX backends. Passing here proves nothing about the
# machine - SAVEMGR.EXE is run there separately - but this is the arrangement that
# caught `create_directories`'s contract discrepancy.
add_test(NAME DKRWin95Saves
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" saves)

# `render` (E04-S08) builds the reference rasteriser with the host's compiler and
# runs it. The same binary built for the target runs on the machine, and the two
# render the **same bytes** - verified. That is what allows an image produced here
# to be compared against one produced there.
add_test(NAME DKRWin95Render
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" render)
add_test(NAME DKRWin95RdpState
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" rdp)
add_test(NAME DKRWin95Texture
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" texture)
add_test(NAME DKRWin95F3DDKR
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" f3ddkr)
add_test(NAME DKRWin95Transform
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" transform)
add_test(NAME DKRWin95Clip
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" clip)
add_test(NAME DKRWin95Pipeline
         COMMAND "${DKR_WIN95_PLATFORM}/tests/run-tests.sh" pipeline)

# The verifier's own trial. With this option, a translation unit is added and
# compiled with SSE: the post-link check must then fail the build. It is the only
# way to tell a working verifier from a broken one, both keeping quiet in the same
# way.
#
#   cmake ... -DDKR_WIN95_SELFTEST_SSE=ON && cmake --build ...   # must fail
option(DKR_WIN95_SELFTEST_SSE "Inject SSE to try the verifier" OFF)
if(DKR_WIN95_SELFTEST_SSE)
    list(APPEND DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/sse_canary.c")
    set_source_files_properties("${DKR_WIN95_TOOLS}/witnesses/sse_canary.c"
        PROPERTIES COMPILE_OPTIONS "-msse;-mfpmath=sse")
    message(WARNING "DKR_WIN95_SELFTEST_SSE active: the build MUST fail the post-link check")
endif()

# The import check's own trial, symmetric to the previous one. This translation
# unit imports `GetTickCount64`, which is from Vista: the post-link check must fail
# the build, naming it, and naming the offending object.
#
#   cmake ... -DDKR_WIN95_SELFTEST_IMPORT=ON && cmake --build ...   # must fail
option(DKR_WIN95_SELFTEST_IMPORT "Import a Vista API to try the check" OFF)
if(DKR_WIN95_SELFTEST_IMPORT)
    list(APPEND DKR_WIN95_WITNESS_SOURCES "${DKR_WIN95_TOOLS}/witnesses/import_canary.c")
    message(WARNING "DKR_WIN95_SELFTEST_IMPORT active: the build MUST fail the import check")
endif()

add_executable(DKRWin95Witness ${DKR_WIN95_WITNESS_SOURCES})
target_link_libraries(DKRWin95Witness PRIVATE win95compat user32)
set_target_properties(DKRWin95Witness PROPERTIES
    OUTPUT_NAME "WITNESS"
    SUFFIX ".EXE"                      # 8.3, so it can be launched from DOS
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Witness)

# --- Live recompiler (E02-S06) -----------------------------------------------
#
# `librecomp` calls it from `initialize_mods`, and the mod system is compiled for
# this target even though it will not work there: its symbols must therefore be
# resolved, or nothing links.
#
# It is a JIT - it writes machine code into a page allocated at run time. `sljit`
# detects the absence of SSE2 at startup and falls back on x87, which makes it
# viable on a Pentium II. The instruction-set check confirms it: its SSE2 tables
# are **data**, never executed here.
#
# `fmt` is used in header-only mode: it does not need to be built, and that avoids
# one more library.
set(DKR_WIN95_N64RECOMP "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp")

# `GLOB_RECURSE` and not `GLOB`: rabbitizer's sources are spread over several
# levels, and a fixed-depth pattern left some behind - the linker then asked for
# `RabbitizerInstruction_getRaw` and thirty or so others.
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
# Third-party code: its warnings are not ours and would drown ours.
target_compile_options(win95liverecomp PRIVATE -w)

# --- Recompiled code (E01-S05) -----------------------------------------------
file(GLOB DKR_WIN95_RECOMPILED_C   CONFIGURE_DEPENDS
     "${DKRPORT_ROOT}/runtime-recomp/RecompiledFuncs/*.c")
file(GLOB DKR_WIN95_RECOMPILED_RSP CONFIGURE_DEPENDS
     "${DKRPORT_ROOT}/runtime-recomp/RecompiledRSP/*.cpp")
if(NOT DKR_WIN95_RECOMPILED_C)
    message(FATAL_ERROR
        "No N64Recomp output under runtime-recomp/RecompiledFuncs. "
        "Run Prepare-DKR-Runtime then the recompilation before building the game.")
endif()

add_library(win95recompiled STATIC
    ${DKR_WIN95_RECOMPILED_C} ${DKR_WIN95_RECOMPILED_RSP})
target_include_directories(win95recompiled PUBLIC
    "${DKRPORT_ROOT}/runtime-recomp/RecompiledFuncs"
    "${DKRPORT_ROOT}/runtime-recomp/RecompiledRSP")
target_link_libraries(win95recompiled PUBLIC win95librecomp)
target_compile_options(win95recompiled PRIVATE -w)   # generated code

# --- The game (E02-S06) ------------------------------------------------------
#
# The 17 sources this target builds: those of `DKR_GAME_SOURCES` less the four that
# only RT64 compiles, since RT64 requires D3D12, Vulkan or Metal.
set(DKR_WIN95_GAME_SOURCES
    audio_equalizer dkr_save_codec game_main game_registration
    glide_renderer null_renderer
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

# `--start-group`: `librecomp` and `ultramodern` require each other, and `miniz`
# lives in the same archive as what calls it. A single-pass linker would leave
# unresolved symbols depending on the order.
target_link_libraries(DKRWin95Game PRIVATE
    -Wl,--start-group
    win95recompiled win95librecomp win95ultramodern win95liverecomp
    win95fileio win95clock win95threading
    -Wl,--end-group
    # The render chain. `win95f3ddkr` pulls in clipping and the transformation;
    # `win95glide` pulls in the TMU and the combiner. The game is the first binary
    # to bring them together - until now only witnesses opened them separately.
    win95f3ddkr win95glide)
# `win95compat` is not named here: it adds itself, first and under
# `--whole-archive`, through the interface options set above. Naming it a second
# time duplicates the archive and the linker refuses - multiple definitions.
set_target_properties(DKRWin95Game PROPERTIES
    OUTPUT_NAME "DKRR"
    SUFFIX ".EXE"                      # 8.3, so it can be launched from DOS
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
dkr_win95_verify(DKRWin95Game)

message(STATUS "Windows 95 target configured: win95compat, DKRWin95Witness, DKRWin95Game")
