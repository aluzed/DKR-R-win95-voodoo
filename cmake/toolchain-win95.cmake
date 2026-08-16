# E01-S01 - cross-compilation toolchain for Windows 95 / Pentium II.
#
#   cmake -S runtime-recomp -B build/win95 -G Ninja \
#         --toolchain cmake/toolchain-win95.cmake \
#         -DDKR_RUNTIME_TARGET_WIN95=ON
#
# The compiler choice comes from ADR 0001 (E00-S02): mingw-w64 GCC 13, `posix`
# threading model, statically linked CRT, with the `win95compat` bridge. Open
# Watcom remains the fallback the ADR documents, not chosen here because its C++98
# would force `ultramodern` and `librecomp` to be rewritten.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_VERSION   4.0)          # Windows 95
set(CMAKE_SYSTEM_PROCESSOR i686)

set(DKR_WIN95_PREFIX "$ENV{DKR_WIN95_PREFIX}" CACHE PATH
    "Unprivileged installation prefix for the Win95 tooling")
if(DKR_WIN95_PREFIX STREQUAL "")
    set(DKR_WIN95_PREFIX "$ENV{HOME}/.local/dkr-win95")
endif()
set(DKR_WIN95_MINGW_BIN "${DKR_WIN95_PREFIX}/opt/mingw/usr/bin")

# The `-posix` suffix is not cosmetic: it selects winpthreads rather than the
# `win32` model, whose condition variables are Vista's. Both models are short of
# just as many symbols, but the `posix` model's take a few lines to write - see
# ADR 0001.
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

# --- Instruction set ---------------------------------------------------------
#
# The Pentium II has neither SSE nor SSE2; a modern 32-bit compiler nonetheless
# emits SSE2 by default for floating-point arithmetic. `-mfpmath=387` forces the
# x87 stack.
#
# These options do not suffice on their own: they only cover the code compiled
# here, not the CRT's startup nor the standard library. It is
# `tools/win95/check-instruction-set.sh`, wired in after the link, that provides
# the proof - and it is itself verified by injecting SSE.
set(DKR_WIN95_ARCH_FLAGS
    "-march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2")

# --- Windows version ---------------------------------------------------------
#
# 0x0400 hides the APIs later than Windows 95 in mingw's headers. Verified:
# `InitializeConditionVariable` does disappear from the preprocessor's output.
#
# But in C, calling an undeclared function is still only a warning with GCC 13.
# Without `-Werror=implicit-function-declaration`, a Vista API would pass
# compilation only to fail at load time. In C++ the error is native.
set(DKR_WIN95_DEFINES "-D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -DDKR_TARGET_WIN95=1")

set(CMAKE_C_FLAGS_INIT
    "${DKR_WIN95_ARCH_FLAGS} ${DKR_WIN95_DEFINES} -Werror=implicit-function-declaration")
set(CMAKE_CXX_FLAGS_INIT
    "${DKR_WIN95_ARCH_FLAGS} ${DKR_WIN95_DEFINES}")

# --- Linking -----------------------------------------------------------------
#
# Static without exception. `libgcc_s_dw2-1.dll` does not exist under Windows 95,
# and `MSVCRT.DLL` is not present in the system's first generation: it arrives with
# OSR2 or Internet Explorer. Depending on it would amount to making the game depend
# on a version of IE.
set(DKR_WIN95_LINK_STATIC "-static -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${DKR_WIN95_LINK_STATIC}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${DKR_WIN95_LINK_STATIC}")

set(DKR_WIN95_TOOLCHAIN ON CACHE BOOL "Cross-compilation to Windows 95" FORCE)
