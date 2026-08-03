# Runtime licensing notice

The original DKR Port source in this repository is MIT licensed. The unified
launcher/runtime executable links `N64ModernRuntime`, whose GPL-3.0 terms apply
to distribution of that combined executable. A binary release therefore needs
the GPL licence notice and complete corresponding source availability for the
exact release revision and linked dependencies.

`N64Recomp` and RT64 retain their separate upstream licences. No Diddy Kong
Racing ROM or extracted game data is part of the source or binary package; the
user supplies a supported Game Pak image locally.

Dependency sources are fetched into the ignored `extern` work area by the
preparation/build scripts. Their resolved revisions are recorded so a release
source bundle can be reproduced without committing generated recompilation
output or user-owned game data.
