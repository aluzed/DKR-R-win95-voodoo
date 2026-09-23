#pragma once

// **A measurement mode that turns wall time into processor time.**
//
// Windows 95 does not implement `GetThreadTimes`, so there is no way to ask how
// much processor a thread used. Every figure in `docs/research/cpu-budget.md` is
// therefore a wall interval, and on one processor a wall interval counts the time
// of every thread that preempted the one being timed. Patch 0050 showed how much
// that matters: the audio microcode has no row in the budget and is still inside
// the other rows, as preemption.
//
// With DKR_TRACE_EXCLUSIVE set, a section raises its thread to
// THREAD_PRIORITY_TIME_CRITICAL for its duration and restores it on exit. No other
// thread of the process can then run inside it -- only hardware interrupts can --
// so the section's wall interval is its processor time. The price is that the
// sections now delay everything else, which changes the frame; the figure worth
// reading from such a run is each section's own cost, not the frame.
//
// Off unless the variable is set, and a no-op on every target but Windows 95.

#if defined(DKR_TARGET_WIN95)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdlib>

namespace dkr::runtime {

// Namespace scope and not a function-local static: see the note on
// `g_guest_time_on` in ultramodern's threads.cpp about initialisation guards on
// this toolchain.
static const bool kExclusiveSections = std::getenv("DKR_TRACE_EXCLUSIVE") != nullptr;

class ExclusiveSection {
public:
    ExclusiveSection() {
        if (kExclusiveSections) {
            previous_ = GetThreadPriority(GetCurrentThread());
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        }
    }
    ~ExclusiveSection() {
        if (kExclusiveSections) {
            SetThreadPriority(GetCurrentThread(), previous_);
        }
    }
    ExclusiveSection(const ExclusiveSection&) = delete;
    ExclusiveSection& operator=(const ExclusiveSection&) = delete;

private:
    int previous_ = THREAD_PRIORITY_NORMAL;
};

} // namespace dkr::runtime

#else

namespace dkr::runtime {

class ExclusiveSection {
public:
    ExclusiveSection() = default;
    ExclusiveSection(const ExclusiveSection&) = delete;
    ExclusiveSection& operator=(const ExclusiveSection&) = delete;
};

} // namespace dkr::runtime

#endif
