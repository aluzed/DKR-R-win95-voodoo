#include "interpolation_state_policy.hpp"

#include <cassert>
#include <cstdio>

using dkr::runtime::interpolation::GroupState;
using dkr::runtime::interpolation::effective_tile_interpolation;

int main() {
    assert(!effective_tile_interpolation(true, 0U));
    assert(effective_tile_interpolation(true, 1U));
    assert(effective_tile_interpolation(true, 7U));
    assert(!effective_tile_interpolation(false, 0U));
    assert(!effective_tile_interpolation(false, 7U));

    GroupState state{};
    constexpr std::uint32_t world = 0x11111111U;
    constexpr std::uint32_t object = 0x22222222U;
    constexpr std::uint32_t billboard = 0x33333333U;
    constexpr std::uint32_t shadow = 0x44444444U;

    state.load_matrix(0U, world);
    state.load_matrix(1U, object);
    state.load_matrix(2U, billboard, true, true, true);
    assert(state.active_group().identity == billboard);
    assert(state.active_group().interpolate_vertices);
    assert(state.active_group().interpolate_texcoords);
    assert(state.active_group().interpolate_tiles);
    state.load_matrix(1U, object);

    // Regression: gSPSelectMatrixDKR used to restore only the matrix. The
    // billboard/object ID remained active and was then inherited by terrain.
    state.select_matrix(0U);
    assert(state.active_group().identity == world);
    state.select_matrix(1U);
    assert(state.active_group().identity == object);

    // A scoped draw overrides the selected slot but must restore that exact
    // slot when it ends. Matrices loaded while scoped are retained for later.
    assert(state.begin_scope(2U, shadow, true));
    assert(state.active_group().identity == shadow);
    assert(state.active_group().interpolate_vertices);
    assert(!state.active_group().interpolate_texcoords);
    assert(!state.active_group().interpolate_tiles);
    state.load_matrix(2U, billboard, true, true);
    assert(state.active_group().identity == shadow);
    const auto ended_shadow = state.end_scope();
    assert(ended_shadow.had_scope && ended_shadow.ended.mode == 2U);
    assert(state.active_group().identity == billboard);

    // Nested scopes restore their parent rather than falling straight back to
    // world geometry. This protects transitions and dynamically generated
    // billboards that can be emitted from another presentation scope.
    assert(state.begin_scope(3U, 0U, false));
    assert(state.contains_mode(3U));
    assert(state.begin_scope(6U, billboard, true));
    assert(state.active_group().identity == billboard);

    assert(state.begin_scope(7U, 0x55555555U, false, true, true));
    assert(!state.active_group().interpolate_vertices);
    assert(state.active_group().interpolate_texcoords);
    assert(state.active_group().interpolate_tiles);
    assert(state.end_scope().ended.mode == 7U);
    assert(state.end_scope().ended.mode == 6U);
    assert(state.active_group().mode == 3U);
    assert(state.contains_mode(3U));
    assert(state.end_scope().ended.mode == 3U);
    assert(!state.has_active_scope());
    assert(!state.contains_mode(3U));
    assert(state.active_group().identity == billboard);

    state.select_matrix(99U);
    assert(state.selected_matrix() == 2U);
    assert(!state.end_scope().had_scope);

    std::puts("[test][interpolation-state-policy] PASS");
    return 0;
}
