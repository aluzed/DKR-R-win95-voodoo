/* The hooks the recompiler injects for subsystems this build does not carry.
 *
 * `dkr.us.v77.recomp-policy.json` is a property of the ROM revision, not of the
 * target: the generated code calls every hook the policy names, on every build.
 * A target that leaves netplay or custom tracks out therefore still has to
 * *supply* those entry points, or it does not link.
 *
 * **This is not a stub implementation of either subsystem.** Each of these is
 * the hook's answer when the thing it hooks into does not exist, and the two
 * that return a value return the one the caller's contract gives for "nothing
 * happened here":
 *
 *   `drive_authored_tick` returns non-zero only when rollback has already run
 *   this tick and the generated call must be skipped. Nothing rolls back here,
 *   so the game always runs its own loop: 0.
 *
 *   `presentation_random_range` returns non-zero when the synchronized session
 *   has overridden the range. There is no session: 0, and the game keeps its
 *   own random.
 *
 * The file is compiled **only** when the guards are off, so on a build that does
 * carry the subsystem these definitions cannot collide with the real ones.
 * See `netplay_presence.hpp` for why Windows 95 turns them off. */

#include "netplay_presence.hpp"
#include "recomp.h"

#include <cstdint>

#if !DKR_RUNTIME_HAS_NETPLAY

extern "C" {

void dkr_netplay_adventure_finish_barrier(std::uint8_t*, recomp_context*) {}
void dkr_netplay_authoritative_frame_commit(std::uint8_t*, recomp_context*) {}
void dkr_netplay_character_select_ai_seed(std::uint8_t*, recomp_context*) {}
void dkr_netplay_character_select_enter(std::uint8_t*, recomp_context*) {}
void dkr_netplay_character_select_lock(std::uint8_t*, recomp_context*) {}
void dkr_netplay_gameplay_level_begin(std::uint8_t*, recomp_context*) {}
void dkr_netplay_gameplay_level_end(std::uint8_t*, recomp_context*) {}
void dkr_netplay_gameplay_level_ready(std::uint8_t*, recomp_context*) {}
void dkr_netplay_postrace_barrier(std::uint8_t*, recomp_context*) {}
void dkr_netplay_prepare_controller_init(std::uint8_t*, recomp_context*) {}
void dkr_netplay_presentation_random_begin(std::uint8_t*, recomp_context*) {}
void dkr_netplay_presentation_random_end(std::uint8_t*, recomp_context*) {}
void dkr_netplay_resolve_authored_input_frame(std::uint8_t*, recomp_context*) {}

/* Nothing has driven this tick, so the generated loop must run. */
int dkr_netplay_drive_authored_tick(std::uint8_t*, recomp_context*) { return 0; }

/* Nothing has overridden the range, so the game's own random stands. */
int dkr_netplay_presentation_random_range(std::uint8_t*, recomp_context*) {
    return 0;
}

} // extern "C"

#endif  /* !DKR_RUNTIME_HAS_NETPLAY */

#if !DKR_RUNTIME_HAS_RT64

/* Custom tracks are RT64's: they load replacement assets through the renderer's
   own table. Without it the game reads the assets the ROM carries, which is what
   these hooks stepping aside leaves it doing. */
extern "C" {

void dkr_custom_tracks_asset_load_begin(std::uint8_t*, recomp_context*) {}
void dkr_custom_tracks_asset_load_end(std::uint8_t*, recomp_context*) {}
void dkr_custom_tracks_auto_boot(std::uint8_t*, recomp_context*) {}
void dkr_custom_tracks_table_load_begin(std::uint8_t*, recomp_context*) {}
void dkr_custom_tracks_table_load_end(std::uint8_t*, recomp_context*) {}
void dkr_custom_tracks_track_id_override(std::uint8_t*, recomp_context*) {}

} // extern "C"

#endif  /* !DKR_RUNTIME_HAS_RT64 */
