#include "presentation_identity.hpp"

#include "recomp.h"

#include "runtime_enhancements.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr std::uint32_t kRdramMask = 0x007FFFFFU;
constexpr std::uint32_t kObjectBehaviourOffset = 0x48U;
constexpr std::uint32_t kObjectIdOffset = 0x4AU;
constexpr std::uint32_t kObjectCurrentMatrixAddress = 0x8011AE90U;
constexpr std::uint32_t kSpTaskNumberAddress = 0x801234E8U;
constexpr std::size_t kMaximumObjectNesting = 16U;
constexpr std::size_t kMaximumPendingFrames = 8U;

struct Lifetime {
    std::uint32_t generation = 0;
    bool alive = false;
};

struct ObjectCapture {
    std::uint32_t identity = 0;
    std::uint32_t first_matrix = 0;
    std::uint32_t buffer = 0;
};

struct ObjectOwner {
    std::uint32_t scene = 0;
    std::uint32_t address = 0;
    std::uint32_t lifetime = 0;

    bool operator==(const ObjectOwner&) const = default;
};

struct MatrixBinding {
    std::uint32_t matrix_identity = 0;
    std::uint32_t object_identity = 0;
};

struct SubmittedFrame {
    std::uint32_t display_list_address = 0;
    std::unordered_map<std::uint32_t, MatrixBinding> matrices;
};

std::mutex g_identity_mutex;
std::unordered_map<std::uint32_t, Lifetime> g_lifetimes;
std::unordered_map<std::uint32_t, ObjectOwner> g_identity_owners;
std::unordered_set<std::uint32_t> g_collided_identities;
std::array<std::unordered_map<std::uint32_t, MatrixBinding>, 2> g_matrix_maps;
std::deque<SubmittedFrame> g_submitted_frames;
bool g_submission_overflowed = false;
std::atomic<std::uint32_t> g_scene_generation{1U};
std::atomic<std::uint32_t> g_next_lifetime{1U};
std::atomic<std::uint64_t> g_identity_collisions{0U};
std::atomic<std::uint64_t> g_matrix_ranges{0U};
thread_local std::array<ObjectCapture, kMaximumObjectNesting> g_capture_stack{};
thread_local std::size_t g_capture_depth = 0U;
thread_local std::size_t g_capture_overflow_depth = 0U;
thread_local std::uint32_t g_recording_buffer = 0U;
thread_local std::unordered_map<std::uint32_t, MatrixBinding>
    g_active_matrix_map;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool ValidObjectAddress(std::uint32_t address) {
    return address >= 0x80000000U && address <= 0x807FFF00U;
}

std::uint32_t Physical(std::uint32_t address) {
    return address & kRdramMask;
}

std::uint32_t ReadU32(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint32_t>(MEM_W(0, RdramAddress(address)));
}

std::uint16_t ReadU16(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint16_t>(MEM_H(0, RdramAddress(address)));
}

std::uint32_t NextLifetimeGeneration() {
    for (;;) {
        const std::uint32_t generation =
            g_next_lifetime.fetch_add(1U, std::memory_order_relaxed);
        if (generation != 0U) {
            return generation;
        }
    }
}

std::uint32_t EnsureLifetimeLocked(std::uint32_t object) {
    Lifetime& lifetime = g_lifetimes[object];
    if (!lifetime.alive || lifetime.generation == 0U) {
        lifetime.generation = NextLifetimeGeneration();
        lifetime.alive = true;
    }
    return lifetime.generation;
}

std::uint32_t ObjectIdentityLocked(std::uint8_t* rdram,
                                   std::uint32_t object) {
    const std::uint32_t generation = EnsureLifetimeLocked(object);
    const std::uint16_t object_id = ReadU16(rdram, object + kObjectIdOffset);
    const std::uint16_t behaviour_id =
        ReadU16(rdram, object + kObjectBehaviourOffset);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint32_t identity =
        dkr::runtime::presentation::make_object_identity(
            scene, Physical(object), generation, object_id, behaviour_id);
    if (g_collided_identities.contains(identity)) {
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    const ObjectOwner owner{scene, Physical(object), generation};
    const auto [it, inserted] = g_identity_owners.emplace(identity, owner);
    if (!inserted && it->second != owner) {
        g_identity_collisions.fetch_add(1U, std::memory_order_relaxed);
        g_collided_identities.insert(identity);
        // Disable both owners of a collision, including bindings authored by
        // the first owner earlier in this frame.
        for (auto& map : g_matrix_maps) {
            std::erase_if(map, [identity](const auto& item) {
                return item.second.object_identity == identity;
            });
        }
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    return identity;
}

void NoteSpawn(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    Lifetime& lifetime = g_lifetimes[object];
    lifetime.generation = NextLifetimeGeneration();
    lifetime.alive = true;
}

void NoteFree(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    const auto it = g_lifetimes.find(object);
    if (it != g_lifetimes.end()) {
        it->second.alive = false;
    }
}

} // namespace

std::uint32_t dkr::runtime::presentation::matrix_identity(
    std::uint32_t physical_matrix_address) {
    const std::uint32_t address = physical_matrix_address & kRdramMask;
    const auto it = g_active_matrix_map.find(address);
    if (it != g_active_matrix_map.end()) {
        return it->second.matrix_identity;
    }
    return kIgnoredIdentity;
}

dkr::runtime::presentation::TaskIdentityScope::TaskIdentityScope(
    std::uint32_t display_list_address) {
    g_active_matrix_map.clear();
    std::scoped_lock lock(g_identity_mutex);
    if (g_submitted_frames.empty()) {
        return;
    }
    SubmittedFrame frame = std::move(g_submitted_frames.front());
    g_submitted_frames.pop_front();
    const std::uint32_t expected = frame.display_list_address & kRdramMask;
    const std::uint32_t actual = display_list_address & kRdramMask;
    if (expected != actual) {
        std::fprintf(stderr,
                     "[boot][presentation] identity sidecar task mismatch "
                     "expected=0x%06X actual=0x%06X; interpolation disabled "
                     "for this task\n",
                     expected, actual);
        return;
    }
    g_active_matrix_map = std::move(frame.matrices);
}

dkr::runtime::presentation::TaskIdentityScope::~TaskIdentityScope() {
    g_active_matrix_map.clear();
}

extern "C" void dkr_presentation_scene_begin(std::uint8_t*, recomp_context*) {
    std::scoped_lock lock(g_identity_mutex);
    std::uint32_t next =
        g_scene_generation.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (next == 0U) {
        g_scene_generation.store(1U, std::memory_order_relaxed);
    }
    g_lifetimes.clear();
    g_identity_owners.clear();
    g_collided_identities.clear();
    g_submission_overflowed = false;
    for (auto& map : g_matrix_maps) {
        map.clear();
        map.reserve(1024U);
    }
}

extern "C" void dkr_presentation_frame_begin(std::uint8_t* rdram,
                                              recomp_context*) {
    g_recording_buffer = ReadU32(rdram, kSpTaskNumberAddress) & 1U;
    g_capture_depth = 0U;
    g_capture_overflow_depth = 0U;
    std::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[g_recording_buffer].clear();
}

extern "C" void dkr_presentation_object_spawned(std::uint8_t*,
                                                 recomp_context* context) {
    NoteSpawn(static_cast<std::uint32_t>(context->r2));
}

extern "C" void dkr_presentation_object_freed(std::uint8_t*,
                                               recomp_context* context) {
    NoteFree(static_cast<std::uint32_t>(context->r4));
}

extern "C" void dkr_presentation_task_submitted(std::uint8_t*,
                                                  recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    SubmittedFrame frame{};
    frame.display_list_address = static_cast<std::uint32_t>(context->r4);
    std::scoped_lock lock(g_identity_mutex);
    if (g_submission_overflowed) {
        return;
    }
    if (g_submitted_frames.size() >= kMaximumPendingFrames) {
        g_submitted_frames.clear();
        g_submission_overflowed = true;
        std::fprintf(stderr,
                     "[boot][presentation] semantic sidecar queue exceeded "
                     "%zu tasks; interpolation identities disabled until "
                     "the next scene\n",
                     kMaximumPendingFrames);
        return;
    }
    frame.matrices = std::move(g_matrix_maps[g_recording_buffer & 1U]);
    g_matrix_maps[g_recording_buffer & 1U].reserve(1024U);
    g_submitted_frames.emplace_back(std::move(frame));
}

extern "C" void dkr_presentation_object_begin(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    if (g_capture_depth >= g_capture_stack.size()) {
        ++g_capture_overflow_depth;
        return;
    }
    ObjectCapture& capture = g_capture_stack[g_capture_depth++];
    capture = {};
    capture.buffer = g_recording_buffer;
    const std::uint32_t object = static_cast<std::uint32_t>(context->r7);
    if (!ValidObjectAddress(object)) {
        return;
    }
    const gpr matrix_reference = MEM_W(context->r29, 0x24);
    const std::uint32_t first_matrix =
        static_cast<std::uint32_t>(MEM_W(0, matrix_reference));
    if (!ValidObjectAddress(first_matrix)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    capture.identity = ObjectIdentityLocked(rdram, object);
    capture.first_matrix = Physical(first_matrix);
}

extern "C" void dkr_presentation_object_end(std::uint8_t* rdram,
                                             recomp_context*) {
    if (g_capture_overflow_depth != 0U) {
        --g_capture_overflow_depth;
        return;
    }
    if (g_capture_depth == 0U) {
        return;
    }
    const ObjectCapture capture = g_capture_stack[--g_capture_depth];
    const std::uint32_t end_matrix =
        Physical(ReadU32(rdram, kObjectCurrentMatrixAddress));
    if (capture.identity == dkr::runtime::presentation::kIgnoredIdentity ||
        end_matrix < capture.first_matrix ||
        ((end_matrix - capture.first_matrix) & 0x3FU) != 0U) {
        return;
    }
    const std::uint32_t matrix_count =
        (end_matrix - capture.first_matrix) / 64U;
    if (matrix_count == 0U || matrix_count > 256U) {
        return;
    }

    std::scoped_lock lock(g_identity_mutex);
    auto& map = g_matrix_maps[capture.buffer & 1U];
    for (std::uint32_t ordinal = 0; ordinal < matrix_count; ++ordinal) {
        const std::uint32_t address = capture.first_matrix + ordinal * 64U;
        // A nested render_object completes first. Preserve its more-specific
        // ownership when the outer object's wider range is closed.
        map.try_emplace(address, MatrixBinding{
            dkr::runtime::presentation::make_matrix_identity(
                capture.identity, ordinal),
            capture.identity});
    }
    g_matrix_ranges.fetch_add(1U, std::memory_order_relaxed);
}
