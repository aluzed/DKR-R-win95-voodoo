/* The presentation-identity hooks, for a build with the Accurate profile only
 * (E07-S01).
 *
 * `presentation_identity.cpp` gives every object, camera, wave block and HUD
 * widget a stable identity so that RT64 can interpolate between two authored
 * frames: the Modern profile. The Windows 95 build has no Modern profile --
 * `presentation_profile()` starts at Accurate and only `runtime_ui.cpp`, which
 * this build leaves out, can change it -- so every one of those identities was
 * recorded for nobody, and 65 KB of code was linked for it.
 *
 * The recompilation policy is a property of the ROM, not of the target: the
 * generated code calls these hooks on every build, so this build still has to
 * supply them. What they keep is what the Accurate profile itself needs from
 * them, and nothing else:
 *
 *   `dkr_presentation_frame_begin` opens the HUD's authored frame and the
 *   post-race frame gate, as `presentation_identity.cpp` does before anything
 *   Modern-specific;
 *
 *   `dkr_presentation_task_submitted` closes the post-race gate for the task.
 *
 * Every other hook records identity state that only the Modern profile reads,
 * and none of them writes guest memory or a register, so stepping aside leaves
 * the game exactly where it was. `record_presentation_marker` answers what the
 * full version answers outside the Modern profile: nothing was recorded.
 */
#include "finish_presentation_policy.hpp"
#include "presentation_identity.hpp"
#include "runtime_hud_layout.hpp"

#include "recomp.h"

#include <cstdint>

extern "C" {

void dkr_presentation_frame_begin(std::uint8_t* rdram, recomp_context*) {
    dkr::runtime::hud::begin_authored_frame(rdram);
    dkr::runtime::presentation::postrace_presentation_begin_frame();
}

void dkr_presentation_task_submitted(std::uint8_t*, recomp_context*) {
    dkr::runtime::presentation::postrace_presentation_submit_frame(true);
}

void dkr_presentation_scene_begin(std::uint8_t*, recomp_context*) {}
void dkr_presentation_perspective_matrix(std::uint8_t*, recomp_context*) {}
void dkr_presentation_world_origin_matrix(std::uint8_t*, recomp_context*) {}
void dkr_presentation_finish_camera_node(std::uint8_t*, recomp_context*) {}
void dkr_presentation_wave_begin(std::uint8_t*, recomp_context*) {}
void dkr_presentation_wave_end(std::uint8_t*, recomp_context*) {}
void dkr_presentation_wave_block(std::uint8_t*, recomp_context*) {}
void dkr_presentation_wave_selection(std::uint8_t*, recomp_context*) {}
void dkr_presentation_wave_matrix(std::uint8_t*, recomp_context*) {}
void dkr_presentation_object_spawned(std::uint8_t*, recomp_context*) {}
void dkr_presentation_object_freed(std::uint8_t*, recomp_context*) {}
void dkr_presentation_object_begin(std::uint8_t*, recomp_context*) {}
void dkr_presentation_object_end(std::uint8_t*, recomp_context*) {}

} // extern "C"

bool dkr::runtime::presentation::record_presentation_marker(
    std::uint32_t, std::uint8_t, std::uint16_t, std::uint8_t,
    PresentationMarkerKind, hud::groups::Transform) {
    return false;
}

/* Diagnostics of the interpolation, gated on DKR_INTERPOLATION_TRACE: there is
   no interpolation to trace. */
void dkr::runtime::presentation::interpolation_trace_segment_region(
    std::uint8_t*, std::uint32_t, int, bool, bool, bool) {}
void dkr::runtime::presentation::interpolation_trace_segment_block(
    std::uint8_t*, std::uint32_t, bool) {}
