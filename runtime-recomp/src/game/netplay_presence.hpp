#pragma once

// Whether this build carries online play at all.
//
// **Defaults to 1, and that direction is deliberate.** A target that says
// nothing gets netplay, so a target added upstream cannot lose online play by
// omission — only by saying so. The reverse default would make every new target
// silently single-player until someone noticed.
//
// Windows 95 sets it to 0, and that is not a scope decision. The netplay sources
// call `getaddrinfo`, `freeaddrinfo`, `inet_pton` and `inet_ntop`, and none of
// the four is in the export table of the machine's `WSOCK32.DLL` — read from the
// machine itself, see `tools/win95/exports/PROVENANCE.md`. On this target a
// missing import stops the process from *loading*, whether or not the function
// is ever called, so linking them would not give a build without online play: it
// would give a build that does not start. `WSAStartup(MAKEWORD(2, 2))` asks for
// Winsock 2.2 besides, where the machine carries 1.1, and two of the transports
// are built on libdatachannel — WebRTC, with DTLS and ICE under it.
//
// What the guard protects is therefore the loader, not the frame budget.
#ifndef DKR_RUNTIME_HAS_NETPLAY
#define DKR_RUNTIME_HAS_NETPLAY 1
#endif

// Whether this build carries the legacy mod system.
//
// Same default and same reasoning as the line above: a target that says nothing
// gets mods. Windows 95 says no, and here it *is* a scope decision rather than a
// loader one — 35 sources and five thousand lines of character and asset
// modding, serving a launcher path this target never takes, against the eight
// mebibytes ADR 0003 has to fit the whole game into. The mod system has been out
// of scope since E00-S01; this only makes the build say so.
#ifndef DKR_RUNTIME_HAS_LEGACY_MODS
#define DKR_RUNTIME_HAS_LEGACY_MODS 1
#endif

// Which recompiled ROM revisions this build carries a payload for.
//
// `game_payload.cpp` maps a revision to its entrypoint, and there are two. The
// Windows 95 target recompiles US v1.0 only, so `payload_v80` has no definition
// to link against and the v1.1 arm of that switch has to go with it.
#ifndef DKR_RUNTIME_HAS_PAYLOAD_V80
#define DKR_RUNTIME_HAS_PAYLOAD_V80 1
#endif
