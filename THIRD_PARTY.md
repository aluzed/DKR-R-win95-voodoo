# Third-party sources

## Native launcher libraries

Resolved by the pinned vcpkg checkout during the launcher build:

| Project | Purpose | Upstream licence |
|---|---|---|
| SDL3 | Window, renderer, input, controllers and native file picker | Zlib |
| RmlUi 6.2 | Native in-window launcher layout and styling | MIT |
| FreeType | Runtime font rendering used by RmlUi | FreeType/GPL dual licence |
| vcpkg | C/C++ dependency installation | MIT |

## DKR runtime preparation sources

Cloned locally by `Build-DKR-Runtime.cmd`; not bundled in the clean repository ZIP:

| Project | Purpose | Upstream licence |
|---|---|---|
| DavidSM64/Diddy-Kong-Racing | Matching DKR decomp, ELF metadata, headers and source reference | See upstream `LICENSE.md` |
| N64Recomp/N64Recomp | Static translation of the N64 executable/RSP code to native C; consumed through N64ModernRuntime's pinned submodule | MIT |
| N64Recomp/N64ModernRuntime | libultra/runtime bridge for generated code | GPL-3.0 |
| rt64/rt64 | Recommended RDP renderer for N64 recompilation projects | MIT |

A final executable linked with N64ModernRuntime must be distributed in compliance with GPL-3.0. See
`runtime-recomp/COPYING-NOTICE.md`. Exact resolved commits are written locally by the preparation
script so the experimental runtime build can be reproduced.

## Earlier researched alternatives

`libultraship` and Harbour Masters' Torch remain recorded references for decomp-source port and asset
pipelines. Milestone 0.4 uses N64Recomp/N64ModernRuntime for the earliest boot path because it already
implements much of the N64 runtime boundary and supports RT64.

Each dependency retains its own copyright and licence notices. Nintendo, Rare, Diddy Kong Racing and
related names/assets belong to their respective owners.
