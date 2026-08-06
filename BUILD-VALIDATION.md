# DKR-R build validation record

Validation date: **6 August 2026**  
Runtime source revision: `fd17ef4f4feabb4328e62098bd1a90a55c454ce0`  
Candidate: **DKR-R 1.0.0 RC4**

## Scope

RC4 completes the planned Phase 3-8 release work after the previously accepted
launcher/overlay navigation phase:

- HUD Placement was removed from UI, persistence, renderer configuration and
  recomp hooks; both presets use the original authored HUD layout.
- Modern post-race camera viewports expand horizontally without moving results
  text or HUD coordinates.
- skydome horizontal cover remains intact while super-ultrawide vertical cover
  corrects the horizon direction beyond 21:9.
- gyro default polarity is corrected and steering now integrates controller
  angle, holds while tilted, returns with physical recentering and exposes an
  explicit Recenter Steering action.
- the runtime, metadata, icon, packages and public documentation use DKR-R /
  Diddy Kong Racing - Recompiled consistently.
- legacy save/config directory IDs remain unchanged to preserve existing data.

## Protected-source discipline

All game instruction/function changes entered through
`runtime-recomp/dkr.us.v77.recomp-policy.json` and were regenerated with the
Patch Pipeline. No manual diff exists under `extern/`,
`runtime-recomp/RecompiledFuncs` or `runtime-recomp/RecompiledPatches`.

## Windows build and tests

- Generator: Visual Studio 2022 x64 using its bundled native CMake.
- Configuration: Release, generated runtime and RT64 enabled.
- Runtime: `build/dkr-runtime-rt64/bin/Release/DKR-R.exe`.
- Windows resource metadata reports product `DKR-R`, description
  `Diddy Kong Racing - Recompiled`, version `1.0.0` and original filename
  `DKR-R.exe`.
- The supplied multi-size Diddy Kong Racing icon is compiled into the runtime.
- `DKRRuntimeProbe` linked generated CPU functions, N64ModernRuntime and RT64.
- raw and packaged Windows runtimes passed virtual Controller Pak round-trip
  and backup recovery.

Release CTest result: **11/11 passed**.

1. DKRPresentationPolicy
2. DKRModernCameraPolicy
3. DKRWidescreenPolicy
4. DKRCharacterSelectAnimationPolicy
5. DKRCharacterSelectMusicPolicy
6. DKRMotionSteeringPolicy
7. DKRSaveManager
8. DKRAudioEqualizer
9. DKRAudioMixPolicy
10. DKRRendererSnapshot
11. DKRPresentationIdentity

All assert-based policy tests explicitly undefine `NDEBUG` in Release builds.

## Linux and AppImage

- Qualified builder: Ubuntu 24.04 under WSL2, GCC/Ninja Release.
- Runtime: `build/dkr-runtime-linux/bin/Release/DKR-R`.
- RT64 backend: Vulkan with SDL2 Vulkan window integration.
- Linux CTest: **11/11 passed**.
- raw Linux runtime and packaged AppImage both passed virtual Controller Pak
  round-trip and backup recovery.
- AppImage packaging collected 65 dependency copyright records for 71 bundled
  libraries.
- AppDir safety scan passed: 163 files inspected, with no prohibited ROM/save
  extensions or N64 ROM headers.

This proves Linux compilation and packaging. Physical Steam Deck/SteamOS launch
and gameplay remain a user-visible hardware qualification gate.

## Repository and package safety

- Repository asset scan passed: 252 source files inspected.
- Windows staging and ZIP scans passed.
- Linux AppDir scan passed.
- deterministic source ZIP scan passed: 208 files inspected.
- no ROM or extracted game asset is included in any RC4 artifact.

## Artifacts

Windows:

- `dist/DKR-R-1.0.0-rc4-Windows-x64.zip`
- size: 14,189,288 bytes
- SHA-256: `DE2609E98ABCFAF501C01A36BD7E5B62392FB12AEE403728758DFB0513CC9054`

Linux:

- `dist/DKR-R-1.0.0-rc4-Linux-x86_64.AppImage`
- size: 17,705,464 bytes
- SHA-256: `B5F040366141B3C956A10CBA1710A8D4DD1384AE2CB4047427B0261EC36A8737`

Corresponding source:

- `dist/DKR-R-1.0.0-rc4-Source.zip`
- size: 856,009 bytes
- SHA-256: `736113A4A4DAECB79E15F0C33536B26690B3F72A7A95128D3059B77299FF0E6F`

## User-visible RC4 gate

Automated validation cannot certify composition or physical-controller feel.
Before promoting RC4 to the public 1.0 tag, complete one visible Windows pass
covering:

1. Accurate 4:3 intro, character select and one race.
2. Modern 16:9 plus a post-race results screen.
3. Modern 32:9 horizon placement and edge culling.
4. vehicle shadows and steering-wheel rendering above 30 FPS.
5. mouse and controller navigation across every in-game overlay page.
6. gyro right/left polarity, held steering angle, physical return and Recenter.
7. audio transition into/out of character select and several minutes of racing.
8. clean Exit to Desktop.

Then test the RC4 AppImage on a physical Steam Deck before advertising SteamOS
as fully qualified.
