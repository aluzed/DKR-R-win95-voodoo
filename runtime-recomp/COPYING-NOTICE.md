# Runtime licensing notice

The original DKR Port source in this repository is MIT licensed. The unified
launcher/runtime executable links `N64ModernRuntime`, whose GPL-3.0 terms apply
to distribution of that combined executable. A binary release therefore needs
the GPL licence notice and complete corresponding source availability for the
exact release revision and linked dependencies.

`N64Recomp` and RT64 retain their separate upstream licences. No Diddy Kong
Racing ROM or extracted game data is part of the source or binary package; the
user supplies a supported Game Pak image locally.

The companion source archive contains the committed project integration source,
build scripts, dependency lock, recomp policy and complete Patch Pipeline. The
preparation scripts fetch the exact upstream source revisions into the ignored
`extern` work area and apply the recorded patches to reconstruct the preferred
source form used for the executable. Generated recompilation output and
user-owned game data are deliberately excluded and are regenerated locally from
the supported ROM.
