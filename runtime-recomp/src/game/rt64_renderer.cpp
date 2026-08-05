#include "rt64_renderer.hpp"

#include "game_registration.hpp"
#include "presentation_identity.hpp"
#include "renderer_snapshot.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_telemetry.hpp"
#include "runtime_platform.hpp"
#include "runtime_ui.hpp"

#if defined(_WIN32)
#include <Unknwn.h>
#include <oaidl.h>
#endif

#include "common/rt64_enhancement_configuration.h"
#include "common/rt64_user_configuration.h"
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <tuple>
#include <utility>

namespace {

static_assert(
    std::tuple_size_v<decltype(
        std::declval<RT64::WorkloadQueue>().workloads)> >= 4,
    "Modern presentation requires RT64's four-slot owned workload ring");

std::array<std::uint8_t, 0x40> g_rom_header{};
std::array<std::uint8_t, 0x1000> g_dmem{};
std::array<std::uint8_t, 0x1000> g_imem{};
std::uint32_t g_mi_interrupt = 0;
std::array<std::uint32_t, 8> g_dpc_registers{};
int g_requested_refresh_target = 30;
int g_effective_refresh_target = 30;
int g_detected_display_rate = 60;
// High-refresh matching remains isolated until it has passed full visual
// validation across menus, hubs, races and every vehicle type. The public
// Modern profile currently falls back to the proven native cadence; visible
// development checkpoints opt in explicitly.
bool ExperimentalInterpolationEnabled() {
    // Accurate is the immutable 30 Hz baseline. Modern is the explicit user
    // opt-in to RT64 presentation interpolation; its selected display/manual
    // target must work from the launcher without a private environment flag.
    return dkr::runtime::enhancements::modern_presentation_enabled();
}

bool ExperimentalSkipBufferingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DKR_INTERPOLATION_SKIP_BUFFERING");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return ExperimentalInterpolationEnabled() && enabled;
}

RT64::EnhancementConfiguration::Presentation::Mode PresentationMode() {
    return ExperimentalSkipBufferingEnabled()
        ? RT64::EnhancementConfiguration::Presentation::Mode::SkipBuffering
        : RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
}

void CheckInterrupts() {}

RT64::UserConfiguration::GraphicsAPI ToRT64(ultramodern::renderer::GraphicsApi api) {
    using UM = ultramodern::renderer::GraphicsApi;
    using RT = RT64::UserConfiguration::GraphicsAPI;
    switch (api) {
    case UM::D3D12: return RT::D3D12;
    case UM::Vulkan: return RT::Vulkan;
    case UM::Metal: return RT::Metal;
    default: return RT::Automatic;
    }
}

RT64::UserConfiguration::Antialiasing ToRT64(
    ultramodern::renderer::Antialiasing antialiasing) {
    using UM = ultramodern::renderer::Antialiasing;
    using RT = RT64::UserConfiguration::Antialiasing;
    switch (antialiasing) {
    case UM::MSAA2X: return RT::MSAA2X;
    case UM::MSAA4X: return RT::MSAA4X;
    case UM::MSAA8X: return RT::MSAA8X;
    default: return RT::None;
    }
}

RT64::UserConfiguration::AspectRatio ToRT64(
    ultramodern::renderer::AspectRatio aspect_ratio) {
    using UM = ultramodern::renderer::AspectRatio;
    using RT = RT64::UserConfiguration::AspectRatio;
    switch (aspect_ratio) {
    case UM::Expand: return RT::Expand;
    case UM::Manual: return RT::Manual;
    default: return RT::Original;
    }
}

int DetectDisplayRate() {
    auto* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return 60;
    }
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    if (display < 0 || SDL_GetCurrentDisplayMode(display, &mode) != 0 ||
        mode.refresh_rate <= 0) {
        return 60;
    }
    return dkr::runtime::enhancements::clamp_presentation_rate(mode.refresh_rate);
}

void ApplyConfig(RT64::Application& application,
                 const ultramodern::renderer::GraphicsConfig& config) {
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const auto effective_api = modern
        ? config.api_option
        : ultramodern::renderer::GraphicsApi::Auto;
    const auto effective_aspect = modern
        ? config.ar_option
        : ultramodern::renderer::AspectRatio::Original;
    dkr::runtime::enhancements::set_fit_to_window_enabled(
        modern && effective_aspect == ultramodern::renderer::AspectRatio::Expand);
    application.userConfig.graphicsAPI = ToRT64(effective_api);
    application.userConfig.antialiasing = ToRT64(config.msaa_option);
    application.userConfig.aspectRatio = ToRT64(effective_aspect);
    if (!modern || config.hr_option ==
            ultramodern::renderer::HUDRatioMode::Original) {
        application.userConfig.extAspectRatio =
            RT64::UserConfiguration::AspectRatio::Original;
    } else if (config.hr_option ==
               ultramodern::renderer::HUDRatioMode::Clamp16x9) {
        application.userConfig.extAspectRatio =
            RT64::UserConfiguration::AspectRatio::Manual;
        application.userConfig.extAspectTarget = 16.0 / 9.0;
    } else {
        application.userConfig.extAspectRatio =
            RT64::UserConfiguration::AspectRatio::Expand;
    }
    application.userConfig.resolution =
        config.res_option == ultramodern::renderer::Resolution::Auto
            ? RT64::UserConfiguration::Resolution::WindowIntegerScale
            : RT64::UserConfiguration::Resolution::Manual;
    application.userConfig.resolutionMultiplier =
        config.res_option == ultramodern::renderer::Resolution::Original2x
            ? 2.0 * std::max(config.ds_option, 1)
            : static_cast<double>(std::max(config.ds_option, 1));
    application.userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
    // Accurate is a hard renderer boundary, not a cosmetic launcher preset.
    // Modern may request presentation-only interpolation after DKR's sky,
    // transition, gradient and menu-background matrices have been explicitly
    // excluded by the custom F3DDKR bridge.
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        // RT64's swap-chain estimate can be implausibly high on hidden,
        // variable-refresh or newly-created Windows surfaces. That previously
        // let "Match display" saturate the GPU and stall the original 30 Hz
        // game producer. Resolve both Modern choices against SDL's active
        // desktop mode and send RT64 an explicit, bounded manual target.
        g_detected_display_rate = DetectDisplayRate();
        g_requested_refresh_target = config.rr_option ==
                ultramodern::renderer::RefreshRate::Manual
            ? dkr::runtime::enhancements::clamp_presentation_rate(
                  config.rr_manual_value)
            : g_detected_display_rate;
        if (ExperimentalInterpolationEnabled()) {
            // Match Display follows the active monitor. A deliberately chosen
            // manual rate remains deliberate, including rates above the
            // monitor refresh for latency testing; it is still bounded by the
            // public 30..500 FPS contract and RT64's paced presentation queue.
            // Manual 60 and Match Display are unchanged from the accepted
            // Modern-60 baseline.
            g_effective_refresh_target =
                dkr::runtime::enhancements::resolve_effective_presentation_rate(
                    dkr::runtime::enhancements::PresentationProfile::Modern,
                    config.rr_option ==
                        ultramodern::renderer::RefreshRate::Manual,
                    g_requested_refresh_target, g_detected_display_rate);
            application.userConfig.refreshRate =
                RT64::UserConfiguration::RefreshRate::Manual;
            application.userConfig.refreshRateTarget = g_effective_refresh_target;
        } else {
            g_effective_refresh_target = 30;
            application.userConfig.refreshRate =
                RT64::UserConfiguration::RefreshRate::Original;
            application.userConfig.refreshRateTarget = 30;
        }
    } else {
        g_requested_refresh_target = 30;
        g_effective_refresh_target = 30;
        g_detected_display_rate = DetectDisplayRate();
        application.userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        application.userConfig.refreshRateTarget = 30;
    }
    application.userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
    application.userConfig.internalColorFormat =
        config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::On
            ? RT64::UserConfiguration::InternalColorFormat::High
            : config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::Off
                ? RT64::UserConfiguration::InternalColorFormat::Standard
                : RT64::UserConfiguration::InternalColorFormat::Automatic;
}

ultramodern::renderer::SetupResult MapSetupResult(RT64::Application::SetupResult result) {
    using RT = RT64::Application::SetupResult;
    using UM = ultramodern::renderer::SetupResult;
    switch (result) {
    case RT::Success: return UM::Success;
    case RT::DynamicLibrariesNotFound: return UM::DynamicLibrariesNotFound;
    case RT::InvalidGraphicsAPI: return UM::InvalidGraphicsAPI;
    case RT::GraphicsAPINotFound: return UM::GraphicsAPINotFound;
    case RT::GraphicsDeviceNotFound: return UM::GraphicsDeviceNotFound;
    }
    return UM::GraphicsDeviceNotFound;
}

ultramodern::renderer::GraphicsApi MapGraphicsAPI(
    RT64::UserConfiguration::GraphicsAPI api) {
    using RT = RT64::UserConfiguration::GraphicsAPI;
    using UM = ultramodern::renderer::GraphicsApi;
    switch (api) {
    case RT::D3D12: return UM::D3D12;
    case RT::Vulkan: return UM::Vulkan;
    case RT::Metal: return UM::Metal;
    default: return UM::Auto;
    }
}

} // namespace

dkr::runtime::RT64Renderer::RT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    RT64::Application::Core core{};
#if defined(_WIN32)
    core.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    core.window = window_handle;
#elif defined(__APPLE__)
    core.window.window = window_handle.window;
    core.window.view = window_handle.view;
#endif
    core.checkInterrupts = CheckInterrupts;
    core.HEADER = g_rom_header.data();
    core.RDRAM = rdram;
    core.DMEM = g_dmem.data();
    core.IMEM = g_imem.data();
    core.MI_INTR_REG = &g_mi_interrupt;
    core.DPC_START_REG = &g_dpc_registers[0];
    core.DPC_END_REG = &g_dpc_registers[1];
    core.DPC_CURRENT_REG = &g_dpc_registers[2];
    core.DPC_STATUS_REG = &g_dpc_registers[3];
    core.DPC_CLOCK_REG = &g_dpc_registers[4];
    core.DPC_BUFBUSY_REG = &g_dpc_registers[5];
    core.DPC_PIPEBUSY_REG = &g_dpc_registers[6];
    core.DPC_TMEM_REG = &g_dpc_registers[7];

    auto* vi = ultramodern::renderer::get_vi_regs();
    core.VI_STATUS_REG = &vi->VI_STATUS_REG;
    core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
    core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
    core.VI_INTR_REG = &vi->VI_INTR_REG;
    core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
    core.VI_TIMING_REG = &vi->VI_TIMING_REG;
    core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
    core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
    core.VI_LEAP_REG = &vi->VI_LEAP_REG;
    core.VI_H_START_REG = &vi->VI_H_START_REG;
    core.VI_V_START_REG = &vi->VI_V_START_REG;
    core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
    core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
    core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

    RT64::ApplicationConfiguration application_config{};
    application_config.appId = "dkr-port";
    application_config.useConfigurationFile = false;
    application_config.detectDataPath = true;
    auto config = ultramodern::renderer::get_graphics_config();
    const auto create_application = [&] {
        application_ = std::make_unique<RT64::Application>(core, application_config);
        ApplyConfig(*application_, config);
        application_->userConfig.developerMode = developer_mode;
        // DKR renders a canonical 320x240 VI image. RT64's generic VI height
        // heuristic adds and rounds guard rows (often inferring 244), which
        // exposes the unused final rows as a thin bottom/right bar after Fit to
        // Window scaling. Present the authored 320x240 extent exactly.
        application_->enhancementConfig.presentation.removeBlackBorders = false;
        application_->enhancementConfig.rect.fixRectLR = true;
        // DKR presents directly from its alternating rendered color buffers.
        // SkipBuffering can select a stale VI-history entry before either buffer
        // has been approved for interpolation, yielding an entirely black Modern
        // frame. PresentEarly follows the current VI buffer and remains valid both
        // before and after RT64 enables interpolation for that framebuffer.
        application_->enhancementConfig.presentation.mode = PresentationMode();
    };
    create_application();
    // DKR presents directly from its alternating rendered color buffers.
    // SkipBuffering can select a stale VI-history entry before either buffer
    // has been approved for interpolation, yielding an entirely black Modern
    // frame. PresentEarly follows the current VI buffer and remains valid both
    // before and after RT64 enables interpolation for that framebuffer.
    std::uint32_t thread_id = 0;
#if defined(_WIN32)
    thread_id = window_handle.thread_id;
#endif
    setup_result = MapSetupResult(application_->setup(thread_id));
    chosen_api = MapGraphicsAPI(application_->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success &&
        config.api_option != ultramodern::renderer::GraphicsApi::Auto) {
        const auto failed_api = config.api_option;
        const auto failed_result = setup_result;
        std::fprintf(stderr,
                     "[boot][rt64] requested api=%u failed result=%u; retrying Automatic\n",
                     static_cast<unsigned>(failed_api),
                     static_cast<unsigned>(failed_result));
        // setup() can leave backend-owned objects partially initialised. A
        // clean Application is the only safe retry boundary.
        application_.reset();
        config.api_option = ultramodern::renderer::GraphicsApi::Auto;
        create_application();
        setup_result = MapSetupResult(application_->setup(thread_id));
        chosen_api = MapGraphicsAPI(application_->chosenGraphicsAPI);
        if (setup_result == ultramodern::renderer::SetupResult::Success) {
            dkr::runtime::ui::persist_graphics_api_fallback();
            std::fprintf(stderr,
                         "[boot][rt64] Automatic API recovery succeeded api=%u\n",
                         static_cast<unsigned>(chosen_api));
        }
    }
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        std::fprintf(stderr, "[boot][rt64] setup failed result=%u\n",
                     static_cast<unsigned>(setup_result));
        application_.reset();
        return;
    }
    application_->setFullScreen(
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    std::fprintf(stderr,
                 "[boot][rt64] initialized api=%u profile=%s refresh-mode=%u "
                 "requested=%d effective=%d display=%d\n",
                 static_cast<unsigned>(chosen_api),
                 dkr::runtime::enhancements::modern_presentation_enabled()
                     ? "Modern" : "Accurate",
                 static_cast<unsigned>(application_->userConfig.refreshRate),
                 g_requested_refresh_target, g_effective_refresh_target,
                 g_detected_display_rate);
}

dkr::runtime::RT64Renderer::~RT64Renderer() {
    if (application_ != nullptr) {
        dkr::runtime::ui::detach(*application_);
    }
}

bool dkr::runtime::RT64Renderer::valid() {
    return application_ != nullptr;
}

bool dkr::runtime::RT64Renderer::update_config(
    const ultramodern::renderer::GraphicsConfig& old_config,
    const ultramodern::renderer::GraphicsConfig& new_config) {
    if (application_ == nullptr || old_config == new_config) {
        return false;
    }
    if (old_config.wm_option != new_config.wm_option) {
        application_->setFullScreen(
            new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }
    ApplyConfig(*application_, new_config);
    application_->updateUserConfig(true);
    if (old_config.msaa_option != new_config.msaa_option) {
        application_->updateMultisampling();
    }
    return true;
}

void dkr::runtime::RT64Renderer::enable_instant_present() {
    if (application_ != nullptr) {
        application_->enhancementConfig.presentation.mode = PresentationMode();
        application_->updateEnhancementConfig();
    }
}

void dkr::runtime::RT64Renderer::send_dl(const OSTask* task,
                                         std::uint8_t* rdram_snapshot) {
    if (application_ == nullptr || rdram_snapshot == nullptr) {
        return;
    }
    dkr::runtime::telemetry::record_graphics_task();

    // A real RSP DMAs task inputs before notifying the CPU that it may recycle
    // them. DKR relies on that during scene transitions and can free texture
    // allocations while the host graphics queue is still pending. Parse this
    // task from the submission-time snapshot, then restore live RDRAM for VI.
    RendererSnapshotScope snapshot_scope(application_->core.RDRAM,
                                         application_->state->RDRAM,
                                         rdram_snapshot);
    dkr::runtime::presentation::TaskIdentityScope identity_scope(
        task->t.data_ptr);
    // DKR authors a new visual state at 30 Hz. Deriving that source cadence
    // from delayed VI history creates a positive feedback loop under load:
    // one late workload is misread as 20/15 Hz, RT64 schedules three or four
    // renders to catch up, and the extra work makes the next workload later.
    // Modern interpolation must keep the source contract stable and may skip
    // an optional intermediate when a scene exceeds its budget. Accurate mode
    // retains RT64's original VI-history behaviour unchanged.
    if (ExperimentalInterpolationEnabled()) {
        application_->state->setRefreshRate(30);
    }
    f3ddkr_.process(*application_, *task);
}

void dkr::runtime::RT64Renderer::update_screen() {
    if (application_ == nullptr) {
        return;
    }
    dkr::runtime::telemetry::record_vi_present();
    if (application_->sharedQueueResources != nullptr) {
        const std::uint64_t total = application_->sharedQueueResources->
            totalInterpolatedPresentations.load(std::memory_order_relaxed);
        if (total >= interpolated_present_count_) {
            dkr::runtime::telemetry::record_interpolated_presents(
                total - interpolated_present_count_);
        }
        interpolated_present_count_ = total;
    }
    dkr::runtime::telemetry::report_if_due();
    ++present_count_;
    if (present_count_ == 1) {
        std::fprintf(stderr, "[boot] VI initialized; starting recompiled DKR entrypoint\n");
        recomp::start_game(kGameId);
    }
    application_->updateScreen();
    // Preserve DKR's proven VI/DP scheduling path exactly; constructing the
    // next overlay frame after the game present keeps UI work out of the
    // original graphics-completion critical section.
    dkr::runtime::ui::draw(*application_);
    if (present_count_ <= 10 || present_count_ % 60 == 0) {
        std::fprintf(stderr, "[boot][vi] present=%llu\n",
                     static_cast<unsigned long long>(present_count_));
        if (present_count_ % 60 == 0 && application_->sharedQueueResources != nullptr) {
            auto& shared = *application_->sharedQueueResources;
            std::uint32_t original_rate = 0;
            std::uint32_t target_rate = 0;
            {
                std::scoped_lock configuration_lock(shared.configurationMutex);
                original_rate = shared.viOriginalRate;
                target_rate = shared.targetRate;
            }
            std::uint32_t interpolation_count = 0;
            std::uint32_t interpolation_available = 0;
            std::uint32_t interpolation_presented = 0;
            bool interpolation_skipped = false;
            std::uint32_t interpolation_bank = 0;
            std::uint32_t interpolation_counts[2]{};
            std::uint32_t interpolation_availables[2]{};
            std::uint32_t interpolation_presenteds[2]{};
            {
                std::scoped_lock interpolation_lock(shared.interpolatedMutex);
                interpolation_bank = shared.interpolatedFramesIndex;
                const auto& counters =
                    shared.interpolatedFrames[interpolation_bank];
                interpolation_count = counters.count;
                interpolation_available = counters.available;
                interpolation_presented = counters.presented;
                interpolation_skipped = counters.skipped;
                for (std::size_t bank = 0; bank < 2; ++bank) {
                    interpolation_counts[bank] =
                        shared.interpolatedFrames[bank].count;
                    interpolation_availables[bank] =
                        shared.interpolatedFrames[bank].available;
                    interpolation_presenteds[bank] =
                        shared.interpolatedFrames[bank].presented;
                }
            }
            std::uint32_t first_color_image = 0;
            std::size_t color_image_count = 0;
            std::uint32_t interpolation_eligible_count = 0;
            {
                std::scoped_lock manager_lock(shared.managerMutex);
                color_image_count = shared.colorImageAddressVector.size();
                if (!shared.colorImageAddressVector.empty()) {
                    first_color_image = shared.colorImageAddressVector.front();
                }
                for (const std::uint32_t color_address :
                     shared.colorImageAddressVector) {
                    const RT64::Framebuffer* framebuffer =
                        shared.framebufferManager.find(color_address);
                    if (framebuffer != nullptr && framebuffer->interpolationEnabled) {
                        ++interpolation_eligible_count;
                    }
                }
            }
            const std::uint32_t vi_origin = application_->core.VI_ORIGIN_REG != nullptr
                ? (*application_->core.VI_ORIGIN_REG & 0x00FFFFFFU)
                : 0U;
            std::fprintf(stderr,
                         "[boot][interpolation] original=%u target=%u count=%u "
                         "available=%u presented=%u skipped=%u targets=%zu "
                         "bank=%u banks=%u/%u/%u,%u/%u/%u "
                         "color-images=%zu eligible=%u first-color=0x%06X vi=0x%06X\n",
                         original_rate, target_rate, interpolation_count,
                         interpolation_available, interpolation_presented,
                         interpolation_skipped ? 1U : 0U,
                         shared.interpolatedColorTargets.size(), interpolation_bank,
                         interpolation_counts[0], interpolation_availables[0],
                         interpolation_presenteds[0], interpolation_counts[1],
                         interpolation_availables[1], interpolation_presenteds[1],
                         color_image_count, interpolation_eligible_count,
                         first_color_image, vi_origin);
        }
    }
}

void dkr::runtime::RT64Renderer::shutdown() {
    if (application_ != nullptr) {
        dkr::runtime::ui::detach(*application_);
        application_->end();
    }
}

std::uint32_t dkr::runtime::RT64Renderer::get_display_framerate() const {
    if (application_ == nullptr || application_->presentQueue == nullptr ||
        application_->presentQueue->ext.sharedResources == nullptr) {
        return 60;
    }
    return application_->presentQueue->ext.sharedResources->swapChainRate;
}

float dkr::runtime::RT64Renderer::get_resolution_scale() const {
    if (application_ == nullptr) {
        return 1.0F;
    }
    if (application_->userConfig.resolution ==
        RT64::UserConfiguration::Resolution::Manual) {
        return static_cast<float>(application_->userConfig.resolutionMultiplier);
    }
    constexpr std::uint32_t kReferenceHeight = 240;
    const std::uint32_t height = application_->sharedQueueResources->swapChainHeight;
    return height > 0
        ? static_cast<float>(std::max((height + kReferenceHeight - 1U) / kReferenceHeight, 1U))
        : 1.0F;
}

std::unique_ptr<ultramodern::renderer::RendererContext>
dkr::runtime::CreateRT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    return std::make_unique<RT64Renderer>(rdram, window_handle, developer_mode);
}
