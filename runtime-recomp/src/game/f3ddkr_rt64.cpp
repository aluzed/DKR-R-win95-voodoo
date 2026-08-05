#include "f3ddkr_rt64.hpp"

#include "presentation_identity.hpp"
#include "runtime_enhancements.hpp"

#include "gbi/rt64_f3d.h"
#include "gbi/rt64_gbi_f3d.h"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_application.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::uint8_t kMatrixOpcode = 0x01;
constexpr std::uint8_t kTextureOffsetOpcode = 0x02;
constexpr std::uint8_t kVertexOpcode = 0x04;
constexpr std::uint8_t kTriangleOpcode = 0x05;
constexpr std::uint8_t kDisplayListOpcode = 0x06;
constexpr std::uint8_t kCountedDisplayListOpcode = 0x07;
constexpr std::uint8_t kDMAOffsetsOpcode = 0xBF;
constexpr std::uint8_t kMoveWordOpcode = 0xBC;
constexpr std::uint8_t kSetTextureImageOpcode = 0xFD;
constexpr std::uint8_t kLoadBlockOpcode = 0xF3;
constexpr std::uint8_t kFillRectOpcode = 0xF6;
constexpr std::uint8_t kEndDisplayListOpcode = 0xB8;
constexpr std::uint8_t kMoveWordBillboard = 0x02;
constexpr std::uint8_t kMoveWordMVPMatrix = 0x0A;
constexpr std::uint8_t kMoveWordPresentationGroup = 0xFE;
constexpr std::uint32_t kPresentationGroupMagic = 0x444B5200U;
constexpr std::uint32_t kRDRAMAddressMask = 0x00FFFFFFU;
constexpr std::uint32_t kRDRAMSize = 0x00800000U;
constexpr std::uint32_t kScratchVertexAddress = 0x007FE000U;
constexpr std::uint32_t kMaxDKRVertices = 32;
constexpr std::uint32_t kMaxNestedDisplayLists = 32;
std::uint32_t g_logged_matrices = 0;
std::uint32_t g_logged_vertices = 0;
std::uint32_t g_logged_triangles = 0;
std::uint32_t g_logged_offsets = 0;
std::uint32_t g_logged_movewords = 0;
std::uint64_t g_profiled_tasks = 0;
std::uint64_t g_profiled_microseconds = 0;
std::uint64_t g_profiled_max_microseconds = 0;
std::atomic<std::uint64_t> g_completed_tasks{0};
std::uint32_t g_logged_counted_lists = 0;
std::uint32_t g_logged_counted_errors = 0;
std::uint32_t g_logged_texture_contexts = 0;
std::uint32_t g_logged_presentation_groups = 0;
std::atomic<bool> g_logged_background_fill_rect{false};


bool DeepTraceEnabled() {
    static const bool enabled = std::getenv("DKR_F3DDKR_TRACE") != nullptr;
    return enabled;
}

bool TaskTraceEnabled() {
    static const bool enabled = std::getenv("DKR_F3DDKR_TASK_TRACE") != nullptr;
    return enabled;
}

bool SpriteTraceEnabled() {
    static const bool enabled = std::getenv("DKR_F3DDKR_SPRITE_TRACE") != nullptr;
    return enabled;
}

std::FILE* SpriteTraceStream() {
    static std::FILE* stream = []() -> std::FILE* {
        const char* path = std::getenv("DKR_F3DDKR_SPRITE_TRACE_FILE");
        if (path == nullptr || *path == '\0') {
            return stderr;
        }
        std::FILE* file = std::fopen(path, "wb");
        if (file == nullptr) {
            return stderr;
        }
        std::setvbuf(file, nullptr, _IONBF, 0);
        return file;
    }();
    return stream;
}

// The completed decomp shows every G_DMADL payload is either a two-command
// material-mode block or a texture/TLUT upload produced by the libultra GBI
// macros. In particular, framebuffer, depth-image and full-sync commands are
// never legal inside G_DMADL. Treating recycled texels as a permissive Fast3D
// list can otherwise create bogus framebuffer pairs and corrupt RT64 at the
// next real full sync.
bool IsSafeCountedOpcode(std::uint8_t opcode) {
    switch (opcode) {
        case 0xE6: // G_RDPLOADSYNC
        case 0xE7: // G_RDPPIPESYNC
        case 0xE8: // G_RDPTILESYNC
        case 0xEF: // G_RDPSETOTHERMODE
        case 0xF0: // G_LOADTLUT
        case 0xF2: // G_SETTILESIZE
        case 0xF3: // G_LOADBLOCK
        case 0xF4: // G_LOADTILE
        case 0xF5: // G_SETTILE
        case 0xF8: // G_SETFOGCOLOR
        case 0xF9: // G_SETBLENDCOLOR
        case 0xFA: // G_SETPRIMCOLOR
        case 0xFB: // G_SETENVCOLOR
        case 0xFC: // G_SETCOMBINE
        case 0xFD: // G_SETTIMG
            return true;
        default:
            return false;
    }
}

std::int16_t ReadS16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t value = 0;
    std::memcpy(&value, rdram + ((address & kRDRAMAddressMask) ^ 2U), sizeof(value));
    return value;
}

std::uint16_t ReadU16(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint16_t value = 0;
    std::memcpy(&value, rdram + ((address & kRDRAMAddressMask) ^ 2U), sizeof(value));
    return value;
}

std::uint8_t ReadU8(const std::uint8_t* rdram, std::uint32_t address) {
    return rdram[(address & kRDRAMAddressMask) ^ 3U];
}

std::uint32_t ReadU32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t value = 0;
    std::memcpy(&value, rdram + (address & kRDRAMAddressMask), sizeof(value));
    return value;
}

void LogTextureContext(const std::uint8_t* rdram, std::uint32_t address,
                       std::uint64_t task_count, bool force = false,
                       std::FILE* output = nullptr) {
    if (!DeepTraceEnabled() && !force) {
        return;
    }
    if (output == nullptr) {
        output = stderr;
    }
    if (g_logged_texture_contexts++ >= 24U) {
        return;
    }

    constexpr std::uint32_t kTextureCachePointer = 0x00126328U;
    constexpr std::uint32_t kTextureCacheCount = 0x00126330U;
    const std::uint32_t cache = ReadU32(rdram, kTextureCachePointer) & kRDRAMAddressMask;
    const std::uint32_t cache_count =
        std::min(ReadU32(rdram, kTextureCacheCount), 700U);
    std::uint32_t nearest_id = 0xFFFFFFFFU;
    std::uint32_t nearest_header = 0U;
    std::uint32_t nearest_distance = kRDRAMSize;

    for (std::uint32_t i = 0; i < cache_count && cache <= kRDRAMSize - 8U; ++i) {
        const std::uint32_t entry = cache + i * 8U;
        if (entry > kRDRAMSize - 8U) {
            break;
        }
        const std::uint32_t id = ReadU32(rdram, entry + 0U);
        std::uint32_t header = ReadU32(rdram, entry + 4U) & kRDRAMAddressMask;
        if (id == 0xFFFFFFFFU || header > kRDRAMSize - 0x20U) {
            continue;
        }

        const std::uint32_t frame_count =
            std::max(1U, std::min(static_cast<std::uint32_t>(
                                     ReadU16(rdram, header + 0x12U) >> 8U),
                                 255U));
        for (std::uint32_t frame = 0; frame < frame_count &&
                                      header <= kRDRAMSize - 0x20U;
             ++frame) {
            const std::uint32_t command =
                ReadU32(rdram, header + 0x0CU) & kRDRAMAddressMask;
            const std::uint32_t command_count = ReadU16(rdram, header + 0x0AU);
            if (command == address) {
                std::fprintf(output,
                             "[boot][f3ddkr][texture-context] task=%llu "
                             "address=0x%06X cache-slot=%u id=0x%04X frame=%u "
                             "header=0x%06X size=%ux%u format=0x%02X "
                             "commands=%u texture-size=%u\n",
                             static_cast<unsigned long long>(task_count), address, i,
                             id & 0xFFFFU, frame, header, ReadU8(rdram, header + 0U),
                             ReadU8(rdram, header + 1U), ReadU8(rdram, header + 2U),
                             command_count, ReadU16(rdram, header + 0x16U));
                return;
            }
            if (header <= address && address - header < nearest_distance) {
                nearest_id = id;
                nearest_header = header;
                nearest_distance = address - header;
            }
            const std::uint32_t texture_size = ReadU16(rdram, header + 0x16U);
            if (texture_size < 0x20U || texture_size > kRDRAMSize - header) {
                break;
            }
            header += texture_size;
        }
    }

    std::fprintf(output,
                 "[boot][f3ddkr][texture-context] task=%llu address=0x%06X "
                 "no-command-owner nearest-id=0x%04X nearest-header=0x%06X "
                 "distance=0x%X cache=0x%06X count=%u\n",
                 static_cast<unsigned long long>(task_count), address,
                 nearest_id & 0xFFFFU, nearest_header, nearest_distance, cache,
                 cache_count);
}

float ReadF32(const std::uint8_t* rdram, std::uint32_t address) {
    float value = 0.0F;
    std::memcpy(&value, rdram + (address & kRDRAMAddressMask), sizeof(value));
    return value;
}

void LogFloatMatrix(const char* name, const std::uint8_t* rdram,
                    std::uint32_t address) {
    std::fprintf(stderr, "[boot][f3ddkr][camera-global] %s=0x%06X", name, address);
    for (std::uint32_t row = 0; row < 4; ++row) {
        std::fprintf(stderr, " row%u=(%.6g,%.6g,%.6g,%.6g)", row,
                     ReadF32(rdram, address + (row * 4U + 0U) * sizeof(float)),
                     ReadF32(rdram, address + (row * 4U + 1U) * sizeof(float)),
                     ReadF32(rdram, address + (row * 4U + 2U) * sizeof(float)),
                     ReadF32(rdram, address + (row * 4U + 3U) * sizeof(float)));
    }
    std::fputc('\n', stderr);
}

std::uint32_t PhysicalAddress(RT64::RSP& rsp, std::uint32_t address) {
    return rsp.fromSegmented(address) & kRDRAMAddressMask;
}

void SelectInterpolationGroup(RT64::RSP& rsp, std::uint32_t id) {
    const bool interpolation_disabled = id == G_EX_ID_IGNORE;
    const std::uint8_t transform_component = interpolation_disabled
        ? G_EX_COMPONENT_SKIP
        : G_EX_COMPONENT_INTERPOLATE;
    // DKR's scrolling and animated textures are authored at the original
    // cadence and already look correct without inferred tile motion. Asking
    // RT64 to AUTO-match tiles makes repeated materials generate a large
    // generic candidate set in busy races, even though object transforms have
    // exact semantic identities. Keep texture animation faithful at 30 Hz and
    // reserve Modern interpolation for camera/object geometry.
    const std::uint8_t tile_component = G_EX_COMPONENT_SKIP;
    // F3DDKR submits an already-combined model/view/projection matrix through
    // its matrix command. It is not an affine model transform and cannot be
    // safely decomposed into a rigid body. Interpolate all matrix regions
    // component-wise so the current 30 Hz pose remains exact at weight 1 and
    // RT64 can generate valid intermediate camera/object poses.
    rsp.matrixId(id, false, false, false,
                 transform_component, transform_component,
                 G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                 transform_component,
                 G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                 tile_component, G_EX_COMPONENT_SKIP,
                 // Every non-ignored ID is an immutable object-lifetime plus
                 // matrix-ordinal identity captured by the decomp patch. Tell
                 // RT64 to pair equal IDs directly and in submission order.
                 // AUTO ordering discards that guarantee and falls back to a
                 // geometric candidate search whose cost grows rapidly in
                 // scenes with many repeated draw calls.
                 G_EX_ORDER_LINEAR, G_EX_ASPECT_AUTO,
                 G_EX_EDIT_NONE, false, false);
}

} // namespace

std::uint64_t dkr::runtime::completed_f3ddkr_task_count() {
    return g_completed_tasks.load(std::memory_order_acquire);
}

struct dkr::runtime::F3DDKRRT64Bridge::StateData {
    std::uint32_t matrix_offset = 0;
    std::uint32_t vertex_offset = 0;
    std::uint32_t texture_offset = 0;
    std::uint32_t texture_shift = 0;
    std::uint32_t texture_count = 0;
    std::uint32_t vertex_cursor = 0;
    std::uint32_t selected_matrix = 0;
    bool billboard = false;
    std::uint32_t nested_depth = 0;
    std::uint64_t task_count = 0;
    std::uint32_t matrix_commands = 0;
    std::uint32_t vertex_commands = 0;
    std::uint32_t triangle_commands = 0;
    std::uint32_t counted_commands = 0;
    std::uint32_t semantic_matrix_hits = 0;
    std::uint32_t semantic_matrix_misses = 0;
    std::uint32_t presentation_group_begins = 0;
    std::uint32_t presentation_group_ends = 0;
    bool presentation_group_active = false;
    bool background_fill_stretch_active = false;
    std::uint32_t current_material_address = 0;
    std::uint32_t current_material_count = 0;
    std::uint32_t current_texture_image = 0;
    std::uint16_t current_texture_width = 0;
    std::uint16_t current_load_uls = 0;
    std::uint16_t current_load_ult = 0;
    std::uint16_t current_load_lrs = 0;
    std::uint16_t current_load_dxt = 0;
    std::uint8_t current_texture_format = 0;
    std::uint8_t current_texture_size = 0;
    std::uint8_t current_load_tile = 0;
    std::uint32_t last_vertex_source = 0;
    std::uint32_t last_vertex_destination = 0;
    std::uint32_t last_vertex_count = 0;
    std::uint32_t traced_gold_knot_batches = 0;
    std::uint32_t current_matrix_group_id = G_EX_ID_IGNORE;
    std::array<RT64::DisplayList*, kMaxNestedDisplayLists> return_stack{};
    std::uint32_t return_depth = 0;
};

dkr::runtime::F3DDKRRT64Bridge* dkr::runtime::F3DDKRRT64Bridge::active_ = nullptr;

dkr::runtime::F3DDKRRT64Bridge::F3DDKRRT64Bridge()
    : gbi_(new RT64::GBI{}), data_(new StateData{}) {
    RT64::GBI_RDP::setup(gbi_, true);
    RT64::GBI_F3D::setup(gbi_);
    gbi_->ucode = RT64::GBIUCode::F3D;
    gbi_->map[kMatrixOpcode] = &Matrix;
    gbi_->map[kTextureOffsetOpcode] = &TextureOffset;
    gbi_->map[kVertexOpcode] = &Vertex;
    gbi_->map[kTriangleOpcode] = &Triangle;
    gbi_->map[kDisplayListOpcode] = &DisplayListBranch;
    gbi_->map[kEndDisplayListOpcode] = &EndDisplayList;
    gbi_->map[kCountedDisplayListOpcode] = &CountedDisplayList;
    gbi_->map[kDMAOffsetsOpcode] = &DMAOffsets;
    gbi_->map[kMoveWordOpcode] = &MoveWord;
    gbi_->map[kSetTextureImageOpcode] = &SetTextureImage;
    gbi_->map[kLoadBlockOpcode] = &LoadBlock;
    gbi_->map[kFillRectOpcode] = &FillRect;
}

dkr::runtime::F3DDKRRT64Bridge::~F3DDKRRT64Bridge() {
    if (active_ == this) {
        active_ = nullptr;
    }
    delete data_;
    delete gbi_;
}

void dkr::runtime::F3DDKRRT64Bridge::process(RT64::Application& application,
                                             const OSTask& task) {
    const auto process_start = std::chrono::steady_clock::now();
    active_ = this;
    StateData fresh{};
    fresh.task_count = data_->task_count + 1;
    *data_ = fresh;

    RT64::State* state = application.state.get();
    RT64::RSP& rsp = *state->rsp;
    rsp.reset();
    // Patch-pipeline HUD draws use RT64's standard extended alignment
    // commands. F3DDKR predates the extension handshake, so register the
    // otherwise-unused 0x64 opcode explicitly for each freshly reset task.
    state->enableExtendedGBI(0x64U);
    application.interpreter->hleGBI = gbi_;
    application.interpreter->UCode.textAddress = task.t.ucode & 0x00FFFFF8U;
    application.interpreter->UCode.dataAddress = task.t.ucode_data & 0x00FFFFF8U;
    rsp.setGBI(gbi_);
    RT64::GBI_F3D::reset(state);
    SelectInterpolationGroup(rsp, G_EX_ID_IGNORE);

    const auto identity = hlslpp::float4x4::identity();
    rsp.viewMatrixStack[0] = identity;
    rsp.projMatrixStack[0] = identity;
    rsp.viewProjMatrixStack[0] = identity;
    rsp.invViewProjMatrixStack[0] = identity;
    rsp.extended.viewMatrix = identity;
    rsp.extended.projMatrix = identity;
    rsp.extended.viewProjMatrix = identity;
    rsp.extended.invViewMatrix = identity;
    rsp.extended.invProjMatrix = identity;
    rsp.extended.invViewProjMatrix = identity;
    rsp.projectionMatrixChanged = true;
    rsp.modelViewProjChanged = true;

    const std::uint32_t start = PhysicalAddress(rsp, task.t.data_ptr);
    if (TaskTraceEnabled()) {
        std::fprintf(stderr, "[test][f3ddkr-task] begin=%llu dl=0x%06X\n",
                     static_cast<unsigned long long>(data_->task_count), start);
        std::fflush(stderr);
    }
    if (DeepTraceEnabled() && data_->task_count <= 6) {
        const auto* commands = reinterpret_cast<const RT64::DisplayList*>(
            application.core.RDRAM + start);
        std::fprintf(stderr, "[boot][f3ddkr][raw] task=%llu start=0x%06X",
                     static_cast<unsigned long long>(data_->task_count), start);
        for (std::uint32_t i = 0; i < 12; ++i) {
            std::fprintf(stderr, " %08X:%08X", commands[i].w0, commands[i].w1);
        }
        std::fputc('\n', stderr);
    }
    application.processDisplayLists(application.core.RDRAM, start, 0, true);
    if (data_->presentation_group_active) {
        std::fprintf(stderr,
                     "[boot][f3ddkr] unbalanced presentation group task=%llu begins=%u ends=%u\n",
                     static_cast<unsigned long long>(data_->task_count),
                     data_->presentation_group_begins,
                     data_->presentation_group_ends);
    }
    g_completed_tasks.store(data_->task_count, std::memory_order_release);
    if (TaskTraceEnabled()) {
        std::fprintf(stderr, "[test][f3ddkr-task] end=%llu\n",
                     static_cast<unsigned long long>(data_->task_count));
        std::fflush(stderr);
    }

    const auto process_end = std::chrono::steady_clock::now();
    const auto process_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(process_end - process_start)
            .count());
    ++g_profiled_tasks;
    g_profiled_microseconds += process_us;
    g_profiled_max_microseconds = std::max(g_profiled_max_microseconds, process_us);

    if ((DeepTraceEnabled() && data_->task_count <= 10) ||
        data_->task_count % 60 == 0) {
        std::fprintf(stderr,
                     "[boot][f3ddkr] task=%llu dl=0x%08X matrix-base=0x%06X "
                     "vertex-base=0x%06X commands=%u/%u/%u/%u groups=%u/%u "
                     "semantic=%u/%u\n",
                     static_cast<unsigned long long>(data_->task_count), start,
                     data_->matrix_offset, data_->vertex_offset, data_->matrix_commands,
                     data_->vertex_commands, data_->triangle_commands,
                     data_->counted_commands, data_->presentation_group_begins,
                     data_->presentation_group_ends,
                     data_->semantic_matrix_hits,
                     data_->semantic_matrix_misses);
        if (data_->task_count % 60 == 0) {
            std::fprintf(stderr,
                         "[boot][perf] tasks=%llu f3ddkr-avg-us=%llu max-us=%llu "
                         "video-delta=%u delta-limit=%u\n",
                         static_cast<unsigned long long>(g_profiled_tasks),
                         static_cast<unsigned long long>(
                             g_profiled_microseconds /
                             std::max<std::uint64_t>(g_profiled_tasks, 1U)),
                         static_cast<unsigned long long>(g_profiled_max_microseconds),
                         static_cast<unsigned>(ReadU8(application.core.RDRAM, 0x00126309U)),
                         static_cast<unsigned>(ReadU8(application.core.RDRAM, 0x001262E4U)));
        }
    }
}

void dkr::runtime::F3DDKRRT64Bridge::PresentationGroup(
    RT64::State* state, RT64::DisplayList** display_list) {
    const std::uint32_t mode = (*display_list)->w1 & 3U;
    const bool presentation_static = mode != 0U;
    StateData& data = *active_->data_;
    const bool ending_background_fill =
        mode == 0U && data.background_fill_stretch_active;
    if (mode == 3U) {
        // DKR's level colour is a real full-frame RDP fill emitted before the
        // world. Stretch only that rectangle to the host edges; the Z clear,
        // skydome, water, terrain, models, effects and HUD remain untouched.
        state->flush();
        state->rdp->setRectAspect(G_EX_ASPECT_STRETCH);
        data.background_fill_stretch_active = true;
    } else if (ending_background_fill) {
        state->flush();
        state->rdp->setRectAspect(G_EX_ASPECT_AUTO);
        data.background_fill_stretch_active = false;
    }

    if (presentation_static) {
        ++data.presentation_group_begins;
    } else {
        ++data.presentation_group_ends;
    }
    data.presentation_group_active = presentation_static;
    SelectInterpolationGroup(*state->rsp,
                             presentation_static
                                  ? G_EX_ID_IGNORE
                                  : data.current_matrix_group_id);
    // Force the next vertex batch to create a transform entry with the new
    // group even when DKR intentionally reuses the same matrix. This makes the
    // begin/end commands a strict draw-range scope instead of allowing a sky,
    // transition or menu background ID to bleed into decals or world objects.
    state->rsp->modelViewProjChanged = true;
    if (DeepTraceEnabled() && g_logged_presentation_groups++ < 32U) {
        const char* mode_name = mode == 3U ? "background-fill-stretch" :
            (presentation_static ? "static" : "world");
        std::fprintf(stderr,
                     "[boot][f3ddkr][presentation-group] task=%llu mode=%s begins=%u ends=%u\n",
                     static_cast<unsigned long long>(data.task_count),
                     mode_name,
                     data.presentation_group_begins, data.presentation_group_ends);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::Matrix(RT64::State* state,
                                            RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    const std::uint32_t w0 = (*display_list)->w0;
    if ((w0 & 0xFFFFU) != 64U) {
        return;
    }

    StateData& data = *active_->data_;
    std::uint32_t index = (w0 >> 16U) & 0xFU;
    if (index == 0) {
        index = (w0 >> 22U) & 0x3U;
    }
    index = std::min(index, 2U);
    data.selected_matrix = index;

    RT64::RSP& rsp = *state->rsp;
    rsp.modelMatrixStackSize = static_cast<int>(index + 1U);
    const std::uint32_t address =
        (data.matrix_offset + PhysicalAddress(rsp, (*display_list)->w1)) & kRDRAMAddressMask;
    if (address > kRDRAMSize - 64U) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected matrix address=0x%06X task=%llu\n",
                         address, static_cast<unsigned long long>(data.task_count));
        }
        return;
    }
    ++data.matrix_commands;
    if (DeepTraceEnabled() && g_logged_matrices++ < 32) {
        std::fprintf(stderr,
                     "[boot][f3ddkr][matrix] task=%llu index=%u address=0x%06X w0=%08X\n",
                     static_cast<unsigned long long>(data.task_count), index, address, w0);
        if (g_logged_matrices == 1) {
            LogFloatMatrix("perspective", state->RDRAM, 0x00120EE0U);
            LogFloatMatrix("view-projection", state->RDRAM, 0x00120F20U);
            LogFloatMatrix("current-model", state->RDRAM, 0x00121060U);
        }
    }
    data.current_matrix_group_id = G_EX_ID_IGNORE;
    if (!data.presentation_group_active &&
        dkr::runtime::enhancements::modern_presentation_enabled()) {
        data.current_matrix_group_id =
            dkr::runtime::presentation::matrix_identity(address);
        if (data.current_matrix_group_id == G_EX_ID_IGNORE) {
            ++data.semantic_matrix_misses;
        } else {
            ++data.semantic_matrix_hits;
        }
    }
    SelectInterpolationGroup(rsp, data.presentation_group_active
        ? G_EX_ID_IGNORE
        : data.current_matrix_group_id);
    rsp.matrix(address, static_cast<std::uint8_t>(
        active_->gbi_->constants[F3DENUM::G_MTX_LOAD]));
    if (DeepTraceEnabled() && g_logged_matrices <= 32) {
        const auto& matrix = rsp.modelMatrixStack[index];
        std::fprintf(stderr,
                     "[boot][f3ddkr][matrix-data] row0=(%.3f,%.3f,%.3f,%.3f) "
                     "row3=(%.3f,%.3f,%.3f,%.3f)\n",
                     matrix[0][0], matrix[0][1], matrix[0][2], matrix[0][3],
                     matrix[3][0], matrix[3][1], matrix[3][2], matrix[3][3]);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::FillRect(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (!data.background_fill_stretch_active) {
        RT64::GBI_RDP::fillRect(state, display_list);
        return;
    }

    // RT64's fill-cycle clear path intentionally ignores rectAspect. DKR uses
    // that path for the level-colour background beneath the skydome, so a
    // nominal 0..319 fill otherwise remains visibly boxed to 4:3 when the 3D
    // viewport expands. Anchor only this Patch-Pipeline-marked rectangle to the
    // host's left and right edges. Its vertical coordinates and fill colour are
    // preserved, and the Z clear, world, water, skydome, effects and HUD never
    // enter this scope.
    const std::int32_t uly = (*display_list)->p1(0, 12);
    const std::int32_t lry = (*display_list)->p0(0, 12);
    RT64::ExtendedAlignment alignment{};
    alignment.leftOrigin = G_EX_ORIGIN_LEFT;
    alignment.rightOrigin = G_EX_ORIGIN_RIGHT;
    state->rdp->fillRect(0, uly, 0, lry, alignment);

    if (!g_logged_background_fill_rect.exchange(true,
                                                  std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] fill-cycle background anchored to host edges y=%d..%d\n",
                     uly, lry);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::TextureOffset(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    data.texture_offset = (*display_list)->w1 & kRDRAMAddressMask;
    data.texture_shift = 0;
    data.texture_count = 0;
}

void dkr::runtime::F3DDKRRT64Bridge::Vertex(RT64::State* state,
                                            RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    RT64::RSP& rsp = *state->rsp;
    const std::uint32_t w0 = (*display_list)->w0;
    const bool append = (w0 & 0x00010000U) != 0;
    const std::uint32_t count = ((w0 >> 19U) & 0x1FU) + 1U;
    if (append) {
        if (data.billboard) {
            data.vertex_cursor = 1U;
        }
    } else {
        data.vertex_cursor = 0U;
    }
    const std::uint32_t destination =
        data.vertex_cursor + ((w0 >> 9U) & 0x1FU);
    if (count > kMaxDKRVertices || destination + count > RSP_MAX_VERTICES) {
        std::fprintf(stderr, "[boot][f3ddkr] rejected vertex range dst=%u count=%u\n",
                     destination, count);
        return;
    }

    const std::uint32_t source =
        (data.vertex_offset + PhysicalAddress(rsp, (*display_list)->w1)) & kRDRAMAddressMask;
    const std::uint64_t source_end = static_cast<std::uint64_t>(source) +
                                     static_cast<std::uint64_t>(count) * 10U;
    if (count == 0U || count > kMaxDKRVertices || source_end > kRDRAMSize ||
        destination > kMaxDKRVertices || count > kMaxDKRVertices - destination) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected vertex range task=%llu "
                         "source=0x%06X count=%u destination=%u append=%u\n",
                         static_cast<unsigned long long>(data.task_count), source,
                         count, destination, append ? 1U : 0U);
        }
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 10U;
        RT64::RSP::Vertex vertex{};
        vertex.x = ReadS16(state->RDRAM, address + 0U);
        vertex.y = ReadS16(state->RDRAM, address + 2U);
        vertex.z = ReadS16(state->RDRAM, address + 4U);
        vertex.color.r = ReadU8(state->RDRAM, address + 6U);
        vertex.color.g = ReadU8(state->RDRAM, address + 7U);
        vertex.color.b = ReadU8(state->RDRAM, address + 8U);
        vertex.color.a = ReadU8(state->RDRAM, address + 9U);
        std::memcpy(state->RDRAM + kScratchVertexAddress + i * sizeof(vertex),
                    &vertex, sizeof(vertex));
        if (DeepTraceEnabled() && g_logged_vertices < 48 && i == 0) {
            std::fprintf(stderr,
                         "[boot][f3ddkr][vertex] task=%llu src=0x%06X dst=%u count=%u "
                         "append=%u billboard=%u xyz=(%d,%d,%d) rgba=(%u,%u,%u,%u)\n",
                         static_cast<unsigned long long>(data.task_count), source,
                         destination, count, append ? 1U : 0U, data.billboard ? 1U : 0U,
                         vertex.x, vertex.y, vertex.z, vertex.color.r, vertex.color.g,
                         vertex.color.b, vertex.color.a);
        }
    }
    data.last_vertex_source = source;
    data.last_vertex_destination = destination;
    data.last_vertex_count = count;
    data.vertex_cursor += count;
    if (DeepTraceEnabled() && g_logged_vertices < 48) {
        ++g_logged_vertices;
    }
    ++data.vertex_commands;

    data.selected_matrix = std::min(data.selected_matrix, 2U);
    rsp.modelMatrixStackSize = static_cast<int>(data.selected_matrix + 1U);
    const auto original_matrix = rsp.modelMatrixStack[data.selected_matrix];
    bool adjusted_billboard = false;
    if (data.billboard && append && rsp.indices[0] <
        state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor]
            .drawData.posTransformed.size()) {
        const auto& workload =
            state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];
        const hlslpp::float4 anchor = workload.drawData.posTransformed[rsp.indices[0]];
        auto adjusted_matrix = original_matrix;
        adjusted_matrix[3] += anchor;
        rsp.modelMatrixStack[data.selected_matrix] = adjusted_matrix;
        rsp.modelViewProjChanged = true;
        adjusted_billboard = true;
    }

    rsp.setVertex(kScratchVertexAddress, count, destination);
    if (adjusted_billboard) {
        rsp.modelMatrixStack[data.selected_matrix] = original_matrix;
        rsp.modelViewProjChanged = true;
    }
}

void dkr::runtime::F3DDKRRT64Bridge::Triangle(RT64::State* state,
                                              RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    RT64::RSP& rsp = *state->rsp;
    const std::uint32_t w0 = (*display_list)->w0;
    const std::uint32_t count = ((w0 >> 20U) & 0xFU) + 1U;
    rsp.textureState.on = static_cast<std::uint8_t>((w0 >> 16U) & 0xFU);
    const std::uint32_t source = PhysicalAddress(rsp, (*display_list)->w1);
    const std::uint64_t source_end = static_cast<std::uint64_t>(source) +
                                     static_cast<std::uint64_t>(count) * 16U;
    if (count == 0U || source_end > kRDRAMSize) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected triangle range task=%llu "
                         "source=0x%06X count=%u\n",
                         static_cast<unsigned long long>(data.task_count), source,
                         count);
        }
        return;
    }

    // Validate the complete batch before changing RT64 state. F3DDKR has a
    // 32-entry vertex cache; an index outside it is stale/recycled data, not a
    // drawable triangle.
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 16U;
        const std::array<std::uint8_t, 3> vertices{
            ReadU8(state->RDRAM, address + 1U),
            ReadU8(state->RDRAM, address + 2U),
            ReadU8(state->RDRAM, address + 3U),
        };
        if (std::any_of(vertices.begin(), vertices.end(), [](std::uint8_t index) {
                return index >= kMaxDKRVertices;
            })) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] rejected triangle vertex index "
                             "task=%llu source=0x%06X triangle=%u "
                             "vertices=(%u,%u,%u)\n",
                             static_cast<unsigned long long>(data.task_count),
                             source, i, vertices[0], vertices[1], vertices[2]);
            }
            return;
        }
    }

    // The Golden Balloon's third sprite tile is the original 12x3 blue
    // knot/string tip. Identify it from the authored S10.5 UVs rather than the
    // G_SETTIMG width (DKR deliberately uploads sprite load blocks at width 1).
    // This is a bounded, opt-in diagnostic and does not alter rendering.
    if (SpriteTraceEnabled() && data.billboard && count == 2U &&
        data.traced_gold_knot_batches < 32U) {
        const auto authored_uv_matches = [&](std::uint32_t triangle) {
            const std::uint32_t address = source + triangle * 16U;
            std::array<std::int16_t, 3> s{
                ReadS16(state->RDRAM, address + 4U),
                ReadS16(state->RDRAM, address + 8U),
                ReadS16(state->RDRAM, address + 12U),
            };
            std::array<std::int16_t, 3> t{
                ReadS16(state->RDRAM, address + 6U),
                ReadS16(state->RDRAM, address + 10U),
                ReadS16(state->RDRAM, address + 14U),
            };
            std::sort(s.begin(), s.end());
            std::sort(t.begin(), t.end());
            return s.front() == 1 && s.back() == 352 &&
                   t.front() == 0 && t.back() == 64;
        };
        if (authored_uv_matches(0U) && authored_uv_matches(1U)) {
            std::FILE* trace = SpriteTraceStream();
            const std::uint32_t first_triangle = source;
            const std::array<std::uint8_t, 6> indices{
                ReadU8(state->RDRAM, first_triangle + 1U),
                ReadU8(state->RDRAM, first_triangle + 2U),
                ReadU8(state->RDRAM, first_triangle + 3U),
                ReadU8(state->RDRAM, first_triangle + 17U),
                ReadU8(state->RDRAM, first_triangle + 18U),
                ReadU8(state->RDRAM, first_triangle + 19U),
            };
            std::fprintf(trace,
                         "[test][f3ddkr-sprite] gold-knot task=%llu "
                         "material=0x%06X image=0x%06X triangles=0x%06X "
                         "texture=%u/%u/%u load=(tile=%u uls=%u ult=%u lrs=%u dxt=%u) "
                         "vertices=0x%06X dst=%u count=%u "
                         "indices=(%u,%u,%u)(%u,%u,%u)",
                         static_cast<unsigned long long>(data.task_count),
                         data.current_material_address,
                         data.current_texture_image, source,
                         data.current_texture_format,
                         data.current_texture_size,
                         data.current_texture_width,
                         data.current_load_tile,
                         data.current_load_uls,
                         data.current_load_ult,
                         data.current_load_lrs,
                         data.current_load_dxt,
                         data.last_vertex_source,
                         data.last_vertex_destination,
                         data.last_vertex_count,
                         indices[0], indices[1], indices[2],
                         indices[3], indices[4], indices[5]);
            for (const std::uint8_t index : indices) {
                if (index >= data.last_vertex_destination &&
                    index < data.last_vertex_destination + data.last_vertex_count) {
                    const std::uint32_t vertex_address = data.last_vertex_source +
                        (index - data.last_vertex_destination) * 10U;
                    std::fprintf(trace, " v%u=(%d,%d,%d)", index,
                                 ReadS16(state->RDRAM, vertex_address + 0U),
                                 ReadS16(state->RDRAM, vertex_address + 2U),
                                 ReadS16(state->RDRAM, vertex_address + 4U));
                }
            }
            std::fputc('\n', trace);
            if (data.current_material_count != 0U &&
                data.current_material_address <= kRDRAMSize -
                    data.current_material_count * sizeof(RT64::DisplayList)) {
                std::fprintf(trace, "[test][f3ddkr-sprite] material-commands");
                for (std::uint32_t i = 0; i < data.current_material_count; ++i) {
                    const auto* command = reinterpret_cast<const RT64::DisplayList*>(
                        state->fromRDRAM(data.current_material_address)) + i;
                    std::fprintf(trace, " %08X:%08X", command->w0, command->w1);
                }
                std::fputc('\n', trace);
            }
            std::fflush(trace);
            LogTextureContext(state->RDRAM, data.current_material_address,
                              data.task_count, true, trace);
            std::fflush(trace);
            ++data.traced_gold_knot_batches;
        }
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 16U;
        const std::uint8_t flag = ReadU8(state->RDRAM, address + 0U);
        const std::array<std::uint8_t, 3> vertices{
            ReadU8(state->RDRAM, address + 1U),
            ReadU8(state->RDRAM, address + 2U),
            ReadU8(state->RDRAM, address + 3U),
        };
        const std::array<std::int16_t, 3> s{
            ReadS16(state->RDRAM, address + 4U),
            ReadS16(state->RDRAM, address + 8U),
            ReadS16(state->RDRAM, address + 12U),
        };
        const std::array<std::int16_t, 3> t{
            ReadS16(state->RDRAM, address + 6U),
            ReadS16(state->RDRAM, address + 10U),
            ReadS16(state->RDRAM, address + 14U),
        };
        if (DeepTraceEnabled() && g_logged_triangles < 48 && i == 0) {
            std::fprintf(stderr,
                         "[boot][f3ddkr][tri] task=%llu src=0x%06X count=%u flag=%02X "
                         "v=(%u,%u,%u) st=((%d,%d),(%d,%d),(%d,%d))\n",
                         static_cast<unsigned long long>(data.task_count), source, count, flag,
                         vertices[0], vertices[1], vertices[2], s[0], t[0], s[1], t[1],
                         s[2], t[2]);
        }

        rsp.clearGeometryMode(rsp.cullBothMask);
        if ((flag & 0x40U) == 0) {
            const bool positive_x = rsp.viewportStack[rsp.viewportStackSize - 1].scale.x > 0.0F;
            rsp.setGeometryMode(positive_x ?
                active_->gbi_->constants[F3DENUM::G_CULL_BACK] :
                active_->gbi_->constants[F3DENUM::G_CULL_FRONT]);
        }

        for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
            const std::uint32_t texcoord =
                (static_cast<std::uint32_t>(static_cast<std::uint16_t>(s[corner])) << 16U) |
                static_cast<std::uint16_t>(t[corner]);
            rsp.modifyVertex(vertices[corner], G_MWO_POINT_ST, texcoord);
        }
        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
    }
    if (DeepTraceEnabled() && g_logged_triangles < 48) {
        ++g_logged_triangles;
    }
    ++data.triangle_commands;
    data.vertex_cursor = 0U;
}

void dkr::runtime::F3DDKRRT64Bridge::DisplayListBranch(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const bool branch = (*display_list)->p0(16, 1) != 0U;
    const std::uint32_t target =
        PhysicalAddress(*state->rsp, (*display_list)->w1) & 0x00FFFFF8U;
    if (target > kRDRAMSize - sizeof(RT64::DisplayList) ||
        (!branch && data.return_depth >= data.return_stack.size())) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected G_DL target=0x%06X "
                         "branch=%u depth=%u task=%llu\n",
                         target, branch ? 1U : 0U, data.return_depth,
                         static_cast<unsigned long long>(data.task_count));
        }
        *display_list = nullptr;
        return;
    }
    if (!branch) {
        data.return_stack[data.return_depth++] = *display_list;
    }
    *display_list = reinterpret_cast<RT64::DisplayList*>(state->fromRDRAM(target)) - 1;
}

void dkr::runtime::F3DDKRRT64Bridge::EndDisplayList(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (data.return_depth == 0U) {
        *display_list = nullptr;
    } else {
        *display_list = data.return_stack[--data.return_depth];
    }
}

void dkr::runtime::F3DDKRRT64Bridge::CountedDisplayList(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (data.nested_depth >= kMaxNestedDisplayLists) {
        std::fprintf(stderr, "[boot][f3ddkr] counted display-list nesting limit reached\n");
        return;
    }
    const std::uint32_t count = ((*display_list)->w0 >> 16U) & 0xFFU;
    const std::uint32_t address = PhysicalAddress(*state->rsp, (*display_list)->w1);
    ++data.counted_commands;
    if (DeepTraceEnabled() && g_logged_counted_lists < 256 &&
        (data.task_count <= 10 || data.selected_matrix == 2U || data.billboard)) {
        std::fprintf(stderr,
                     "[boot][f3ddkr][dmadl] task=%llu address=0x%06X count=%u "
                     "depth=%u matrix=%u billboard=%u first=%08X:%08X\n",
                     static_cast<unsigned long long>(data.task_count), address, count,
                     data.nested_depth, data.selected_matrix, data.billboard ? 1U : 0U,
                     count != 0U && address <= kRDRAMSize - sizeof(RT64::DisplayList)
                         ? reinterpret_cast<const RT64::DisplayList*>(
                               state->fromRDRAM(address))->w0
                         : 0U,
                     count != 0U && address <= kRDRAMSize - sizeof(RT64::DisplayList)
                         ? reinterpret_cast<const RT64::DisplayList*>(
                               state->fromRDRAM(address))->w1
                         : 0U);
        ++g_logged_counted_lists;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(address) +
                              static_cast<std::uint64_t>(count) *
                                  sizeof(RT64::DisplayList);
    if (count == 0U || address == 0U || end > kRDRAMSize) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected counted display-list "
                         "address=0x%06X count=%u end=0x%llX\n",
                         address, count, static_cast<unsigned long long>(end));
        }
        return;
    }

    // A G_DMADL is an inline block of already-built GBI commands. Validate the
    // whole block before mutating RT64 state: if DKR has recycled a texture or
    // a bad header points into texels, executing only its coincidentally valid
    // prefix can poison TMEM/TLUT state and crash at the next full sync.
    const auto* commands = reinterpret_cast<const RT64::DisplayList*>(
        state->fromRDRAM(address));
    data.current_material_address = address;
    data.current_material_count = count;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t opcode =
            static_cast<std::uint8_t>(commands[i].w0 >> 24U);
        if (!IsSafeCountedOpcode(opcode) ||
            active_->gbi_->map[opcode] == nullptr ||
            (opcode == kSetTextureImageOpcode &&
             (commands[i].w1 & ~kRDRAMAddressMask) != 0U)) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] rejected invalid counted block "
                             "opcode=0x%02X task=%llu address=0x%06X "
                             "index=%u/%u word=%08X:%08X\n",
                             opcode,
                             static_cast<unsigned long long>(data.task_count),
                             address, i, count, commands[i].w0, commands[i].w1);
            }
            LogTextureContext(state->RDRAM, address, data.task_count);
            return;
        }
    }
    ++data.nested_depth;
    RunCommands(state, reinterpret_cast<RT64::DisplayList*>(state->fromRDRAM(address)), count);
    --data.nested_depth;
}

void dkr::runtime::F3DDKRRT64Bridge::DMAOffsets(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    data.matrix_offset = (*display_list)->w0 & kRDRAMAddressMask;
    data.vertex_offset = (*display_list)->w1 & kRDRAMAddressMask;
    if (DeepTraceEnabled() && g_logged_offsets++ < 24) {
        std::fprintf(stderr,
                     "[boot][f3ddkr][offsets] task=%llu matrix=0x%06X vertex=0x%06X\n",
                     static_cast<unsigned long long>(data.task_count), data.matrix_offset,
                     data.vertex_offset);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::MoveWord(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t type = static_cast<std::uint8_t>((*display_list)->w0 & 0xFFU);
    if (type == kMoveWordPresentationGroup &&
        ((*display_list)->w1 & ~3U) == kPresentationGroupMagic) {
        PresentationGroup(state, display_list);
    } else if (type == kMoveWordBillboard) {
        data.billboard = ((*display_list)->w1 & 1U) != 0;
        if (DeepTraceEnabled() && g_logged_movewords++ < 24) {
            std::fprintf(stderr, "[boot][f3ddkr][billboard] task=%llu enabled=%u\n",
                         static_cast<unsigned long long>(data.task_count),
                         data.billboard ? 1U : 0U);
        }
    } else if (type == kMoveWordMVPMatrix) {
        data.selected_matrix = std::min(((*display_list)->w1 >> 6U) & 0x3U, 2U);
        state->rsp->modelMatrixStackSize = static_cast<int>(data.selected_matrix + 1U);
        state->rsp->modelViewProjChanged = true;
        if (DeepTraceEnabled() && g_logged_movewords++ < 24) {
            std::fprintf(stderr, "[boot][f3ddkr][matrix-select] task=%llu index=%u\n",
                         static_cast<unsigned long long>(data.task_count),
                         data.selected_matrix);
        }
    } else {
        RT64::GBI_F3D::moveWord(state, display_list);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::SetTextureImage(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t format = static_cast<std::uint8_t>((*display_list)->p0(21, 3));
    const std::uint8_t size = static_cast<std::uint8_t>((*display_list)->p0(19, 2));
    const std::uint16_t width = static_cast<std::uint16_t>((*display_list)->p0(0, 12) + 1U);
    std::uint32_t address = (*display_list)->w1 & kRDRAMAddressMask;
    if (data.texture_offset != 0) {
        if (format == G_IM_FMT_RGBA) {
            const std::uint64_t shift_address =
                static_cast<std::uint64_t>(data.texture_offset) +
                static_cast<std::uint64_t>(data.texture_count) *
                    sizeof(std::uint16_t);
            if (shift_address > kRDRAMSize - sizeof(std::uint16_t)) {
                if (g_logged_counted_errors++ < 64) {
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected texture-offset read "
                                 "task=%llu offset=0x%06X index=%u\n",
                                 static_cast<unsigned long long>(data.task_count),
                                 data.texture_offset, data.texture_count);
                }
                data.texture_offset = 0;
                data.texture_shift = 0;
                data.texture_count = 0;
                return;
            }
            data.texture_shift =
                ReadU16(state->RDRAM, static_cast<std::uint32_t>(shift_address));
            address = (address + data.texture_shift) & kRDRAMAddressMask;
        } else {
            data.texture_offset = 0;
            data.texture_shift = 0;
            data.texture_count = 0;
        }
    }
    state->rdp->setTextureImage(format, size, width, address);
    data.current_texture_image = address;
    data.current_texture_format = format;
    data.current_texture_size = size;
    data.current_texture_width = width;
}

void dkr::runtime::F3DDKRRT64Bridge::LoadBlock(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t tile = static_cast<std::uint8_t>((*display_list)->p1(24, 3));
    const std::uint16_t uls = static_cast<std::uint16_t>((*display_list)->p0(12, 12));
    const std::uint16_t ult = static_cast<std::uint16_t>((*display_list)->p0(0, 12));
    const std::uint16_t lrs = static_cast<std::uint16_t>((*display_list)->p1(12, 12));
    const std::uint16_t dxt = static_cast<std::uint16_t>((*display_list)->p1(0, 12));
    data.current_load_tile = tile;
    data.current_load_uls = uls;
    data.current_load_ult = ult;
    data.current_load_lrs = lrs;
    data.current_load_dxt = dxt;
    if (data.texture_offset != 0) {
        const std::uint32_t block_size = (((lrs >> 2U) + 1U) << 3U);
        if (block_size == 0 || (data.texture_shift % block_size) != 0) {
            state->rdp->texture.address -= data.texture_shift;
            data.texture_offset = 0;
            data.texture_shift = 0;
            data.texture_count = 0;
        } else {
            ++data.texture_count;
        }
    }
    state->rdp->loadBlock(tile, uls, ult, lrs, dxt);
}

void dkr::runtime::F3DDKRRT64Bridge::RunCommands(
    RT64::State* state, RT64::DisplayList* commands, std::uint32_t command_count) {
    assert(active_ != nullptr);
    std::uint32_t consumed = 0;
    RT64::DisplayList* command = commands;
    std::array<RT64::DisplayList*, kMaxNestedDisplayLists> return_stack{};
    std::uint32_t return_depth = 0;
    while (command != nullptr && consumed < command_count) {
        RT64::DisplayList* before = command;
        const std::uint8_t opcode = static_cast<std::uint8_t>(command->w0 >> 24U);
        if (opcode == kDisplayListOpcode) {
            const bool branch = command->p0(16, 1) != 0U;
            const std::uint32_t target =
                PhysicalAddress(*state->rsp, command->w1) & 0x00FFFFF8U;
            if (target > kRDRAMSize - sizeof(RT64::DisplayList) ||
                (!branch && return_depth >= return_stack.size())) {
                if (g_logged_counted_errors++ < 64) {
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected counted G_DL "
                                 "target=0x%06X branch=%u depth=%u\n",
                                 target, branch ? 1U : 0U, return_depth);
                }
                break;
            }
            if (!branch) {
                return_stack[return_depth++] = command;
            }
            command = reinterpret_cast<RT64::DisplayList*>(
                          state->fromRDRAM(target)) -
                      1;
        } else if (opcode == kEndDisplayListOpcode) {
            if (return_depth == 0U) {
                break;
            }
            command = return_stack[--return_depth];
        } else {
            RT64::GBIFunction function = active_->gbi_->map[opcode];
            if (function == nullptr) {
                if (g_logged_counted_errors++ < 64) {
                    const auto base_offset = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(commands) -
                        reinterpret_cast<std::uintptr_t>(state->RDRAM));
                    const auto command_offset = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(command) -
                        reinterpret_cast<std::uintptr_t>(state->RDRAM));
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected unknown counted opcode=0x%02X "
                                 "task=%llu base=0x%06X command=0x%06X "
                                 "word=%08X:%08X consumed=%u/%u\n",
                                 opcode,
                                 static_cast<unsigned long long>(active_->data_->task_count),
                                 base_offset, command_offset, command->w0, command->w1,
                                 consumed, command_count);
                }
                break;
            }
            function(state, &command);
        }
        if (command == nullptr) {
            break;
        }
        // Most GBI handlers leave the command pointer in place. A few consume
        // additional words, while G_DL/G_ENDDL can move it to an unrelated
        // display-list allocation. Pointer subtraction across those allocations
        // is undefined and previously let a branch turn the bounded DMA list
        // into an effectively unbounded walk through texture data.
        std::uint32_t command_words = 1U;
        const auto before_address = reinterpret_cast<std::uintptr_t>(before);
        const auto after_address = reinterpret_cast<std::uintptr_t>(command);
        const auto rdram_begin = reinterpret_cast<std::uintptr_t>(state->RDRAM);
        const auto rdram_end = rdram_begin + kRDRAMSize;
        if (after_address < rdram_begin ||
            after_address > rdram_end - sizeof(RT64::DisplayList)) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] counted display-list escaped RDRAM "
                             "opcode=0x%02X consumed=%u/%u\n",
                             opcode, consumed, command_count);
            }
            break;
        }
        if (after_address >= before_address) {
            const auto byte_advance = after_address - before_address;
            if (byte_advance <= 4U * sizeof(RT64::DisplayList) &&
                (byte_advance % sizeof(RT64::DisplayList)) == 0U) {
                command_words += static_cast<std::uint32_t>(
                    byte_advance / sizeof(RT64::DisplayList));
            }
        }
        consumed += command_words;
        ++command;
    }
}
