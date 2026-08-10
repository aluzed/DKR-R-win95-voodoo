#include "intro_tail_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using dkr::runtime::intro::TailAction;

    // The real cinematic completion is a one-update edge. It must remain
    // latched throughout the tail and be replayed once after 30 authored
    // update units even though subsequent raw samples are false.
    dkr::runtime::intro::TailGate gate{};
    assert(gate.update(true, true, false, 1U) == TailAction::Hold);
    for (std::uint32_t tick = 1U;
         tick < dkr::runtime::intro::TailGate::kTailUpdateUnits; ++tick) {
        assert(gate.update(false, true, false, 1U) == TailAction::Hold);
    }
    assert(gate.update(false, true, false, 1U) == TailAction::Release);
    assert(gate.update(false, true, false, 1U) == TailAction::Pass);

    // Coarse update rates still count authored time rather than presentation
    // frames, and a level/menu lifecycle change cancels a partial latch.
    for (int sample = 0; sample < 6; ++sample) {
        assert(gate.update(sample == 0, true, false, 5U) == TailAction::Hold);
    }
    assert(gate.update(false, true, false, 5U) == TailAction::Release);
    assert(gate.update(true, false, false, 1U) == TailAction::Pass);

    assert(gate.update(true, true, false, 5U) == TailAction::Hold);
    assert(gate.update(false, true, true, 1U) == TailAction::Pass);
    assert(gate.update(false, true, false, 1U) == TailAction::Pass);

    // A level-triggered completion also produces only one release edge.
    gate.reset();
    for (std::uint32_t tick = 0U;
         tick < dkr::runtime::intro::TailGate::kTailUpdateUnits; ++tick) {
        assert(gate.update(true, true, false, 1U) == TailAction::Hold);
    }
    assert(gate.update(true, true, false, 1U) == TailAction::Release);
    assert(gate.update(true, true, false, 1U) == TailAction::Hold);

    std::puts("[test][intro-tail-policy] PASS");
    return 0;
}
