# What the Windows 95 package contains, for its licence review

3 October 2026. **An inventory, not legal advice**: E09-S05's licence
criterion asks for a review by someone qualified, and this is the material
for it. `THIRD_PARTY.md` lists the modern build's components; the Windows 95
package is a different set, established here from `DKRR.EXE`'s own symbol
table (`i686-w64-mingw32-nm -C build/win95/bin/DKRR.EXE`) and from what
`cmake/win95-target.cmake` links.

## In `DKRR.EXE`

| Component | Evidence in the executable | Licence (from its source tree) |
|---|---|---|
| This port's code | -- | MIT (`LICENSE.md`) |
| N64ModernRuntime (`ultramodern`, `librecomp`), patched | linked as `win95ultramodern`, `win95librecomp` | GPL-3.0 (`COPYING`, shipped as `COPYING.TXT`) |
| The game's code, recompiled by N64Recomp from the decomp's ELF | `RecompiledFuncs`, `win95recompiled` | **derived from Rare's game**; see below |
| fmt | 334 symbols (`fmt::`) | MIT (`N64Recomp/lib/fmt/LICENSE`) |
| moodycamel ConcurrentQueue | 249 symbols | Simplified BSD or Boost (dual, per its header) |
| nlohmann/json | 652 symbols | MIT |
| miniz | 152 symbols (`mz_`, `tinfl`, `tdefl`) | MIT (RAD Game Tools, Valve, Rich Geldreich) |
| xxHash | 8 symbols (`XXH`) | BSD-2-Clause |
| o1heap | 5 symbols | MIT |
| GCC's libstdc++ and libgcc, mingw-w64's runtime and winpthread | statically linked (`-static`) | GPL-3.0 with the GCC Runtime Library Exception; mingw-w64's own notices |
| Glide 2.x constants and structure layouts | copied into `platform/render/glide.c` (the comment says so) | from 3dfx's `glide.h`; the SDK is not shipped, and `GLIDE2X.DLL` is the user's driver, not in the package |

## Not in `DKRR.EXE`

Checked absent from the symbol table: RT64, SDL2/SDL3 (one symbol mentions
`SDL_GameController` in a function signature; no SDL code is linked), Dear
ImGui, GekkoNet, Monocypher, libdatachannel, Mbed TLS, libjuice, usrsctp, plog,
the texture packs, the CRT overlay. The package script refuses any game asset
and the ROM (`scripts/scan_for_game_assets.py`).

## The questions for the reviewer

1. **The recompiled game code.** `DKRR.EXE` contains the game's code
   translated to C and compiled for x86 -- the same arrangement as the modern
   build and as other N64 recompilation projects. The package ships no asset
   and no ROM, and the game does not run without the user's own ROM; whether
   that is enough is the central question, and it is the same question the
   modern release already answers one way or another.
2. **GPL-3.0 from N64ModernRuntime.** It reaches the executable, so the
   package ships the GPL text (`COPYING.TXT`); the corresponding source is this
   repository and its patches (`patches/`). Whether a written offer or a link
   belongs in `README.TXT` is for the reviewer.
3. **The Glide constants.** A dozen numeric constants and two structure
   layouts, copied for interoperability with the user's own driver. Their
   terms are those of 3dfx's Glide SDK headers, which the package does not
   reproduce and does not cite yet.
4. **The bundled permissive libraries.** fmt, ConcurrentQueue, nlohmann/json,
   miniz, xxHash and o1heap each ask for their notice to accompany binary
   distributions; `THIRDPTY.TXT` in the package is the modern build's list and
   does not name all of them. A Windows 95-specific notice file is the likely
   remedy.
