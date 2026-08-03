#include "runtime_ui.hpp"

#include "game_registration.hpp"
#include "generated/racing_banana_font.h"
#include "runtime_enhancements.hpp"
#include "runtime_audio_controls.hpp"
#include "runtime_input.hpp"
#include "runtime_platform.hpp"
#include "save_manager.hpp"
#include "virtual_pak.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Unknwn.h>
#include <oaidl.h>
#endif

#include "gui/rt64_inspector.h"
#include "hle/rt64_application.h"
#include "hle/rt64_present_queue.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include "nfd.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using ultramodern::renderer::Antialiasing;
using ultramodern::renderer::AspectRatio;
using ultramodern::renderer::GraphicsApi;
using ultramodern::renderer::GraphicsConfig;
using ultramodern::renderer::HighPrecisionFramebuffer;
using ultramodern::renderer::HUDRatioMode;
using ultramodern::renderer::Resolution;
using ultramodern::renderer::RefreshRate;
using ultramodern::renderer::WindowMode;

std::filesystem::path g_config_directory;
std::atomic<bool> g_overlay_visible{false};
std::atomic<bool> g_overlay_dirty{false};
std::atomic<bool> g_logged_overlay_frame{false};
std::atomic<bool> g_logged_exit_button_rect{false};
std::atomic<bool> g_logged_exit_confirm_rect{false};
std::atomic<bool> g_exit_requested{false};
std::mutex g_inspector_guard;
RT64::Inspector* g_inspector = nullptr;
std::atomic<int> g_overlay_page{0};
ImFont* g_font_heading = nullptr;
ImFont* g_font_title = nullptr;
RefreshRate g_modern_refresh_mode = RefreshRate::Display;
int g_modern_refresh_target = 60;
Resolution g_modern_resolution = Resolution::Auto;
AspectRatio g_modern_aspect = AspectRatio::Expand;
Antialiasing g_modern_antialiasing = Antialiasing::None;
HighPrecisionFramebuffer g_modern_high_precision_fb = HighPrecisionFramebuffer::On;
GraphicsApi g_modern_graphics_api = GraphicsApi::Auto;
HUDRatioMode g_modern_hud_ratio = HUDRatioMode::Clamp16x9;
int g_modern_downsample = 1;
enum class CaptureDevice { None, Keyboard, Controller };
CaptureDevice g_capture_device = CaptureDevice::None;
int g_capture_action = -1;
bool g_capture_popup_pending = false;
bool g_capture_finished = false;
std::string g_save_manager_status;

bool HandleInputCaptureEvent(SDL_Event* event);

struct BrowserEntry {
    std::filesystem::path path;
    bool directory = false;
};

struct RomBrowserState {
    bool open = false;
    bool close_requested = false;
    bool focus_first_entry = false;
    std::filesystem::path directory;
    std::vector<BrowserEntry> entries;
    std::string message;
};

RomBrowserState g_rom_browser;

constexpr ImVec4 kBackground{0.02F, 0.20F, 0.45F, 1.0F};
constexpr ImVec4 kPanel{0.025F, 0.20F, 0.43F, 1.0F};
constexpr ImVec4 kPanelSoft{0.05F, 0.34F, 0.62F, 1.0F};
constexpr ImVec4 kText{1.0F, 0.965F, 0.855F, 1.0F};
constexpr ImVec4 kMuted{0.69F, 0.79F, 0.80F, 1.0F};
constexpr ImVec4 kAccent{0.10F, 0.76F, 0.64F, 1.0F};
constexpr ImVec4 kWarm{1.0F, 0.67F, 0.08F, 1.0F};
constexpr ImVec4 kRaceRed{0.91F, 0.18F, 0.13F, 1.0F};
constexpr ImVec4 kRaceBlue{0.04F, 0.43F, 0.63F, 1.0F};
constexpr ImVec4 kCream{1.0F, 0.94F, 0.76F, 1.0F};

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::filesystem::path SettingsPath() {
    return g_config_directory / "dkr-port-settings.ini";
}

std::filesystem::path LastRomPath() {
    return g_config_directory / "last-rom.txt";
}

bool ReplaceSettingsFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.ChildRounding = 18.0F;
    style.FrameRounding = 12.0F;
    style.PopupRounding = 16.0F;
    style.ScrollbarRounding = 12.0F;
    style.GrabRounding = 12.0F;
    style.WindowPadding = {0.0F, 0.0F};
    style.FramePadding = {16.0F, 11.0F};
    style.ItemSpacing = {12.0F, 13.0F};
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 2.0F;
    style.FrameBorderSize = 1.0F;
    style.Colors[ImGuiCol_WindowBg] = kBackground;
    style.Colors[ImGuiCol_ChildBg] = kPanel;
    style.Colors[ImGuiCol_Border] = {0.16F, 0.42F, 0.44F, 1.0F};
    style.Colors[ImGuiCol_Text] = kText;
    style.Colors[ImGuiCol_TextDisabled] = kMuted;
    style.Colors[ImGuiCol_FrameBg] = kPanelSoft;
    style.Colors[ImGuiCol_FrameBgHovered] = {0.09F, 0.28F, 0.33F, 1.0F};
    style.Colors[ImGuiCol_FrameBgActive] = {0.10F, 0.36F, 0.38F, 1.0F};
    style.Colors[ImGuiCol_Button] = kRaceBlue;
    style.Colors[ImGuiCol_ButtonHovered] = {0.98F, 0.43F, 0.08F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = kRaceRed;
    style.Colors[ImGuiCol_CheckMark] = kAccent;
    style.Colors[ImGuiCol_SliderGrab] = kWarm;
    style.Colors[ImGuiCol_SliderGrabActive] = kRaceRed;
    style.Colors[ImGuiCol_Header] = {0.04F, 0.38F, 0.43F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = {0.98F, 0.43F, 0.08F, 1.0F};
    style.Colors[ImGuiCol_HeaderActive] = kRaceRed;
    style.Colors[ImGuiCol_Separator] = {0.95F, 0.55F, 0.08F, 0.75F};
    // Controller navigation must read as a deliberate selection, even over
    // the blue half of the racing backdrop. Warm yellow is shared with the
    // start lights and remains distinct from every card and button colour.
    style.Colors[ImGuiCol_NavHighlight] = {1.0F, 0.82F, 0.12F, 1.0F};
}

void LoadRacingFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig body_config{};
    body_config.SizePixels = 17.0F;
    ImFont* body_font = io.Fonts->AddFontDefault(&body_config);
    io.FontDefault = body_font;
    static constexpr ImWchar kPunctuationRanges[] = {
        0x0021, 0x002F, // ! " # $ % & ' ( ) * + , - . /
        0x003A, 0x0040, // : ; < = > ? @
        0x005B, 0x0060, // [ \ ] ^ _ `
        0x007B, 0x007E, // { | } ~
        0x00A3, 0x00A3, // Pound sign
        0,
    };
    const auto add_racing_font = [&](float size) {
        ImFontConfig racing_config{};
        racing_config.FontDataOwnedByAtlas = false;
        racing_config.OversampleH = 2;
        racing_config.OversampleV = 2;
        ImFont* racing_font = io.Fonts->AddFontFromMemoryTTF(
            const_cast<char*>(dkr_racing_banana_font),
            static_cast<int>(dkr_racing_banana_font_size), size, &racing_config);
        if (racing_font != nullptr) {
            // Racing Banana intentionally contains a small display alphabet.
            // Merge ImGui's embedded cross-platform font only for punctuation
            // it lacks, at the same pixel height and increased raster weight.
            ImFontConfig fallback_config{};
            fallback_config.MergeMode = true;
            fallback_config.PixelSnapH = true;
            fallback_config.OversampleH = 1;
            fallback_config.OversampleV = 1;
            fallback_config.SizePixels = size;
            fallback_config.RasterizerMultiply = 1.35F;
            fallback_config.GlyphRanges = kPunctuationRanges;
            io.Fonts->AddFontDefault(&fallback_config);
        }
        return racing_font;
    };
    g_font_heading = add_racing_font(28.0F);
    g_font_title = add_racing_font(48.0F);
    if (g_font_heading == nullptr || g_font_title == nullptr) {
        std::fprintf(stderr, "[boot][ui] Racing Banana font could not be loaded; using fallback\n");
        g_font_heading = body_font;
        g_font_title = body_font;
    }
}

void PushHeadingFont(bool title = false) {
    ImFont* font = title ? g_font_title : g_font_heading;
    if (font != nullptr) {
        ImGui::PushFont(font);
    }
}

void PopHeadingFont(bool title = false) {
    if ((title ? g_font_title : g_font_heading) != nullptr) {
        ImGui::PopFont();
    }
}

void RememberModernGraphics(const GraphicsConfig& config) {
    g_modern_resolution = config.res_option;
    g_modern_aspect = config.ar_option == AspectRatio::Original
        ? AspectRatio::Original
        : AspectRatio::Expand;
    g_modern_antialiasing = config.msaa_option;
    g_modern_high_precision_fb = config.hpfb_option;
    g_modern_graphics_api = config.api_option;
    g_modern_hud_ratio = config.hr_option == HUDRatioMode::Original
        ? HUDRatioMode::Original
        : config.hr_option == HUDRatioMode::Full
            ? HUDRatioMode::Full
            : HUDRatioMode::Clamp16x9;
    g_modern_downsample = std::clamp(config.ds_option, 1, 8);
    if (config.rr_option == RefreshRate::Display ||
        config.rr_option == RefreshRate::Manual) {
        g_modern_refresh_mode = config.rr_option;
        g_modern_refresh_target =
            dkr::runtime::enhancements::clamp_presentation_rate(
                config.rr_manual_value);
    }
}

void ApplyProfileGraphics(GraphicsConfig& config,
                          dkr::runtime::enhancements::PresentationProfile profile) {
    if (profile == dkr::runtime::enhancements::PresentationProfile::Modern) {
        config.res_option = g_modern_resolution;
        config.ar_option = g_modern_aspect;
        config.msaa_option = g_modern_antialiasing;
        config.hpfb_option = g_modern_high_precision_fb;
        config.api_option = g_modern_graphics_api;
        config.hr_option = g_modern_hud_ratio;
        config.ds_option = g_modern_downsample;
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
        return;
    }

    // Accurate is enforced again inside the renderer. These launcher values
    // make the effective state explicit and ensure stale Modern preferences do
    // not leak into a pre-launch configuration snapshot.
    config.res_option = Resolution::Auto;
    config.ar_option = AspectRatio::Original;
    config.msaa_option = Antialiasing::None;
    config.hpfb_option = HighPrecisionFramebuffer::Auto;
    config.api_option = GraphicsApi::Auto;
    config.hr_option = HUDRatioMode::Original;
    config.ds_option = 1;
    config.rr_option = RefreshRate::Original;
    config.rr_manual_value = 30;
}

void SaveSettings() {
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    if (error) {
        std::fprintf(stderr, "[boot][settings] failed to create settings directory: %s\n",
                     error.message().c_str());
        return;
    }
    const auto& config = ultramodern::renderer::get_graphics_config();
    const std::filesystem::path settings_path = SettingsPath();
    const std::filesystem::path temporary_path = settings_path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            std::fprintf(stderr, "[boot][settings] failed to open temporary settings file\n");
            return;
        }
        output << "settings_version="
               << dkr::runtime::enhancements::kCurrentSettingsVersion << '\n';
        output << "presentation_profile="
               << static_cast<int>(dkr::runtime::enhancements::presentation_profile()) << '\n';
        output << "window_mode=" << static_cast<int>(config.wm_option) << '\n';
        output << "resolution=" << static_cast<int>(config.res_option) << '\n';
        output << "aspect=" << static_cast<int>(config.ar_option) << '\n';
        output << "antialiasing=" << static_cast<int>(config.msaa_option) << '\n';
        output << "high_precision_fb=" << static_cast<int>(config.hpfb_option) << '\n';
        output << "graphics_api=" << static_cast<int>(config.api_option) << '\n';
        output << "hud_ratio=" << static_cast<int>(config.hr_option) << '\n';
        output << "refresh_rate=" << static_cast<int>(config.rr_option) << '\n';
        output << "refresh_rate_target="
               << dkr::runtime::enhancements::clamp_presentation_rate(
                      config.rr_manual_value) << '\n';
        output << "modern_refresh_rate=" << static_cast<int>(g_modern_refresh_mode) << '\n';
        output << "modern_refresh_target="
               << dkr::runtime::enhancements::clamp_presentation_rate(
                      g_modern_refresh_target) << '\n';
        output << "modern_resolution=" << static_cast<int>(g_modern_resolution) << '\n';
        output << "modern_aspect=" << static_cast<int>(g_modern_aspect) << '\n';
        output << "modern_antialiasing="
               << static_cast<int>(g_modern_antialiasing) << '\n';
        output << "modern_high_precision_fb="
               << static_cast<int>(g_modern_high_precision_fb) << '\n';
        output << "modern_graphics_api="
               << static_cast<int>(g_modern_graphics_api) << '\n';
        output << "modern_hud_ratio="
               << static_cast<int>(g_modern_hud_ratio) << '\n';
        output << "modern_downsample=" << g_modern_downsample << '\n';
        output << "master_volume=" << dkr::runtime::platform::master_volume() << '\n';
        output << "music_volume=" << dkr::runtime::audio::music_volume() << '\n';
        output << "sound_effects_volume=" << dkr::runtime::audio::sound_effects_volume() << '\n';
        output << "vehicle_volume=" << dkr::runtime::audio::vehicle_volume() << '\n';
        output << "eq_bass=" << dkr::runtime::platform::bass_gain() << '\n';
        output << "eq_mid=" << dkr::runtime::platform::mid_gain() << '\n';
        output << "eq_treble=" << dkr::runtime::platform::treble_gain() << '\n';
        output << "maximum_detail="
               << (dkr::runtime::enhancements::maximum_detail_requested() ? 1 : 0) << '\n';
        output << "modern_fov_offset="
               << dkr::runtime::enhancements::fov_offset() << '\n';
        output << "modern_view_distance_multiplier="
               << dkr::runtime::enhancements::view_distance_multiplier() << '\n';
        output << "modern_extended_culling="
               << (dkr::runtime::enhancements::extended_culling_requested() ? 1 : 0)
               << '\n';
        output << "modern_frustum_guard_percent="
               << dkr::runtime::enhancements::frustum_guard_percent() << '\n';
        output << "memory_pak=" << (dkr::runtime::pak::enabled() ? 1 : 0) << '\n';
        output << "rumble=" << (dkr::runtime::platform::rumble_enabled() ? 1 : 0) << '\n';
        output << "rumble_strength=" << dkr::runtime::platform::rumble_strength() << '\n';
        output << "stick_deadzone=" << dkr::runtime::input::stick_deadzone() << '\n';
        output << "stick_anti_deadzone=" << dkr::runtime::input::stick_anti_deadzone() << '\n';
        output << "stick_sensitivity=" << dkr::runtime::input::stick_sensitivity() << '\n';
        output << "stick_curve=" << dkr::runtime::input::stick_curve() << '\n';
        output << "stick_x_inverted=" << (dkr::runtime::input::stick_x_inverted() ? 1 : 0) << '\n';
        output << "stick_y_inverted=" << (dkr::runtime::input::stick_y_inverted() ? 1 : 0) << '\n';
        output << "trigger_threshold=" << dkr::runtime::input::trigger_threshold() << '\n';
        output << "gyro_enabled=" << (dkr::runtime::input::gyro_enabled() ? 1 : 0) << '\n';
        output << "gyro_sensitivity=" << dkr::runtime::input::gyro_sensitivity() << '\n';
        output << "gyro_deadzone=" << dkr::runtime::input::gyro_deadzone() << '\n';
        output << "gyro_inverted=" << (dkr::runtime::input::gyro_inverted() ? 1 : 0) << '\n';
        output << "gyro_axis=" << static_cast<int>(dkr::runtime::input::gyro_axis()) << '\n';
        for (std::size_t index = 0; index < dkr::runtime::input::action_count(); ++index) {
            const auto action = static_cast<dkr::runtime::input::Action>(index);
            output << "keyboard_binding." << dkr::runtime::input::action_identifier(action)
                   << '=' << dkr::runtime::input::keyboard_binding(action) << '\n';
            output << "controller_binding." << dkr::runtime::input::action_identifier(action)
                   << '=' << dkr::runtime::input::controller_binding(action) << '\n';
        }
        // This must be the final record. A truncated settings file is never allowed
        // to reactivate experimental presentation features.
        output << "settings_complete=1\n";
        output.flush();
        if (!output) {
            std::fprintf(stderr, "[boot][settings] failed while writing settings\n");
            output.close();
            std::filesystem::remove(temporary_path, error);
            return;
        }
    }
    if (!ReplaceSettingsFile(temporary_path, settings_path)) {
        std::fprintf(stderr, "[boot][settings] failed to replace settings file\n");
        std::filesystem::remove(temporary_path, error);
    }
}

void LoadSettings() {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    int settings_version = 0;
    bool settings_complete = false;
    auto profile = dkr::runtime::enhancements::PresentationProfile::Accurate;
    bool profile_value_valid = true;
    bool migrated = false;
    std::ifstream input(SettingsPath());
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        try {
            const int number = std::stoi(value);
            if (key == "settings_version") {
                settings_version = number;
            } else if (key == "settings_complete") {
                settings_complete = number == 1;
            } else if (key == "presentation_profile") {
                profile = dkr::runtime::enhancements::normalise_presentation_profile(number);
                profile_value_valid =
                    number == static_cast<int>(
                        dkr::runtime::enhancements::PresentationProfile::Accurate) ||
                    number == static_cast<int>(
                        dkr::runtime::enhancements::PresentationProfile::Modern);
            } else if (key == "window_mode" && number >= 0 && number < 2) {
                config.wm_option = static_cast<WindowMode>(number);
            } else if (key == "resolution" && number >= 0 && number < 3) {
                config.res_option = static_cast<Resolution>(number);
            } else if (key == "aspect" && number >= 0 && number < 2) {
                config.ar_option = static_cast<AspectRatio>(number);
            } else if (key == "antialiasing" && number >= 0 && number < 4) {
                config.msaa_option = static_cast<Antialiasing>(number);
            } else if (key == "high_precision_fb" && number >= 0 && number < 3) {
                config.hpfb_option = static_cast<HighPrecisionFramebuffer>(number);
            } else if (key == "graphics_api" && number >= 0 && number < 4) {
                config.api_option = static_cast<GraphicsApi>(number);
            } else if (key == "hud_ratio" && number >= 0 && number < 3) {
                config.hr_option = static_cast<HUDRatioMode>(number);
            } else if (key == "refresh_rate" && number >= 0 && number < 3) {
                config.rr_option = static_cast<RefreshRate>(number);
            } else if (key == "refresh_rate_target") {
                config.rr_manual_value =
                    dkr::runtime::enhancements::clamp_presentation_rate(number);
            } else if (key == "modern_refresh_rate" &&
                       number >= static_cast<int>(RefreshRate::Display) &&
                       number <= static_cast<int>(RefreshRate::Manual)) {
                g_modern_refresh_mode = static_cast<RefreshRate>(number);
            } else if (key == "modern_refresh_target") {
                g_modern_refresh_target =
                    dkr::runtime::enhancements::clamp_presentation_rate(number);
            } else if (key == "modern_resolution" && number >= 0 && number < 3) {
                g_modern_resolution = static_cast<Resolution>(number);
            } else if (key == "modern_aspect" && number >= 0 && number < 2) {
                g_modern_aspect = static_cast<AspectRatio>(number);
            } else if (key == "modern_antialiasing" && number >= 0 && number < 4) {
                g_modern_antialiasing = static_cast<Antialiasing>(number);
            } else if (key == "modern_high_precision_fb" && number >= 0 && number < 3) {
                g_modern_high_precision_fb =
                    static_cast<HighPrecisionFramebuffer>(number);
            } else if (key == "modern_graphics_api" && number >= 0 && number < 4) {
                g_modern_graphics_api = static_cast<GraphicsApi>(number);
            } else if (key == "modern_hud_ratio" && number >= 0 && number < 3) {
                g_modern_hud_ratio = static_cast<HUDRatioMode>(number);
            } else if (key == "modern_downsample") {
                g_modern_downsample = std::clamp(number, 1, 8);
            } else if (key == "master_volume") {
                dkr::runtime::platform::set_master_volume(std::stof(value));
            } else if (key == "music_volume") {
                dkr::runtime::audio::set_music_volume(std::stof(value));
            } else if (key == "sound_effects_volume") {
                dkr::runtime::audio::set_sound_effects_volume(std::stof(value));
            } else if (key == "vehicle_volume") {
                dkr::runtime::audio::set_vehicle_volume(std::stof(value));
            } else if (key == "eq_bass") {
                dkr::runtime::platform::set_bass_gain(std::stof(value));
            } else if (key == "eq_mid") {
                dkr::runtime::platform::set_mid_gain(std::stof(value));
            } else if (key == "eq_treble") {
                dkr::runtime::platform::set_treble_gain(std::stof(value));
            } else if (key == "maximum_detail") {
                dkr::runtime::enhancements::set_maximum_detail_enabled(number != 0);
            } else if (key == "modern_fov_offset") {
                dkr::runtime::enhancements::set_fov_offset(number);
            } else if (key == "modern_view_distance_multiplier") {
                dkr::runtime::enhancements::set_view_distance_multiplier(number);
            } else if (key == "modern_extended_culling") {
                dkr::runtime::enhancements::set_extended_culling_enabled(number != 0);
            } else if (key == "modern_frustum_guard_percent") {
                dkr::runtime::enhancements::set_frustum_guard_percent(number);
            } else if (key == "memory_pak") {
                dkr::runtime::pak::set_enabled(number != 0);
            } else if (key == "rumble") {
                dkr::runtime::platform::set_rumble_enabled(number != 0);
            } else if (key == "rumble_strength") {
                dkr::runtime::platform::set_rumble_strength(std::stof(value));
            } else if (key == "stick_deadzone") {
                dkr::runtime::input::set_stick_deadzone(std::stof(value));
            } else if (key == "stick_anti_deadzone") {
                dkr::runtime::input::set_stick_anti_deadzone(std::stof(value));
            } else if (key == "stick_sensitivity") {
                dkr::runtime::input::set_stick_sensitivity(std::stof(value));
            } else if (key == "stick_curve") {
                dkr::runtime::input::set_stick_curve(std::stof(value));
            } else if (key == "stick_x_inverted") {
                dkr::runtime::input::set_stick_x_inverted(number != 0);
            } else if (key == "stick_y_inverted") {
                dkr::runtime::input::set_stick_y_inverted(number != 0);
            } else if (key == "trigger_threshold") {
                dkr::runtime::input::set_trigger_threshold(std::stof(value));
            } else if (key == "gyro_enabled") {
                dkr::runtime::input::set_gyro_enabled(number != 0);
            } else if (key == "gyro_sensitivity") {
                dkr::runtime::input::set_gyro_sensitivity(std::stof(value));
            } else if (key == "gyro_deadzone") {
                dkr::runtime::input::set_gyro_deadzone(std::stof(value));
            } else if (key == "gyro_inverted") {
                dkr::runtime::input::set_gyro_inverted(number != 0);
            } else if (key == "gyro_axis") {
                dkr::runtime::input::set_gyro_axis(
                    number == static_cast<int>(dkr::runtime::input::GyroAxis::Yaw)
                        ? dkr::runtime::input::GyroAxis::Yaw
                        : dkr::runtime::input::GyroAxis::Roll);
            } else if (key.rfind("keyboard_binding.", 0) == 0 ||
                       key.rfind("controller_binding.", 0) == 0) {
                const bool keyboard = key.rfind("keyboard_binding.", 0) == 0;
                const std::size_t prefix = keyboard ? 17U : 19U;
                const std::string identifier = key.substr(prefix);
                for (std::size_t index = 0;
                     index < dkr::runtime::input::action_count(); ++index) {
                    const auto action = static_cast<dkr::runtime::input::Action>(index);
                    if (identifier == dkr::runtime::input::action_identifier(action)) {
                        if (keyboard) {
                            dkr::runtime::input::set_keyboard_binding(action, number);
                        } else {
                            dkr::runtime::input::set_controller_binding(action, number);
                        }
                        break;
                    }
                }
            }
        } catch (...) {
            std::fprintf(stderr, "[boot][settings] ignored malformed setting %s\n", key.c_str());
        }
    }
    // Windows cannot atomically replace the settings file while this reader
    // still owns an open handle. Migration may call SaveSettings below, so
    // release the read handle before attempting that replacement.
    input.close();
    // Early launcher builds defaulted to 8x MSAA, which can turn busy races
    // GPU-bound at high desktop resolutions. Migrate that one legacy default
    // to 2x; users can still explicitly choose 4x or 8x afterwards.
    if (settings_version < 2 &&
        config.msaa_option == Antialiasing::MSAA8X) {
        config.msaa_option = Antialiasing::MSAA2X;
        migrated = true;
        std::fprintf(stderr, "[boot][settings] migrated legacy MSAA 8x default to 2x\n");
    }
    const auto resolved_profile =
        dkr::runtime::enhancements::resolve_settings_profile(
            settings_version, settings_complete, profile);
    if (settings_version != dkr::runtime::enhancements::kCurrentSettingsVersion ||
        !settings_complete || !profile_value_valid) {
        // Every settings file from before the hardened profile boundary, and
        // every truncated v4 write, becomes Accurate. Preserve the old refresh
        // preference as Modern's remembered value without activating it.
        if (config.rr_option == RefreshRate::Display ||
            config.rr_option == RefreshRate::Manual) {
            g_modern_refresh_mode = config.rr_option;
            g_modern_refresh_target =
                dkr::runtime::enhancements::clamp_presentation_rate(
                    config.rr_manual_value);
        }
        RememberModernGraphics(config);
        profile = resolved_profile;
        migrated = true;
        if (settings_version >= 4 && !settings_complete) {
            std::fprintf(stderr,
                         "[boot][settings] incomplete settings file; "
                         "falling back to Accurate\n");
        }
    }
    profile = resolved_profile;
    dkr::runtime::enhancements::set_presentation_profile(profile);
    ApplyProfileGraphics(config, profile);
    config.developer_mode = false;
    ultramodern::renderer::set_graphics_config(config);
    if (migrated) {
        SaveSettings();
    }
}

void SaveLastRom(const std::filesystem::path& path) {
    std::ofstream output(LastRomPath(), std::ios::trunc);
    output << PathUtf8(path);
}

std::optional<std::filesystem::path> LoadLastRom() {
    std::ifstream input(LastRomPath());
    std::string path;
    std::getline(input, path);
    if (path.empty()) {
        return std::nullopt;
    }
    return std::filesystem::u8path(path);
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool IsRomFile(const std::filesystem::path& path) {
    const std::string extension = Lowercase(PathUtf8(path.extension()));
    return extension == ".z64" || extension == ".v64" || extension == ".n64";
}

std::filesystem::path DefaultBrowserDirectory(const std::filesystem::path& selected) {
    std::error_code error;
    if (!selected.empty()) {
        const auto parent = selected.parent_path();
        if (std::filesystem::is_directory(parent, error)) {
            return parent;
        }
    }
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr) {
        const auto downloads = std::filesystem::path(profile) / "Downloads";
        if (std::filesystem::is_directory(downloads, error)) {
            return downloads;
        }
        return std::filesystem::path(profile);
    }
#else
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        const auto downloads = std::filesystem::path(home) / "Downloads";
        if (std::filesystem::is_directory(downloads, error)) {
            return downloads;
        }
        return std::filesystem::path(home);
    }
#endif
    return std::filesystem::current_path(error);
}

void RefreshRomBrowser() {
    g_rom_browser.entries.clear();
    g_rom_browser.message.clear();
    std::error_code error;
    if (g_rom_browser.directory.empty()) {
#if defined(_WIN32)
        const DWORD drive_mask = GetLogicalDrives();
        for (int drive = 0; drive < 26; ++drive) {
            if ((drive_mask & (1UL << drive)) != 0) {
                std::string root = "A:\\";
                root[0] = static_cast<char>('A' + drive);
                g_rom_browser.entries.push_back({std::filesystem::path(root), true});
            }
        }
#else
        g_rom_browser.entries.push_back({std::filesystem::path("/"), true});
        if (const char* home = std::getenv("HOME"); home != nullptr) {
            g_rom_browser.entries.push_back({std::filesystem::path(home), true});
        }
#endif
    } else {
        std::filesystem::directory_iterator iterator(
            g_rom_browser.directory,
            std::filesystem::directory_options::skip_permission_denied, error);
        if (error) {
            g_rom_browser.message = "T.T. could not read this location: " + error.message();
        } else {
            for (const auto& item : iterator) {
                const bool directory = item.is_directory(error);
                error.clear();
                if (directory || (item.is_regular_file(error) && IsRomFile(item.path()))) {
                    g_rom_browser.entries.push_back({item.path(), directory});
                }
                error.clear();
            }
        }
    }
    std::sort(g_rom_browser.entries.begin(), g_rom_browser.entries.end(),
              [](const BrowserEntry& left, const BrowserEntry& right) {
        if (left.directory != right.directory) {
            return left.directory > right.directory;
        }
        return Lowercase(PathUtf8(left.path.filename())) <
               Lowercase(PathUtf8(right.path.filename()));
    });
    g_rom_browser.focus_first_entry = true;
}

void OpenRomBrowser(const std::filesystem::path& selected) {
    g_rom_browser.open = true;
    g_rom_browser.close_requested = false;
    g_rom_browser.directory = DefaultBrowserDirectory(selected);
    RefreshRomBrowser();
}

void RomBrowserBack() {
    if (!g_rom_browser.open) {
        return;
    }
    if (g_rom_browser.directory.empty()) {
        g_rom_browser.close_requested = true;
        return;
    }
    const auto parent = g_rom_browser.directory.parent_path();
    if (parent.empty() || parent == g_rom_browser.directory) {
        g_rom_browser.directory.clear();
    } else {
        g_rom_browser.directory = parent;
    }
    RefreshRomBrowser();
}

bool AcceptRom(const std::filesystem::path& path, std::filesystem::path& selected,
               std::string& status) {
    std::string error;
    if (!dkr::runtime::SelectRom(path, error)) {
        status = error;
        g_rom_browser.message = error;
        return false;
    }
    selected = path;
    SaveLastRom(selected);
    status = "Game Pak ready. The adventure can begin!";
    g_rom_browser.open = false;
    g_rom_browser.close_requested = false;
    return true;
}

bool SelectRomWithDialog(std::filesystem::path& selected, std::string& status) {
    if (NFD_Init() != NFD_OKAY) {
        status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Nintendo 64 ROM", "z64,v64,n64"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    if (dialog == NFD_OKAY) {
        selected = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        if (!dkr::runtime::SelectRom(selected, error)) {
            status = error;
            NFD_Quit();
            return false;
        }
        SaveLastRom(selected);
        status = "Game Pak ready. The adventure can begin!";
        NFD_Quit();
        return true;
    }
    if (dialog == NFD_ERROR) {
        status = NFD_GetError();
    }
    NFD_Quit();
    return false;
}

bool ImportAdventureWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR Adventure save", "bin"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    bool imported = false;
    if (dialog == NFD_OKAY) {
        const auto source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        imported = dkr::runtime::saves::import_adventure(source, error);
        g_save_manager_status = imported
            ? "Adventure save imported. The previous save was backed up first."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return imported;
}

bool ExportAdventureWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR Adventure save", "bin"}};
    const nfdresult_t dialog = NFD_SaveDialogU8(
        &result, filters, 1, nullptr, "dkr-adventure-save.bin");
    bool exported = false;
    if (dialog == NFD_OKAY) {
        auto destination = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (destination.extension().empty()) {
            destination += ".bin";
        }
        std::string error;
        exported = dkr::runtime::saves::export_adventure(destination, error);
        g_save_manager_status = exported
            ? "Adventure save exported successfully."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return exported;
}

bool ImportSaveBundleWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdchar_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR Port save bundle", "dkrsave"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    bool imported = false;
    if (dialog == NFD_OKAY) {
        const auto source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        imported = dkr::runtime::saves::import_bundle(source, error);
        g_save_manager_status = imported
            ? "Adventure and Controller Pak saves returned safely to T.T.'s garage."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return imported;
}

bool ExportSaveBundleWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdchar_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR Port save bundle", "dkrsave"}};
    const nfdresult_t dialog = NFD_SaveDialogU8(
        &result, filters, 1, nullptr, "dkr-port-save-garage.dkrsave");
    bool exported = false;
    if (dialog == NFD_OKAY) {
        auto destination = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (destination.extension().empty()) {
            destination += ".dkrsave";
        }
        std::string error;
        exported = dkr::runtime::saves::export_bundle(destination, error);
        g_save_manager_status = exported
            ? "Complete save garage exported successfully."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return exported;
}

void DrawRaceBadge(const char* label, const ImVec4& color, float width = 0.0F);

void DrawRomBrowser(std::filesystem::path& selected, std::string& status,
                    bool& rom_ready) {
    constexpr const char* kPopupName = "Choose Your Game Pak";
    if (g_rom_browser.open && !ImGui::IsPopupOpen(kPopupName)) {
        ImGui::OpenPopup(kPopupName);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        {std::clamp(viewport->Size.x * 0.72F, 680.0F, 1040.0F),
         std::clamp(viewport->Size.y * 0.78F, 560.0F, 760.0F)},
        ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {26.0F, 24.0F});
    const bool popup_visible = ImGui::BeginPopupModal(
        kPopupName, nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!popup_visible) {
        return;
    }
    if (g_rom_browser.close_requested) {
        g_rom_browser.open = false;
        g_rom_browser.close_requested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    DrawRaceBadge(" T.T.'S GAME PAK FINDER ", kRaceRed);
    ImGui::Dummy({0.0F, 8.0F});
    PushHeadingFont();
    ImGui::TextUnformatted("CHOOSE YOUR GAME PAK");
    PopHeadingFont();
    ImGui::TextDisabled("Folders first, then .z64, .v64 and .n64 files. Nothing leaves this PC.");
    ImGui::Dummy({0.0F, 8.0F});

    const std::string location = g_rom_browser.directory.empty()
        ? "This PC"
        : PathUtf8(g_rom_browser.directory);
    ImGui::TextWrapped("Current stop: %s", location.c_str());
    if (ImGui::Button("UP ONE LEVEL", {170.0F, 42.0F})) {
        RomBrowserBack();
    }
    ImGui::SameLine();
    if (ImGui::Button("THIS PC", {130.0F, 42.0F})) {
        g_rom_browser.directory.clear();
        RefreshRomBrowser();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.02F, 0.12F, 0.23F, 0.98F});
    ImGui::BeginChild("game-pak-files", {0.0F, -118.0F}, true,
                      ImGuiWindowFlags_NavFlattened);
    if (!g_rom_browser.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextWrapped("%s", g_rom_browser.message.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    if (g_rom_browser.entries.empty() && g_rom_browser.message.empty()) {
        ImGui::TextDisabled("No race-ready Game Paks or folders were found here.");
    }
    for (std::size_t index = 0; index < g_rom_browser.entries.size(); ++index) {
        const BrowserEntry& entry = g_rom_browser.entries[index];
        std::string name = PathUtf8(entry.path.filename());
        if (name.empty()) {
            name = PathUtf8(entry.path);
        }
        const std::string label = entry.directory
            ? "[ISLAND PATH]  " + name
            : "[GAME PAK]     " + name;
        if (ImGui::Selectable(label.c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns, {0.0F, 38.0F})) {
            if (entry.directory) {
                g_rom_browser.directory = entry.path;
                RefreshRomBrowser();
                break;
            }
            rom_ready = AcceptRom(entry.path, selected, status);
            if (rom_ready) {
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        if (g_rom_browser.focus_first_entry && index == 0) {
            ImGui::SetItemDefaultFocus();
            g_rom_browser.focus_first_entry = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::TextDisabled("A / Cross  SELECT     B / Circle  BACK     D-pad / Stick  MOVE");
    if (ImGui::Button("CANCEL", {130.0F, 42.0F})) {
        g_rom_browser.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("SYSTEM FILE PICKER", {220.0F, 42.0F})) {
        rom_ready = SelectRomWithDialog(selected, status);
        if (rom_ready) {
            g_rom_browser.open = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

bool BeginMainWindow(const char* name, ImGuiWindowFlags extra = 0) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    return ImGui::Begin(name, nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
}

ImU32 MixCheckerColor(const ImVec4& left, const ImVec4& right, float amount,
                      float brightness, float alpha) {
    const auto mix = [&](float a, float b) {
        return std::clamp((a + (b - a) * amount) * brightness, 0.0F, 1.0F);
    };
    return ImGui::ColorConvertFloat4ToU32(
        {mix(left.x, right.x), mix(left.y, right.y), mix(left.z, right.z), alpha});
}

void DrawRaceBackdrop(bool overlay) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float cell = std::clamp(std::min(size.x, size.y) * 0.055F, 34.0F, 68.0F);
    const int columns = static_cast<int>(size.x / cell) + 1;
    const int rows = static_cast<int>(size.y / cell) + 1;
    const ImVec4 blue{0.025F, 0.25F, 0.66F, 1.0F};
    const ImVec4 orange{0.98F, 0.34F, 0.055F, 1.0F};
    const float alpha = overlay ? 0.92F : 1.0F;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const ImVec2 cell_min{origin.x + column * cell,
                                  origin.y + row * cell};
            const ImVec2 cell_max{
                std::min(cell_min.x + cell, origin.x + size.x),
                std::min(cell_min.y + cell, origin.y + size.y)};
            const float amount = std::clamp(
                ((cell_min.x + cell_max.x) * 0.5F - origin.x) /
                    std::max(size.x, 1.0F),
                0.0F, 1.0F);
            const float brightness = ((row + column) & 1) ? 0.72F : 1.0F;
            draw->AddRectFilled(cell_min, cell_max,
                MixCheckerColor(blue, orange, amount, brightness, alpha));
        }
    }
}

void DrawRaceBadge(const char* label, const ImVec4& color, float width) {
    // Badges communicate status; they are deliberately not ImGui buttons so
    // keyboard/gamepad navigation never wastes a stop on non-actions.
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 badge_size{
        width > 0.0F ? width : text_size.x + 20.0F,
        30.0F};
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(position,
        {position.x + badge_size.x, position.y + badge_size.y},
        ImGui::ColorConvertFloat4ToU32(color), 15.0F);
    draw->AddText({position.x + std::max((badge_size.x - text_size.x) * 0.5F, 0.0F),
                   position.y + std::max((badge_size.y - text_size.y) * 0.5F, 0.0F)},
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::Dummy(badge_size);
}

void DrawStartingLights(bool ready) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(cursor, {cursor.x + 42.0F, cursor.y + 112.0F},
                        IM_COL32(12, 25, 34, 255), 21.0F);
    const ImU32 off = IM_COL32(78, 91, 91, 255);
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 24.0F}, 9.0F,
                          ready ? off : IM_COL32(236, 54, 39, 255));
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 56.0F}, 9.0F,
                          ready ? off : IM_COL32(255, 174, 24, 255));
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 88.0F}, 9.0F,
                          ready ? IM_COL32(32, 218, 129, 255) : off);
    ImGui::Dummy({42.0F, 112.0F});
}

void BrandBlock(bool compact) {
    DrawRaceBadge(compact ? " TAJ'S TRAVELLING TENT " : " WELCOME TO TIMBER'S ISLAND ", kRaceRed);
    ImGui::Dummy({0.0F, compact ? 8.0F : 14.0F});
    ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
    PushHeadingFont();
    ImGui::TextUnformatted("DKR PORT");
    PopHeadingFont();
    ImGui::PopStyleColor();
    PushHeadingFont(true);
    ImGui::TextUnformatted("DIDDY KONG\nRACING");
    PopHeadingFont(true);
    ImGui::PushStyleColor(ImGuiCol_Text, kCream);
    ImGui::TextWrapped(compact
        ? "A little genie magic for your journey."
        : "Cars, hovercrafts and planes await. Gather the Golden Balloons and send Wizpig packing!");
    ImGui::PopStyleColor();
}

bool SidebarButton(const char* label, int page, float width = -1.0F) {
    if (g_overlay_page == page) {
        ImGui::PushStyleColor(ImGuiCol_Button, page == 0 ? kAccent : kRaceRed);
    }
    const bool pressed = ImGui::Button(label, {width, 46.0F});
    if (g_overlay_page == page) {
        ImGui::PopStyleColor();
    }
    if (pressed) {
        g_overlay_page = page;
    }
    return pressed;
}

bool DrawGraphicsSettings(bool live) {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    bool changed = false;
    bool profile_changed = false;
    int profile = static_cast<int>(
        dkr::runtime::enhancements::presentation_profile());
    int window = static_cast<int>(config.wm_option);
    int resolution = static_cast<int>(config.res_option);
    int aspect = static_cast<int>(config.ar_option);
    int aa = static_cast<int>(config.msaa_option);
    int hpfb = static_cast<int>(config.hpfb_option);
    int hud_ratio = static_cast<int>(config.hr_option);
    int downsample = std::clamp(config.ds_option, 1, 4);
    const float available_width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const float setting_width = std::clamp(available_width * 0.92F,
                                           std::min(220.0F, available_width),
                                           std::min(920.0F, available_width));
    ImGui::TextUnformatted("Race style");
    ImGui::SetNextItemWidth(setting_width);
    profile_changed = ImGui::Combo("##presentation-profile", &profile,
                                   "Accurate\0Modern\0");
    if (profile_changed) {
        const auto old_profile = dkr::runtime::enhancements::presentation_profile();
        if (old_profile == dkr::runtime::enhancements::PresentationProfile::Modern) {
            RememberModernGraphics(config);
        }
        const auto next_profile =
            static_cast<dkr::runtime::enhancements::PresentationProfile>(profile);
        dkr::runtime::enhancements::set_presentation_profile(next_profile);
        ApplyProfileGraphics(config, next_profile);
        resolution = static_cast<int>(config.res_option);
        aspect = static_cast<int>(config.ar_option);
        aa = static_cast<int>(config.msaa_option);
        hpfb = static_cast<int>(config.hpfb_option);
        hud_ratio = static_cast<int>(config.hr_option);
        downsample = std::clamp(config.ds_option, 1, 4);
        changed = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        ImGui::TextWrapped("High-refresh presentation and maximum vehicle detail. Game logic, race timers and audio keep their original speed.");
    } else {
        ImGui::TextWrapped("Original 4:3 and 30 FPS presentation with release-proven game and audio timing. Modern tune-up controls are safely parked.");
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextUnformatted("Window mode");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##window-mode", &window, "Windowed\0Fullscreen\0");
    config.wm_option = static_cast<WindowMode>(window);
    const bool modern_profile =
        dkr::runtime::enhancements::modern_options_visible(
            dkr::runtime::enhancements::presentation_profile());
    if (modern_profile) {
        ImGui::TextUnformatted("Internal resolution");
        ImGui::SetNextItemWidth(setting_width);
        changed |= ImGui::Combo("##internal-resolution", &resolution,
                                "Original (240p)\0Original 2x\0Automatic integer scale\0");
        ImGui::TextUnformatted("Aspect ratio");
        ImGui::SetNextItemWidth(setting_width);
        changed |= ImGui::Combo("##aspect-ratio", &aspect,
                                "Original 4:3\0Fit to window\0");
        ImGui::TextUnformatted("Anti-aliasing");
        ImGui::SetNextItemWidth(setting_width);
        changed |= ImGui::Combo("##anti-aliasing", &aa,
                                "None\0MSAA 2x\0MSAA 4x\0MSAA 8x\0");
        ImGui::TextUnformatted("High precision framebuffer");
        ImGui::SetNextItemWidth(setting_width);
        changed |= ImGui::Combo("##high-precision-framebuffer", &hpfb,
                                "Automatic\0On\0Off\0");
        ImGui::TextUnformatted("HUD placement");
        ImGui::SetNextItemWidth(setting_width);
        changed |= ImGui::Combo("##hud-ratio", &hud_ratio,
                                "Original 4:3 positions\0Keep within 16:9\0Use full viewport\0");
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Only HUD placement changes. Transition masks and world rendering keep their dedicated widescreen rules.");
        ImGui::PopStyleColor();
        ImGui::TextUnformatted("Downsampling quality");
        ImGui::SetNextItemWidth(setting_width);
        if (ImGui::SliderInt("##downsample-quality", &downsample, 1, 4,
                             downsample == 1 ? "Off" : "%dx", ImGuiSliderFlags_AlwaysClamp)) {
            changed = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Renders extra pixels before the final image is reduced. Higher values are expensive; 1x is recommended for high refresh rates.");
        ImGui::PopStyleColor();

        ImGui::TextUnformatted("Graphics API");
        ImGui::SetNextItemWidth(setting_width);
#if defined(_WIN32)
        int api_choice = config.api_option == GraphicsApi::D3D12
            ? 1
            : config.api_option == GraphicsApi::Vulkan ? 2 : 0;
        if (ImGui::Combo("##graphics-api", &api_choice,
                         "Automatic (recommended)\0Direct3D 12\0Vulkan\0")) {
            config.api_option = api_choice == 1
                ? GraphicsApi::D3D12
                : api_choice == 2 ? GraphicsApi::Vulkan : GraphicsApi::Auto;
            changed = true;
        }
#elif defined(__linux__)
        int api_choice = config.api_option == GraphicsApi::Vulkan ? 1 : 0;
        if (ImGui::Combo("##graphics-api", &api_choice,
                         "Automatic (recommended)\0Vulkan\0")) {
            config.api_option = api_choice == 1
                ? GraphicsApi::Vulkan
                : GraphicsApi::Auto;
            changed = true;
        }
#else
        int api_choice = 0;
        ImGui::BeginDisabled();
        ImGui::Combo("##graphics-api", &api_choice, "Automatic\0");
        ImGui::EndDisabled();
#endif
        config.res_option = static_cast<Resolution>(resolution);
        config.ar_option = static_cast<AspectRatio>(aspect);
        config.msaa_option = static_cast<Antialiasing>(aa);
        config.hpfb_option = static_cast<HighPrecisionFramebuffer>(hpfb);
        config.hr_option = static_cast<HUDRatioMode>(hud_ratio);
        config.ds_option = downsample;
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.055F, 0.19F, 0.29F, 1.0F});
        ImGui::BeginChild("accurate-aspect-lock", {setting_width, 88.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({16.0F, 12.0F});
        ImGui::PushTextWrapPos(std::max(setting_width - 16.0F, 1.0F));
        ImGui::TextUnformatted("Original 4:3 - Accurate");
        ImGui::TextWrapped("Fit to Window, graphics tuning and maximum vehicle detail appear only in Modern.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ApplyProfileGraphics(config,
                             dkr::runtime::enhancements::PresentationProfile::Accurate);
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Presentation rate");
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        int refresh_mode = g_modern_refresh_mode == RefreshRate::Manual ? 1 : 0;
        ImGui::SetNextItemWidth(setting_width);
        if (ImGui::Combo("##modern-refresh-mode", &refresh_mode,
                         "Match display\0Manual target\0")) {
            g_modern_refresh_mode = refresh_mode == 0
                ? RefreshRate::Display
                : RefreshRate::Manual;
            config.rr_option = g_modern_refresh_mode;
            changed = true;
        }
        if (g_modern_refresh_mode == RefreshRate::Manual) {
            ImGui::TextUnformatted("Frame-rate target");
            ImGui::SetNextItemWidth(setting_width);
            if (ImGui::SliderInt("##modern-refresh-target", &g_modern_refresh_target,
                                 30, 500, "%d FPS", ImGuiSliderFlags_AlwaysClamp)) {
                g_modern_refresh_target =
                    dkr::runtime::enhancements::clamp_presentation_rate(
                        g_modern_refresh_target);
                config.rr_manual_value = g_modern_refresh_target;
                changed = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextWrapped("Targets above the monitor refresh can reduce input-to-present latency, but cannot add visible refreshes and use more CPU/GPU power.");
            ImGui::PopStyleColor();
        }
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Modern preserves the original simulation and audio cadence. High-refresh presentation activates only after its current scene passes interpolation validation; unsupported scenes fall back safely to 30 FPS.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.055F, 0.19F, 0.29F, 1.0F});
        ImGui::BeginChild("accurate-presentation-rate", {setting_width, 88.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({16.0F, 12.0F});
        ImGui::PushTextWrapPos(std::max(setting_width - 16.0F, 1.0F));
        ImGui::TextUnformatted("Original 30 FPS - Accurate");
        ImGui::TextWrapped("Interpolation is locked off. Game, audio and presentation use the proven original cadence.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        config.rr_option = RefreshRate::Original;
        config.rr_manual_value = 30;
    }
    if (changed) {
        if (modern_profile) {
            RememberModernGraphics(config);
        }
        ultramodern::renderer::set_graphics_config(config);
        SaveSettings();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    if (!live) {
        ImGui::TextWrapped("Changes made before launch are applied when the adventure begins.");
    }
    ImGui::TextWrapped(modern_profile
        ? "Graphics API selection is applied at the next game launch. Automatic remains the recovery choice."
        : "Graphics API: Automatic. Accurate always uses the release-proven platform choice.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (modern_profile) {
        bool maximum_detail =
            dkr::runtime::enhancements::maximum_detail_requested();
        if (ImGui::Checkbox("Maximum vehicle detail", &maximum_detail)) {
            dkr::runtime::enhancements::set_maximum_detail_enabled(maximum_detail);
            SaveSettings();
            changed = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Keeps racer vehicles on their highest available model. Time-trial ghosts and gameplay logic retain their original models.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::SeparatorText("Camera and scenery");
        int fov_offset = dkr::runtime::enhancements::fov_offset();
        ImGui::TextUnformatted("Gameplay field-of-view offset");
        ImGui::SetNextItemWidth(setting_width);
        if (ImGui::SliderInt("##modern-fov-offset", &fov_offset, -20, 40,
                             "%+d degrees", ImGuiSliderFlags_AlwaysClamp)) {
            dkr::runtime::enhancements::set_fov_offset(fov_offset);
            SaveSettings();
            changed = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Adjusts each gameplay level from its authored camera value. Menus, character select and cutscenes keep their original framing.");
        ImGui::PopStyleColor();

        int view_distance =
            dkr::runtime::enhancements::view_distance_multiplier();
        ImGui::TextUnformatted("Object view distance");
        ImGui::SetNextItemWidth(setting_width);
        if (ImGui::SliderInt("##modern-view-distance", &view_distance, 1, 6,
                             "%dx", ImGuiSliderFlags_AlwaysClamp)) {
            dkr::runtime::enhancements::set_view_distance_multiplier(view_distance);
            SaveSettings();
            changed = true;
        }

        bool extended_culling =
            dkr::runtime::enhancements::extended_culling_requested();
        if (ImGui::Checkbox("Ultrawide scenery guard", &extended_culling)) {
            dkr::runtime::enhancements::set_extended_culling_enabled(
                extended_culling);
            SaveSettings();
            changed = true;
        }
        if (extended_culling) {
            int guard = dkr::runtime::enhancements::frustum_guard_percent();
            ImGui::TextUnformatted("Culling safety margin");
            ImGui::SetNextItemWidth(setting_width);
            if (ImGui::SliderInt("##modern-frustum-guard", &guard, 0, 20,
                                 "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                dkr::runtime::enhancements::set_frustum_guard_percent(guard);
                SaveSettings();
                changed = true;
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Expands DKR's original CPU visibility planes to the active viewport and adds a small guard band. Objects directly behind the camera still cull normally.");
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0.0F, 16.0F});
    ImGui::BeginDisabled(!modern_profile);
    if (ImGui::Button("RESTORE ACCURATE DEFAULTS", {setting_width, 46.0F})) {
        RememberModernGraphics(config);
        dkr::runtime::enhancements::set_presentation_profile(
            dkr::runtime::enhancements::PresentationProfile::Accurate);
        ApplyProfileGraphics(
            config, dkr::runtime::enhancements::PresentationProfile::Accurate);
        ultramodern::renderer::set_graphics_config(config);
        SaveSettings();
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}

void DrawSaveManager(bool live = false) {
    const auto info = dkr::runtime::saves::adventure_info();
    const float width = std::max(ImGui::GetContentRegionAvail().x - 30.0F, 1.0F);
    DrawRaceBadge(" T.T.'S SAVE GARAGE ", kAccent, width);
    ImGui::Dummy({0.0F, 10.0F});
    if (live) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.045F, 0.18F, 0.25F, 0.96F});
        ImGui::BeginChild("live-save-manager-lock", {width, 132.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({18.0F, 16.0F});
        ImGui::PushTextWrapPos(width - 18.0F);
        ImGui::TextUnformatted("PIT LANE SAFETY LOCK");
        ImGui::TextWrapped("Save import, restore and reset are available before the game starts. Close the game and use Save Garage so DKR cannot write to the same EEPROM or Controller Pak during a transfer.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.045F, 0.18F, 0.25F, 0.96F});
    ImGui::BeginChild("adventure-save-card", {width, 190.0F}, true,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos({18.0F, 16.0F});
    ImGui::PushTextWrapPos(width - 18.0F);
    ImGui::TextUnformatted("ADVENTURE PROGRESS");
    if (!info.exists) {
        ImGui::TextDisabled("No Adventure save yet. DKR will create one after your first save.");
    } else if (!info.valid) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRaceRed);
        ImGui::TextWrapped("This save does not have DKR's expected 512-byte EEPROM size. Import a known-good backup before racing.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted("READY - 512 BYTE EEPROM");
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("%s", PathUtf8(info.path).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Dummy({0.0F, 10.0F});

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool row = width >= 620.0F;
    const float button_width = row ? (width - gap * 2.0F) / 3.0F : width;
    ImGui::BeginDisabled(!info.valid);
    if (ImGui::Button("MAKE SAFETY BACKUP", {button_width, 46.0F})) {
        std::filesystem::path created;
        std::string error;
        if (dkr::runtime::saves::backup_adventure(created, error)) {
            g_save_manager_status = "Safety backup parked in T.T.'s garage.";
        } else {
            g_save_manager_status = error;
        }
    }
    if (row) ImGui::SameLine();
    if (ImGui::Button("EXPORT SAVE", {button_width, 46.0F})) {
        ExportAdventureWithDialog();
    }
    ImGui::EndDisabled();
    if (row) ImGui::SameLine();
    if (ImGui::Button("IMPORT SAVE", {button_width, 46.0F})) {
        ImportAdventureWithDialog();
    }

    ImGui::Dummy({0.0F, 14.0F});
    ImGui::SeparatorText("Complete garage transfer");
    ImGui::TextWrapped("A single path-free bundle carries the Adventure EEPROM and every present virtual Controller Pak between Windows and Steam Deck.");
    const float bundle_button_width = row ? (width - gap) * 0.5F : width;
    if (ImGui::Button("EXPORT COMPLETE GARAGE", {bundle_button_width, 46.0F})) {
        ExportSaveBundleWithDialog();
    }
    if (row) ImGui::SameLine();
    if (ImGui::Button("IMPORT COMPLETE GARAGE", {bundle_button_width, 46.0F})) {
        ImportSaveBundleWithDialog();
    }

    ImGui::Dummy({0.0F, 14.0F});
    ImGui::SeparatorText("Virtual Controller Paks");
    for (int channel = 0; channel < dkr::runtime::saves::kControllerPakCount;
         ++channel) {
        const auto pak_info = dkr::runtime::saves::controller_pak_info(channel);
        ImGui::PushID(channel);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.045F, 0.18F, 0.25F, 0.96F});
        ImGui::BeginChild("controller-pak-card", {width, 74.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({14.0F, 10.0F});
        ImGui::Text("CONTROLLER %d", channel + 1);
        ImGui::SameLine();
        if (!pak_info.exists) {
            ImGui::TextDisabled("Not created yet");
        } else if (pak_info.valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::TextUnformatted("PAK READY");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, kRaceRed);
            ImGui::TextUnformatted("RECOVERY NEEDED");
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPos({14.0F, 38.0F});
        ImGui::TextDisabled("%s", PathUtf8(pak_info.path).c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    if (!g_save_manager_status.empty()) {
        ImGui::Dummy({0.0F, 8.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextWrapped("%s", g_save_manager_status.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy({0.0F, 18.0F});
    ImGui::SeparatorText("Recent automatic backups");
    const auto backups = dkr::runtime::saves::adventure_backups();
    if (backups.empty()) {
        ImGui::TextDisabled("No backups are parked here yet.");
    } else {
        const std::size_t shown = std::min<std::size_t>(backups.size(), 6U);
        for (std::size_t index = 0; index < shown; ++index) {
            ImGui::PushID(static_cast<int>(index));
            const std::string filename = PathUtf8(backups[index].filename());
            const float restore_width = std::min(190.0F, width * 0.33F);
            if (ImGui::BeginTable("backup-row", 2,
                    ImGuiTableFlags_SizingStretchProp, {width, 0.0F})) {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("restore", ImGuiTableColumnFlags_WidthFixed,
                                        restore_width);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", filename.c_str());
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("RESTORE", {-1.0F, 38.0F})) {
                    std::string error;
                    if (dkr::runtime::saves::import_adventure(backups[index], error)) {
                        g_save_manager_status = "Backup restored. The replaced save was backed up too.";
                    } else {
                        g_save_manager_status = error;
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
    }

    ImGui::Dummy({0.0F, 18.0F});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
    if (ImGui::Button("START A FRESH ADVENTURE", {width, 44.0F})) {
        ImGui::OpenPopup("Reset Adventure save?");
    }
    ImGui::PopStyleColor();
    if (ImGui::BeginPopupModal("Reset Adventure save?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Park the current save in a backup, then start fresh?");
        ImGui::TextDisabled("The backup can be restored from this screen later.");
        if (ImGui::Button("CANCEL", {130.0F, 40.0F})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
        if (ImGui::Button("START FRESH", {150.0F, 40.0F})) {
            std::string error;
            if (dkr::runtime::saves::reset_adventure(error)) {
                g_save_manager_status = "Fresh Adventure save created; the previous journey is safe in backups.";
            } else {
                g_save_manager_status = error;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}

void DrawAudioSettings(float width) {
    const float control_width = std::min(width, 720.0F);
    float master = dkr::runtime::platform::master_volume() * 100.0F;
    ImGui::TextUnformatted("Master volume");
    ImGui::SetNextItemWidth(control_width);
    if (ImGui::SliderFloat("##audio-master", &master, 0.0F, 100.0F,
                           "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        dkr::runtime::platform::set_master_volume(master / 100.0F);
        SaveSettings();
    }
    if (!dkr::runtime::enhancements::modern_options_visible(
            dkr::runtime::enhancements::presentation_profile())) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.055F, 0.19F, 0.29F, 1.0F});
        ImGui::BeginChild("accurate-audio-lock", {control_width, 92.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({16.0F, 12.0F});
        ImGui::PushTextWrapPos(std::max(control_width - 16.0F, 1.0F));
        ImGui::TextUnformatted("Original island mix - Accurate");
        ImGui::TextWrapped("Music, effects, vehicles and EQ stay at their authored values. Master volume remains available.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Island mix");
    const auto volume_slider = [&](const char* label, const char* id,
                                   float value, auto setter) {
        float percent = value * 100.0F;
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::SliderFloat(id, &percent, 0.0F, 100.0F, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp)) {
            setter(percent / 100.0F);
            SaveSettings();
        }
    };
    volume_slider("Music", "##audio-music", dkr::runtime::audio::music_volume(),
                  dkr::runtime::audio::set_music_volume);
    volume_slider("Sound effects", "##audio-effects",
                  dkr::runtime::audio::sound_effects_volume(),
                  dkr::runtime::audio::set_sound_effects_volume);
    volume_slider("Vehicle sounds", "##audio-vehicles",
                  dkr::runtime::audio::vehicle_volume(),
                  dkr::runtime::audio::set_vehicle_volume);
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Vehicle volume is applied within DKR's racer-audio update. Sound effects remain the parent mix, matching the original sound groups.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::SeparatorText("Three-band EQ");
    const auto eq_slider = [&](const char* label, const char* id,
                               float value, auto setter) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::SliderFloat(id, &value, -12.0F, 12.0F, "%+.1f dB",
                               ImGuiSliderFlags_AlwaysClamp)) {
            setter(value);
            SaveSettings();
        }
    };
    eq_slider("Bass", "##audio-bass", dkr::runtime::platform::bass_gain(),
              dkr::runtime::platform::set_bass_gain);
    eq_slider("Mid", "##audio-mid", dkr::runtime::platform::mid_gain(),
              dkr::runtime::platform::set_mid_gain);
    eq_slider("Treble", "##audio-treble", dkr::runtime::platform::treble_gain(),
              dkr::runtime::platform::set_treble_gain);
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("At 0 dB the EQ is bypassed exactly, preserving the current release-proven samples and latency.");
    ImGui::PopStyleColor();
    if (ImGui::Button("RESTORE ORIGINAL MIX", {control_width, 44.0F})) {
        dkr::runtime::audio::set_music_volume(1.0F);
        dkr::runtime::audio::set_sound_effects_volume(1.0F);
        dkr::runtime::audio::set_vehicle_volume(1.0F);
        dkr::runtime::platform::set_bass_gain(0.0F);
        dkr::runtime::platform::set_mid_gain(0.0F);
        dkr::runtime::platform::set_treble_gain(0.0F);
        SaveSettings();
    }
}

void DrawControlsReference(bool live) {
    using dkr::runtime::input::Action;
    ImGui::TextUnformatted("DRIVER BINDINGS");
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Select any binding, then press the key, button or stick direction you want. Reusing an input moves it to the new action so conflicts cannot stack. Interface navigation always keeps A, B, the D-pad and left stick as a safe recovery route.");
    ImGui::PopStyleColor();
    const auto begin_capture_button = [](Action action, std::size_t index,
                                         CaptureDevice device, float width) {
        const std::string binding_name = device == CaptureDevice::Keyboard
            ? dkr::runtime::input::keyboard_binding_name(
                  dkr::runtime::input::keyboard_binding(action))
            : dkr::runtime::input::controller_binding_name(
                  dkr::runtime::input::controller_binding(action));
        ImGui::PushID(static_cast<int>(index * 2U +
                      (device == CaptureDevice::Controller ? 1U : 0U)));
        const bool pressed = ImGui::Button(binding_name.c_str(), {width, 38.0F});
        ImGui::PopID();
        if (pressed) {
            g_capture_action = static_cast<int>(index);
            g_capture_device = device;
            g_capture_popup_pending = true;
            g_capture_finished = false;
        }
    };

    // Keep a real gutter on the right at every width. Tables and full-width
    // capture buttons otherwise consume the parent's last pixel and appear to
    // collide with the card border/scrollbar.
    constexpr float kControlsRightPadding = 30.0F;
    const float available_width = std::max(
        ImGui::GetContentRegionAvail().x - kControlsRightPadding, 1.0F);
    if (available_width >= 620.0F &&
        ImGui::BeginTable("controls", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp,
                          {available_width, 0.0F})) {
        const float label_width = std::clamp(available_width * 0.22F, 118.0F, 156.0F);
        ImGui::TableSetupColumn("N64 CONTROL", ImGuiTableColumnFlags_WidthFixed, label_width);
        ImGui::TableSetupColumn("KEYBOARD", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("GAMEPAD", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < dkr::runtime::input::action_count(); ++index) {
            const auto action = static_cast<Action>(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", dkr::runtime::input::action_label(action));
            ImGui::TableSetColumnIndex(1);
            begin_capture_button(action, index, CaptureDevice::Keyboard, -1.0F);
            ImGui::TableSetColumnIndex(2);
            begin_capture_button(action, index, CaptureDevice::Controller, -1.0F);
        }
        ImGui::EndTable();
    } else if (available_width < 620.0F) {
        // A two-row card is easier to read and drive with a controller than
        // squeezing the three desktop columns into a narrow overlay.
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const bool side_by_side = available_width >= 360.0F;
        for (std::size_t index = 0; index < dkr::runtime::input::action_count(); ++index) {
            const auto action = static_cast<Action>(index);
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.045F, 0.18F, 0.25F, 0.92F});
            const float card_height = side_by_side ? 96.0F : 146.0F;
            ImGui::BeginChild("binding-card", {available_width, card_height}, true,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos({12.0F, 10.0F});
            ImGui::PushTextWrapPos(available_width - 12.0F);
            ImGui::TextUnformatted(dkr::runtime::input::action_label(action));
            ImGui::PopTextWrapPos();
            ImGui::SetCursorPosX(12.0F);
            const float inner_width = std::max(available_width - 24.0F, 1.0F);
            const float button_width = side_by_side
                ? std::max((inner_width - gap) * 0.5F, 1.0F)
                : inner_width;
            begin_capture_button(action, index, CaptureDevice::Keyboard, button_width);
            if (side_by_side) {
                ImGui::SameLine(0.0F, gap);
            } else {
                ImGui::SetCursorPosX(12.0F);
            }
            begin_capture_button(action, index, CaptureDevice::Controller, button_width);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("RESTORE T.T.'S DEFAULTS", {available_width, 44.0F})) {
        dkr::runtime::input::reset_defaults();
        dkr::runtime::input::set_stick_deadzone(23.95F);
        dkr::runtime::input::set_stick_anti_deadzone(0.0F);
        dkr::runtime::input::set_stick_sensitivity(100.0F);
        dkr::runtime::input::set_stick_curve(1.0F);
        dkr::runtime::input::set_stick_x_inverted(false);
        dkr::runtime::input::set_stick_y_inverted(false);
        dkr::runtime::input::set_trigger_threshold(0.5F);
        SaveSettings();
    }

    if (dkr::runtime::enhancements::modern_options_visible(
            dkr::runtime::enhancements::presentation_profile())) {
        ImGui::Dummy({0.0F, 18.0F});
        ImGui::SeparatorText("Controller feel");
        const auto tune_slider = [&](const char* label, const char* id,
                                     float value, float minimum, float maximum,
                                     const char* format, auto setter) {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(available_width);
            if (ImGui::SliderFloat(id, &value, minimum, maximum, format,
                                   ImGuiSliderFlags_AlwaysClamp)) {
                setter(value);
                SaveSettings();
            }
        };
        tune_slider("Stick deadzone", "##stick-deadzone",
                    dkr::runtime::input::stick_deadzone(), 0.0F, 35.0F, "%.1f%%",
                    dkr::runtime::input::set_stick_deadzone);
        tune_slider("Stick anti-deadzone", "##stick-anti-deadzone",
                    dkr::runtime::input::stick_anti_deadzone(), 0.0F, 50.0F, "%.1f%%",
                    dkr::runtime::input::set_stick_anti_deadzone);
        tune_slider("Stick sensitivity", "##stick-sensitivity",
                    dkr::runtime::input::stick_sensitivity(), 50.0F, 150.0F, "%.0f%%",
                    dkr::runtime::input::set_stick_sensitivity);
        tune_slider("Response curve", "##stick-curve",
                    dkr::runtime::input::stick_curve(), 0.5F, 2.5F, "%.2f",
                    dkr::runtime::input::set_stick_curve);
        tune_slider("Trigger threshold", "##trigger-threshold",
                    dkr::runtime::input::trigger_threshold(), 0.05F, 0.95F, "%.2f",
                    dkr::runtime::input::set_trigger_threshold);
        bool invert_x = dkr::runtime::input::stick_x_inverted();
        if (ImGui::Checkbox("Invert horizontal stick", &invert_x)) {
            dkr::runtime::input::set_stick_x_inverted(invert_x);
            SaveSettings();
        }
        bool invert_y = dkr::runtime::input::stick_y_inverted();
        if (ImGui::Checkbox("Invert vertical stick", &invert_y)) {
            dkr::runtime::input::set_stick_y_inverted(invert_y);
            SaveSettings();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("These adjustments shape the final N64 stick sample once per authored game update. Accurate keeps the original response.");
        ImGui::PopStyleColor();

        ImGui::Dummy({0.0F, 18.0F});
        ImGui::SeparatorText("Motion steering");
        bool gyro = dkr::runtime::input::gyro_enabled();
        if (ImGui::Checkbox("Gyro steering", &gyro)) {
            dkr::runtime::input::set_gyro_enabled(gyro);
            SaveSettings();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Modern only. Motion steering blends with the left stick and never changes DKR's handling physics.");
        ImGui::PopStyleColor();
        if (gyro) {
            int axis = static_cast<int>(dkr::runtime::input::gyro_axis());
            ImGui::TextUnformatted("Motion style");
            ImGui::SetNextItemWidth(available_width);
            if (ImGui::Combo("##gyro-axis", &axis,
                             "Roll controller like a wheel\0Yaw controller left and right\0")) {
                dkr::runtime::input::set_gyro_axis(
                    axis == 1 ? dkr::runtime::input::GyroAxis::Yaw
                              : dkr::runtime::input::GyroAxis::Roll);
                SaveSettings();
            }
            float sensitivity = dkr::runtime::input::gyro_sensitivity();
            ImGui::TextUnformatted("Gyro sensitivity");
            ImGui::SetNextItemWidth(available_width);
            if (ImGui::SliderFloat("##gyro-sensitivity", &sensitivity,
                                   25.0F, 300.0F, "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                dkr::runtime::input::set_gyro_sensitivity(sensitivity);
                SaveSettings();
            }
            float deadzone = dkr::runtime::input::gyro_deadzone();
            ImGui::TextUnformatted("Motion deadzone");
            ImGui::SetNextItemWidth(available_width);
            if (ImGui::SliderFloat("##gyro-deadzone", &deadzone,
                                   0.0F, 12.0F, "%.1f deg/s",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                dkr::runtime::input::set_gyro_deadzone(deadzone);
                SaveSettings();
            }
            bool inverted = dkr::runtime::input::gyro_inverted();
            if (ImGui::Checkbox("Invert gyro steering", &inverted)) {
                dkr::runtime::input::set_gyro_inverted(inverted);
                SaveSettings();
            }
            const bool available = dkr::runtime::platform::gyro_available();
            ImGui::BeginDisabled(!live || !available ||
                                 dkr::runtime::input::gyro_calibrating());
            if (ImGui::Button("CALIBRATE CONTROLLER", {available_width, 44.0F})) {
                dkr::runtime::input::begin_gyro_calibration();
            }
            ImGui::EndDisabled();
            if (dkr::runtime::input::gyro_calibrating()) {
                const float progress = dkr::runtime::input::gyro_calibration_progress();
                ImGui::ProgressBar(progress, {available_width, 18.0F},
                                   "Keep the controller still");
            } else if (!live) {
                ImGui::TextDisabled("Calibration is available from the in-game overlay.");
            } else if (!available) {
                ImGui::TextDisabled("No SDL gyro sensor was reported by Controller 1.");
            }
        }
    }

    constexpr const char* kCapturePopup = "CHOOSE A NEW CONTROL";
    if (g_capture_popup_pending) {
        ImGui::OpenPopup(kCapturePopup);
        g_capture_popup_pending = false;
    }
    ImGui::SetNextWindowSize({std::min(520.0F, ImGui::GetIO().DisplaySize.x - 32.0F), 230.0F},
                             ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(kCapturePopup, nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (g_capture_action >= 0) {
            const auto action = static_cast<Action>(g_capture_action);
            PushHeadingFont();
            ImGui::TextWrapped("%s", dkr::runtime::input::action_label(action));
            PopHeadingFont();
            ImGui::TextWrapped(g_capture_device == CaptureDevice::Keyboard
                ? "Press a keyboard key. Escape cancels."
                : "Press a gamepad button or move an axis firmly. Escape cancels.");
        }
        ImGui::Dummy({0.0F, 12.0F});
        const float popup_gap = ImGui::GetStyle().ItemSpacing.x;
        const float popup_button_width = std::max(
            (ImGui::GetContentRegionAvail().x - popup_gap) * 0.5F, 1.0F);
        if (ImGui::Button("UNBIND", {popup_button_width, 44.0F}) && g_capture_action >= 0) {
            const auto action = static_cast<Action>(g_capture_action);
            if (g_capture_device == CaptureDevice::Keyboard) {
                dkr::runtime::input::set_keyboard_binding(action, dkr::runtime::input::kUnbound);
            } else {
                dkr::runtime::input::set_controller_binding(action, dkr::runtime::input::kUnbound);
            }
            SaveSettings();
            g_capture_finished = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("CANCEL", {popup_button_width, 44.0F})) {
            g_capture_finished = true;
        }
        if (g_capture_finished) {
            ImGui::CloseCurrentPopup();
            g_capture_action = -1;
            g_capture_device = CaptureDevice::None;
            g_capture_finished = false;
        }
        ImGui::EndPopup();
    }
    ImGui::Dummy({0.0F, 44.0F});
}

bool HandleInputCaptureEvent(SDL_Event* event) {
    if (event == nullptr || g_capture_action < 0 || g_capture_finished) {
        return false;
    }
    using dkr::runtime::input::Action;
    const auto action = static_cast<Action>(g_capture_action);
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            g_capture_finished = true;
            return true;
        }
        if (g_capture_device == CaptureDevice::Keyboard) {
            dkr::runtime::input::set_keyboard_binding(
                action, static_cast<int>(event->key.keysym.scancode));
            SaveSettings();
            g_capture_finished = true;
            return true;
        }
    }
    if (g_capture_device == CaptureDevice::Controller &&
        event->type == SDL_CONTROLLERBUTTONDOWN) {
        dkr::runtime::input::set_controller_binding(
            action, dkr::runtime::input::encode_controller_button(event->cbutton.button));
        SaveSettings();
        g_capture_finished = true;
        return true;
    }
    if (g_capture_device == CaptureDevice::Controller &&
        event->type == SDL_CONTROLLERAXISMOTION && std::abs(event->caxis.value) >= 20000) {
        dkr::runtime::input::set_controller_binding(
            action, dkr::runtime::input::encode_controller_axis(
                        event->caxis.axis, event->caxis.value > 0));
        SaveSettings();
        g_capture_finished = true;
        return true;
    }
    return false;
}

void DrawOverlayContent(float content_width) {
    if (g_overlay_page == 0) {
        DrawRaceBadge(" TIMBER'S ISLAND PAUSED ", kRaceRed);
        ImGui::Dummy({0.0F, 10.0F});
        PushHeadingFont();
        ImGui::TextUnformatted("TAJ TRAVELLING TENT");
        PopHeadingFont();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Taj has paused the adventure while you adjust your journey. Racing controls wait safely until you return.");
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 18.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kWarm);
        if (ImGui::Button("RETURN TO TIMBER'S ISLAND", {content_width, 64.0F})) {
            g_overlay_visible.store(false, std::memory_order_release);
        }
        ImGui::PopStyleColor(2);
        ImGui::Dummy({0.0F, 12.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, kCream);
        ImGui::TextWrapped("RACE RULE: Gameplay and audio always keep the original timing. Higher frame-rate choices only make the journey look smoother.");
        ImGui::PopStyleColor();
    } else if (g_overlay_page == 1) {
        DrawRaceBadge(" TAJ'S TUNE-UP ", kRaceBlue);
        ImGui::Dummy({0.0F, 8.0F});
        PushHeadingFont();
        ImGui::TextUnformatted("VISUAL TUNE-UP");
        PopHeadingFont();
        ImGui::Separator();
        DrawGraphicsSettings(true);
    } else if (g_overlay_page == 2) {
        DrawRaceBadge(" ISLAND SOUND ", kRaceBlue);
        ImGui::Dummy({0.0F, 8.0F});
        PushHeadingFont();
        ImGui::TextUnformatted("ISLAND SOUND");
        PopHeadingFont();
        ImGui::Separator();
        DrawAudioSettings(content_width);
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Audio timing and buffer cadence remain controlled by the original game runtime.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        bool rumble = dkr::runtime::platform::rumble_enabled();
        if (ImGui::Checkbox("Controller rumble", &rumble)) {
            dkr::runtime::platform::set_rumble_enabled(rumble);
            SaveSettings();
        }
        if (rumble && dkr::runtime::enhancements::modern_options_visible(
                          dkr::runtime::enhancements::presentation_profile())) {
            float rumble_percent = dkr::runtime::platform::rumble_strength() * 100.0F;
            ImGui::TextUnformatted("Rumble strength");
            ImGui::SetNextItemWidth(std::min(content_width, 720.0F));
            if (ImGui::SliderFloat("##rumble-strength", &rumble_percent,
                                   0.0F, 100.0F, "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                dkr::runtime::platform::set_rumble_strength(rumble_percent / 100.0F);
                SaveSettings();
            }
        }
        bool memory_pak = dkr::runtime::pak::enabled();
        if (ImGui::Checkbox("Virtual Memory Pak", &memory_pak)) {
            dkr::runtime::pak::set_enabled(memory_pak);
            SaveSettings();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("T.T. keeps Controller Pak data safely in your DKR Port settings folder, with a recovery backup after every successful write.");
        ImGui::PopStyleColor();
    } else if (g_overlay_page == 3) {
        DrawRaceBadge(" T.T.'S DRIVER GUIDE ", kRaceBlue);
        ImGui::Dummy({0.0F, 8.0F});
        PushHeadingFont();
        ImGui::TextUnformatted("DRIVING CONTROLS");
        PopHeadingFont();
        ImGui::Separator();
        DrawControlsReference(true);
    } else {
        DrawRaceBadge(" ADVENTURE LOG ", kRaceRed);
        ImGui::Dummy({0.0F, 8.0F});
        PushHeadingFont();
        ImGui::TextUnformatted("ABOUT DKR PORT");
        PopHeadingFont();
        ImGui::Separator();
        ImGui::TextWrapped("DKR Port 1.0.0\nA native recompilation of Diddy Kong Racing for Windows and Linux.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("No copyrighted game data is distributed. A legally obtained supported Game Pak is required. Press F1, Escape or Back / View at any time to close this overlay.");
        ImGui::PopStyleColor();
    }
}

} // namespace

void dkr::runtime::ui::configure(const std::filesystem::path& config_directory) {
    g_config_directory = config_directory;
    LoadSettings();
}

void dkr::runtime::ui::persist_graphics_api_fallback() {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    config.api_option = GraphicsApi::Auto;
    g_modern_graphics_api = GraphicsApi::Auto;
    ultramodern::renderer::set_graphics_config(config);
    SaveSettings();
    std::fprintf(stderr,
                 "[boot][settings] unavailable graphics API recovered to Automatic\n");
}

dkr::runtime::ui::StartupResult dkr::runtime::ui::run_startup_screen(SDL_Window* window) {
    StartupResult result{};
    if (window == nullptr) {
        return result;
    }

    SDL_SetWindowTitle(window, "DKR Port - Diddy Kong Racing");
    // SDL's accelerated Linux renderers can replace the native window state
    // used by SDL_Vulkan_CreateSurface. That leaves the launcher visible but
    // makes the window disappear as soon as RT64 takes over under Gamescope.
    // The launcher is inexpensive 2D UI, so keep it on the software backend
    // and reserve the Vulkan-capable window for the game renderer.
#if defined(__linux__)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#else
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
#endif
    if (renderer == nullptr) {
        std::fprintf(stderr, "[boot][launcher] SDL renderer failed: %s\n", SDL_GetError());
        return result;
    }
    SDL_RendererInfo renderer_info{};
    if (SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
        std::fprintf(stderr,
                     "[boot][launcher] SDL renderer=%s accelerated=%s "
                     "window-flags=0x%08X\n",
                     renderer_info.name != nullptr ? renderer_info.name : "unknown",
                     (renderer_info.flags & SDL_RENDERER_ACCELERATED) != 0 ? "yes" : "no",
                     static_cast<unsigned>(SDL_GetWindowFlags(window)));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadRacingFonts();
    ApplyStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    std::filesystem::path selected_rom;
    std::string rom_status = "Choose your legally obtained Diddy Kong Racing Game Pak.";
    bool rom_ready = false;
    if (const auto remembered = LoadLastRom(); remembered.has_value()) {
        std::string error;
        if (dkr::runtime::SelectRom(*remembered, error)) {
            selected_rom = *remembered;
            rom_ready = true;
            rom_status = "Game Pak ready. The adventure can begin!";
        } else {
            rom_status = "T.T. could not find the previous Game Pak. Choose it again.";
        }
    }

    int page = 0;
    bool focus_selected_tab = true;
    if (const char* test_page = std::getenv("DKR_TEST_STARTUP_PAGE")) {
        char* end = nullptr;
        const long parsed = std::strtol(test_page, &end, 10);
        if (end != test_page && *end == '\0' && parsed >= 0 && parsed <= 3) {
            page = static_cast<int>(parsed);
        }
    }
    bool running = true;
    bool launch_requested = false;
    while (running) {
        const bool modern_launcher =
            dkr::runtime::enhancements::modern_options_visible(
                dkr::runtime::enhancements::presentation_profile());
        const int launcher_page_count = modern_launcher ? 4 : 3;
        if (page >= launcher_page_count) {
            page = 0;
        }
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (HandleInputCaptureEvent(&event)) {
                continue;
            }
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && g_rom_browser.open) {
                g_rom_browser.close_requested = true;
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B && g_rom_browser.open) {
                    RomBrowserBack();
                } else if (!g_rom_browser.open &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    page = (page + launcher_page_count - 1) % launcher_page_count;
                    focus_selected_tab = true;
                } else if (!g_rom_browser.open &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    page = (page + 1) % launcher_page_count;
                    focus_selected_tab = true;
                } else if (!g_rom_browser.open && page == 0 && rom_ready &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    launch_requested = true;
                }
            }
        }

        if (launch_requested) {
            result.start_game = true;
            result.rom_path = selected_rom;
            running = false;
            continue;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        dkr::runtime::platform::update_ui_gamepad_navigation();
        ImGui::NewFrame();
        BeginMainWindow("DKR Port Startup");
        DrawRaceBackdrop(false);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float outer_margin = std::clamp(available.x * 0.025F, 22.0F, 38.0F);
        const float content_height = available.y - outer_margin * 2.0F;
        const bool compact_layout = available.x < 1040.0F;
        const float hero_width = compact_layout
            ? 0.0F
            : std::clamp(available.x * 0.34F, 320.0F, 470.0F);
        const float right_width = compact_layout
            ? available.x - outer_margin * 2.0F
            : available.x - hero_width - outer_margin * 3.0F;
        const float panel_padding = std::clamp(right_width * 0.05F, 18.0F, 46.0F);
        const float right_inner_width = std::max(right_width - panel_padding * 2.0F, 1.0F);
        ImGui::SetCursorPos({outer_margin, outer_margin});
        if (!compact_layout) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.025F, 0.105F, 0.15F, 0.96F});
        ImGui::PushStyleColor(ImGuiCol_Border, {1.0F, 0.67F, 0.08F, 0.92F});
        ImGui::BeginChild("race-hero", {hero_width, content_height}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({38.0F, 38.0F});
        ImGui::PushTextWrapPos(hero_width - 38.0F);
        ImGui::BeginGroup();
        DrawStartingLights(rom_ready);
        ImGui::Dummy({0.0F, 18.0F});
        BrandBlock(false);
        ImGui::Dummy({0.0F, 26.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted("FAITHFUL. FAST. READY TO RACE.");
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 12.0F});
        ImGui::TextWrapped("Original timing, graphics and audio preserved - with a modern PC pit crew under the hood.");
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos({38.0F, std::max(450.0F, content_height - 150.0F)});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.075F, 0.165F, 0.20F, 0.94F});
        ImGui::BeginChild("paddock-pass", {hero_width - 76.0F, 112.0F}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos({18.0F, 15.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextUnformatted("TIMBER'S ISLAND NOTE");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Bring your own legally obtained Game Pak. It stays on this PC and is never uploaded.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0F, outer_margin);
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.035F, 0.085F, 0.12F, 0.98F});
        ImGui::PushStyleColor(ImGuiCol_Border, {0.12F, 0.62F, 0.58F, 0.88F});
        ImGui::BeginChild("pit-garage", {right_width, content_height}, true);
        ImGui::SetCursorPos({panel_padding, 30.0F});
        ImGui::PushItemWidth(right_inner_width);
        ImGui::PushTextWrapPos(panel_padding + right_inner_width);
        ImGui::BeginGroup();
        DrawRaceBadge(" WELCOME TO TIMBER'S ISLAND ", kRaceRed);
        ImGui::Dummy({0.0F, 12.0F});
        const bool tabs_inline = right_inner_width >= (modern_launcher ? 680.0F : 540.0F);
        const float tab_gap = ImGui::GetStyle().ItemSpacing.x;
        const float tab_width = tabs_inline
            ? (right_inner_width - tab_gap * (launcher_page_count - 1)) /
                  static_cast<float>(launcher_page_count)
             : right_inner_width;
        ImGui::PushStyleColor(ImGuiCol_Button, page == 0 ? kRaceRed : kRaceBlue);
        if (focus_selected_tab && page == 0) ImGui::SetKeyboardFocusHere();
        if (ImGui::Button("ADVENTURE", {tab_width, 46.0F})) {
            page = 0;
            focus_selected_tab = true;
        }
        ImGui::PopStyleColor();
        if (tabs_inline) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, page == 1 ? kRaceRed : kRaceBlue);
        if (focus_selected_tab && page == 1) ImGui::SetKeyboardFocusHere();
        if (ImGui::Button("TAJ'S TUNE-UP", {tab_width, 46.0F})) {
            page = 1;
            focus_selected_tab = true;
        }
        ImGui::PopStyleColor();
        if (tabs_inline) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, page == 2 ? kRaceRed : kRaceBlue);
        if (focus_selected_tab && page == 2) ImGui::SetKeyboardFocusHere();
        if (ImGui::Button("DRIVER GUIDE", {tab_width, 46.0F})) {
            page = 2;
            focus_selected_tab = true;
        }
        ImGui::PopStyleColor();
        if (modern_launcher) {
            if (tabs_inline) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, page == 3 ? kRaceRed : kRaceBlue);
            if (focus_selected_tab && page == 3) ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("SAVE GARAGE", {tab_width, 46.0F})) {
                page = 3;
                focus_selected_tab = true;
            }
            ImGui::PopStyleColor();
        }
        // Keyboard/gamepad navigation now has an explicit landing target after
        // a bumper tab switch. Do this once; continuously forcing focus would
        // prevent the player from moving into the page's controls.
        focus_selected_tab = false;
        ImGui::Dummy({0.0F, 24.0F});

        if (page == 0) {
            PushHeadingFont();
            ImGui::TextUnformatted(rom_ready ? "GAME PAK READY!" : "ADVENTURE PAK REQUIRED");
            PopHeadingFont();
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextWrapped("Choose your Game Pak once and T.T. will remember where it is for your next visit.");
            ImGui::PopStyleColor();
            ImGui::Dummy({0.0F, 16.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kCream);
            ImGui::PushStyleColor(ImGuiCol_Border, rom_ready ? kAccent : kRaceRed);
            // The vertical starting-light stack is 112 px tall. Reserve a
            // complete second row for the selected path and Game Pak button,
            // plus a comfortable bottom inset, instead of clipping the action
            // against the old card height designed for horizontal lights.
            ImGui::BeginChild("race-pass", {right_inner_width, 300.0F}, true);
            ImGui::SetCursorPos({24.0F, 20.0F});
            ImGui::BeginGroup();
            DrawStartingLights(rom_ready);
            ImGui::SameLine(0.0F, 18.0F);
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, rom_ready
                ? ImVec4{0.01F, 0.48F, 0.28F, 1.0F} : kRaceRed);
            ImGui::TextUnformatted(rom_ready ? "ENTRY CLEARED" : "GAME DATA NEEDED");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, {0.035F, 0.105F, 0.14F, 1.0F});
            ImGui::TextWrapped("%s", rom_status.c_str());
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            if (!selected_rom.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, {0.18F, 0.31F, 0.34F, 1.0F});
                ImGui::TextWrapped("%s", PathUtf8(selected_rom).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 6.0F});
            if (ImGui::Button(rom_ready ? "CHOOSE ANOTHER GAME PAK" : "CHOOSE YOUR GAME PAK",
                              {std::min(260.0F, right_inner_width - 48.0F), 46.0F})) {
                OpenRomBrowser(selected_rom);
            }
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Dummy({0.0F, 20.0F});
            ImGui::BeginDisabled(!rom_ready);
            ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kWarm);
            if (ImGui::Button("BEGIN THE ADVENTURE!", {right_inner_width, 68.0F})) {
                result.start_game = true;
                result.rom_path = selected_rom;
                running = false;
            }
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
            ImGui::Dummy({0.0F, 16.0F});
            ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
            ImGui::TextUnformatted("T.T.'S RACE-DAY PROMISES");
            ImGui::PopStyleColor();
            ImGui::Dummy({0.0F, 3.0F});
            const float promise_gap = 10.0F;
            const float promise_width = (right_inner_width - promise_gap * 2.0F) / 3.0F;
            if (promise_width >= 190.0F) {
                DrawRaceBadge(" GAME PAK STAYS LOCAL ", kRaceBlue, promise_width);
                ImGui::SameLine(0.0F, promise_gap);
                DrawRaceBadge(" ORIGINAL TIMING ", kRaceRed, promise_width);
                ImGui::SameLine(0.0F, promise_gap);
                DrawRaceBadge(" F1 / ESC TAJ'S TENT ", kAccent, promise_width);
            } else {
                DrawRaceBadge(" GAME PAK STAYS LOCAL ", kRaceBlue, right_inner_width);
                DrawRaceBadge(" ORIGINAL TIMING ", kRaceRed, right_inner_width);
                DrawRaceBadge(" F1 / ESC TAJ'S TENT ", kAccent, right_inner_width);
            }
        } else if (page == 1) {
            PushHeadingFont();
            ImGui::TextUnformatted("TAJ TUNE UP");
            PopHeadingFont();
            ImGui::TextDisabled("Polish the presentation without changing how the original race feels.");
            ImGui::Dummy({0.0F, 18.0F});
            DrawGraphicsSettings(false);
            ImGui::Dummy({0.0F, 18.0F});
            ImGui::SeparatorText("Island sound");
            DrawAudioSettings(right_inner_width);
        } else if (page == 2) {
            PushHeadingFont();
            ImGui::TextUnformatted("TT DRIVER GUIDE");
            PopHeadingFont();
            ImGui::TextDisabled("Keyboard or gamepad - pick your machine and hit the track.");
            ImGui::Dummy({0.0F, 12.0F});
            DrawControlsReference(false);
        } else {
            PushHeadingFont();
            ImGui::TextUnformatted("TT SAVE GARAGE");
            PopHeadingFont();
            ImGui::TextDisabled("Back up, import and export Adventure progress before the race begins.");
            ImGui::Dummy({0.0F, 12.0F});
            DrawSaveManager(false);
        }
        // Every scrollable tab ends with breathing room so its final control
        // never rests directly on the rounded card boundary.
        ImGui::Dummy({0.0F, 54.0F});
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        // The Tune-Up page is intentionally scrollable. A fixed legend here
        // used to paint over its last combo/card at 900p and narrower heights.
        // Keep the start hint only on the short Adventure page, where it has a
        // guaranteed reserved strip and cannot overlap settings or bindings.
        if (page == 0 && content_height >= 720.0F) {
            ImGui::SetCursorPos({panel_padding, content_height - 44.0F});
            ImGui::PushTextWrapPos(panel_padding + right_inner_width);
            ImGui::TextDisabled("A SELECT   B BACK   LB/RB TABS   START BEGIN");
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        DrawRomBrowser(selected_rom, rom_status, rom_ready);
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 6, 9, 14, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    std::fprintf(stderr,
                 "[boot][launcher] handoff complete window-flags=0x%08X\n",
                 static_cast<unsigned>(SDL_GetWindowFlags(window)));
    return result;
}

void dkr::runtime::ui::attach(RT64::Application& application) {
    if (application.presentQueue == nullptr || application.device == nullptr ||
        application.swapChain == nullptr) {
        return;
    }
    std::scoped_lock guard(g_inspector_guard);
    std::scoped_lock present_lock(application.presentQueue->inspectorMutex);
    application.presentQueue->inspector = std::make_unique<RT64::Inspector>(
        application.device.get(), application.swapChain.get(),
        application.chosenGraphicsAPI,
        static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
    application.presentQueue->inspector->setIniPath(g_config_directory / "dkr-port-ui.ini");
    g_inspector = application.presentQueue->inspector.get();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadRacingFonts();
    std::fprintf(stderr, "[boot][ui] in-game overlay attached (F1/Escape/Back)\n");
}

void dkr::runtime::ui::detach(RT64::Application& application) {
    std::scoped_lock guard(g_inspector_guard);
    g_inspector = nullptr;
    if (application.presentQueue != nullptr) {
        std::scoped_lock present_lock(application.presentQueue->inspectorMutex);
        application.presentQueue->inspector.reset();
    }
}

void dkr::runtime::ui::draw(RT64::Application& application) {
    if (application.presentQueue == nullptr || application.framebufferGraphicsWorker == nullptr) {
        return;
    }
    if (!g_overlay_visible.load(std::memory_order_acquire)) {
        if (application.presentQueue->inspector != nullptr) {
            detach(application);
        }
        return;
    }
    if (application.presentQueue->inspector == nullptr) {
        attach(application);
        g_overlay_dirty.store(true, std::memory_order_release);
    }
    if (application.presentQueue->inspector == nullptr) {
        return;
    }
    dkr::runtime::platform::update_ui_gamepad_navigation();
    RT64::Inspector* inspector = application.presentQueue->inspector.get();
    inspector->newFrame(application.framebufferGraphicsWorker.get());
    ApplyStyle();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    BeginMainWindow("DKR Port Overlay", ImGuiWindowFlags_NoBackground);
        bool request_quit_popup = false;
        const bool focus_selected_page =
            g_overlay_dirty.exchange(false, std::memory_order_acq_rel);
        const float overlay_margin = std::clamp(ImGui::GetWindowWidth() * 0.025F, 12.0F, 38.0F);
        const float overlay_gap = std::clamp(ImGui::GetWindowWidth() * 0.018F, 12.0F, 28.0F);
        const float minimum_sidebar = ImGui::GetWindowWidth() < 1000.0F ? 190.0F : 220.0F;
        const float maximum_sidebar = std::max(ImGui::GetWindowWidth() * 0.40F, minimum_sidebar);
        const float sidebar_width = std::clamp(ImGui::GetWindowWidth() * 0.25F,
                                               minimum_sidebar, std::min(370.0F, maximum_sidebar));
        const float overlay_height = ImGui::GetWindowHeight() - overlay_margin * 2.0F;
        const float content_x = overlay_margin + sidebar_width + overlay_gap;
        const float content_panel_width = ImGui::GetWindowWidth() - content_x - overlay_margin;
        ImGui::SetCursorPos({overlay_margin, overlay_margin});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.025F, 0.09F, 0.13F, 0.84F});
        ImGui::PushStyleColor(ImGuiCol_Border, kWarm);
        ImGui::BeginChild("overlay-nav", {sidebar_width, overlay_height}, true);
        const float nav_padding = sidebar_width < 220.0F ? 16.0F : 24.0F;
        ImGui::SetCursorPos({nav_padding, nav_padding});
        const float nav_inner_width = sidebar_width - nav_padding * 2.0F;
        ImGui::PushTextWrapPos(sidebar_width - nav_padding);
        ImGui::BeginGroup();
        BrandBlock(true);
        ImGui::Dummy({0.0F, 24.0F});
        if (focus_selected_page && g_overlay_page == 0) ImGui::SetKeyboardFocusHere();
        SidebarButton("RETURN TO THE ISLAND", 0, nav_inner_width);
        if (focus_selected_page && g_overlay_page == 1) ImGui::SetKeyboardFocusHere();
        SidebarButton("TAJ'S TUNE-UP", 1, nav_inner_width);
        if (focus_selected_page && g_overlay_page == 2) ImGui::SetKeyboardFocusHere();
        SidebarButton("ISLAND SOUND", 2, nav_inner_width);
        if (focus_selected_page && g_overlay_page == 3) ImGui::SetKeyboardFocusHere();
        SidebarButton("T.T.'S DRIVER GUIDE", 3, nav_inner_width);
        if (focus_selected_page && g_overlay_page == 4) ImGui::SetKeyboardFocusHere();
        SidebarButton("ADVENTURE LOG", 4, nav_inner_width);
        ImGui::Dummy({0.0F, 24.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
        const bool leave_island_pressed =
            ImGui::Button("LEAVE TIMBER'S ISLAND", {nav_inner_width, 44.0F});
        if (std::getenv("DKR_INPUT_TRACE") != nullptr &&
            !g_logged_exit_button_rect.exchange(true, std::memory_order_relaxed)) {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            std::fprintf(stderr,
                         "[test][input] leave-button rect=(%.0f,%.0f)-(%.0f,%.0f)\n",
                         item_min.x, item_min.y, item_max.x, item_max.y);
        }
        if (std::getenv("DKR_INPUT_TRACE") != nullptr &&
            (leave_island_pressed || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
            std::fprintf(stderr,
                         "[test][input] leave-button pressed=%d hovered=%d mouse-down=%d clicked=%d released=%d\n",
                         leave_island_pressed ? 1 : 0,
                         ImGui::IsItemHovered() ? 1 : 0,
                         ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0,
                         ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? 1 : 0,
                         ImGui::IsMouseReleased(ImGuiMouseButton_Left) ? 1 : 0);
        }
        if (leave_island_pressed) {
            request_quit_popup = true;
        }
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPos({content_x, overlay_margin});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.035F, 0.085F, 0.12F, 0.84F});
        ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
        ImGui::BeginChild("overlay-content", {content_panel_width, overlay_height}, true);
        const float content_padding = std::clamp(content_panel_width * 0.045F, 20.0F, 44.0F);
        const float content_inner_width = content_panel_width - content_padding * 2.0F;
        ImGui::SetCursorPos({content_padding, content_padding});
        ImGui::PushItemWidth(content_inner_width);
        ImGui::PushTextWrapPos(content_padding + content_inner_width);
        ImGui::BeginGroup();
        DrawOverlayContent(content_inner_width);
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        if (request_quit_popup) {
            // Open the modal in the same parent ID scope where it is rendered.
            // Opening it inside overlay-nav creates a different ImGui popup ID,
            // which made the Exit to Desktop button appear to do nothing.
            ImGui::OpenPopup("Quit DKR Port?");
        }
        if (ImGui::BeginPopupModal("Quit DKR Port?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Leave Timber's Island and return to the desktop?");
            ImGui::TextDisabled("Progress since the last in-game save point may be lost.");
            if (ImGui::Button("CANCEL", {120.0F, 40.0F})) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
            const bool confirm_leave_pressed = ImGui::Button("LEAVE", {120.0F, 40.0F});
            if (std::getenv("DKR_INPUT_TRACE") != nullptr &&
                !g_logged_exit_confirm_rect.exchange(true, std::memory_order_relaxed)) {
                const ImVec2 item_min = ImGui::GetItemRectMin();
                const ImVec2 item_max = ImGui::GetItemRectMax();
                std::fprintf(stderr,
                             "[test][input] confirm-leave rect=(%.0f,%.0f)-(%.0f,%.0f)\n",
                             item_min.x, item_min.y, item_max.x, item_max.y);
            }
            if (confirm_leave_pressed) {
                g_exit_requested.store(true, std::memory_order_release);
                g_overlay_visible.store(false, std::memory_order_release);
                ultramodern::quit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    ImGui::End();
    inspector->endFrame();
    if (!g_logged_overlay_frame.exchange(true, std::memory_order_relaxed)) {
        const ImDrawData* draw_data = ImGui::GetDrawData();
        const ImGuiIO& io = ImGui::GetIO();
        std::fprintf(stderr,
                     "[boot][ui] overlay frame display=%.0fx%.0f cmd-lists=%d "
                     "vertices=%d indices=%d\n",
                     io.DisplaySize.x, io.DisplaySize.y,
                     draw_data != nullptr ? draw_data->CmdListsCount : 0,
                     draw_data != nullptr ? draw_data->TotalVtxCount : 0,
                     draw_data != nullptr ? draw_data->TotalIdxCount : 0);
    }
}

bool dkr::runtime::ui::handle_runtime_event(SDL_Event* event) {
    if (event == nullptr) {
        return false;
    }
    if (HandleInputCaptureEvent(event)) {
        return true;
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
        event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        toggle_overlay();
        return true;
    }
    if (g_overlay_visible.load(std::memory_order_acquire) &&
        event->type == SDL_CONTROLLERBUTTONDOWN) {
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            const int page = g_overlay_page.load(std::memory_order_relaxed);
            g_overlay_page.store((page + 4) % 5, std::memory_order_relaxed);
            g_overlay_dirty.store(true, std::memory_order_release);
            return true;
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            const int page = g_overlay_page.load(std::memory_order_relaxed);
            g_overlay_page.store((page + 1) % 5, std::memory_order_relaxed);
            g_overlay_dirty.store(true, std::memory_order_release);
            return true;
        }
    }
    std::scoped_lock guard(g_inspector_guard);
    if (g_inspector == nullptr) {
        return false;
    }
    std::scoped_lock frame_lock(g_inspector->frameMutex);
    const bool handled = g_inspector->handleSdlEvent(event);
    g_overlay_dirty.store(true, std::memory_order_release);
    return handled;
}

bool dkr::runtime::ui::input_capture_active() {
    return g_capture_action >= 0 && !g_capture_finished;
}

void dkr::runtime::ui::toggle_overlay() {
    const bool next = !g_overlay_visible.load(std::memory_order_acquire);
    g_overlay_page = 0;
    g_overlay_visible.store(next, std::memory_order_release);
    g_overlay_dirty.store(true, std::memory_order_release);
}

bool dkr::runtime::ui::overlay_visible() {
    return g_overlay_visible.load(std::memory_order_acquire);
}

bool dkr::runtime::ui::consume_exit_request() {
    return g_exit_requested.exchange(false, std::memory_order_acq_rel);
}
