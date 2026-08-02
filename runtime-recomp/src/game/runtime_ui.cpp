#include "runtime_ui.hpp"

#include "game_registration.hpp"
#include "generated/racing_banana_font.h"
#include "runtime_enhancements.hpp"
#include "runtime_input.hpp"
#include "runtime_platform.hpp"
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
using ultramodern::renderer::GraphicsConfig;
using ultramodern::renderer::HighPrecisionFramebuffer;
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
enum class CaptureDevice { None, Keyboard, Controller };
CaptureDevice g_capture_device = CaptureDevice::None;
int g_capture_action = -1;
bool g_capture_popup_pending = false;
bool g_capture_finished = false;

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

void SaveSettings() {
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    const auto& config = ultramodern::renderer::get_graphics_config();
    std::ofstream output(SettingsPath(), std::ios::trunc);
    if (!output) {
        return;
    }
    output << "settings_version=3\n";
    output << "presentation_profile="
           << static_cast<int>(dkr::runtime::enhancements::presentation_profile()) << '\n';
    output << "window_mode=" << static_cast<int>(config.wm_option) << '\n';
    output << "resolution=" << static_cast<int>(config.res_option) << '\n';
    output << "aspect=" << static_cast<int>(config.ar_option) << '\n';
    output << "antialiasing=" << static_cast<int>(config.msaa_option) << '\n';
    output << "high_precision_fb=" << static_cast<int>(config.hpfb_option) << '\n';
    output << "refresh_rate=" << static_cast<int>(config.rr_option) << '\n';
    output << "refresh_rate_target=" << std::clamp(config.rr_manual_value, 30, 500) << '\n';
    output << "modern_refresh_rate=" << static_cast<int>(g_modern_refresh_mode) << '\n';
    output << "modern_refresh_target=" << std::clamp(g_modern_refresh_target, 30, 500) << '\n';
    output << "master_volume=" << dkr::runtime::platform::master_volume() << '\n';
    output << "maximum_detail="
           << (dkr::runtime::enhancements::maximum_detail_enabled() ? 1 : 0) << '\n';
    output << "memory_pak=" << (dkr::runtime::pak::enabled() ? 1 : 0) << '\n';
    output << "rumble=" << (dkr::runtime::platform::rumble_enabled() ? 1 : 0) << '\n';
    for (std::size_t index = 0; index < dkr::runtime::input::action_count(); ++index) {
        const auto action = static_cast<dkr::runtime::input::Action>(index);
        output << "keyboard_binding." << dkr::runtime::input::action_identifier(action)
               << '=' << dkr::runtime::input::keyboard_binding(action) << '\n';
        output << "controller_binding." << dkr::runtime::input::action_identifier(action)
               << '=' << dkr::runtime::input::controller_binding(action) << '\n';
    }
}

void LoadSettings() {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    int settings_version = 0;
    auto profile = dkr::runtime::enhancements::PresentationProfile::Accurate;
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
            } else if (key == "presentation_profile" && number >= 0 && number < 2) {
                profile = static_cast<dkr::runtime::enhancements::PresentationProfile>(number);
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
            } else if (key == "refresh_rate" && number >= 0 && number < 3) {
                config.rr_option = static_cast<RefreshRate>(number);
            } else if (key == "refresh_rate_target") {
                config.rr_manual_value = std::clamp(number, 30, 500);
            } else if (key == "modern_refresh_rate" &&
                       number >= static_cast<int>(RefreshRate::Display) &&
                       number <= static_cast<int>(RefreshRate::Manual)) {
                g_modern_refresh_mode = static_cast<RefreshRate>(number);
            } else if (key == "modern_refresh_target") {
                g_modern_refresh_target = std::clamp(number, 30, 500);
            } else if (key == "master_volume") {
                dkr::runtime::platform::set_master_volume(std::stof(value));
            } else if (key == "maximum_detail") {
                dkr::runtime::enhancements::set_maximum_detail_enabled(number != 0);
            } else if (key == "memory_pak") {
                dkr::runtime::pak::set_enabled(number != 0);
            } else if (key == "rumble") {
                dkr::runtime::platform::set_rumble_enabled(number != 0);
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
    // Early launcher builds defaulted to 8x MSAA, which can turn busy races
    // GPU-bound at high desktop resolutions. Migrate that one legacy default
    // to 2x; users can still explicitly choose 4x or 8x afterwards.
    if (settings_version < 2 &&
        config.msaa_option == Antialiasing::MSAA8X) {
        config.msaa_option = Antialiasing::MSAA2X;
        migrated = true;
        std::fprintf(stderr, "[boot][settings] migrated legacy MSAA 8x default to 2x\n");
    }
    if (settings_version < 3) {
        // Every pre-profile installation becomes Accurate. Preserve the old
        // refresh preference as Modern's remembered value without activating
        // interpolation during migration.
        if (config.rr_option == RefreshRate::Display ||
            config.rr_option == RefreshRate::Manual) {
            g_modern_refresh_mode = config.rr_option;
            g_modern_refresh_target = std::clamp(config.rr_manual_value, 30, 500);
        }
        profile = dkr::runtime::enhancements::PresentationProfile::Accurate;
        migrated = true;
    }
    dkr::runtime::enhancements::set_presentation_profile(profile);
    if (profile == dkr::runtime::enhancements::PresentationProfile::Modern) {
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
    } else {
        config.rr_option = RefreshRate::Original;
        config.rr_manual_value = 30;
    }
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

void DrawCheckeredBand(ImDrawList* draw, const ImVec2& origin, float width, float height,
                       float cell_size, ImU32 light, ImU32 dark) {
    const int columns = static_cast<int>(width / cell_size) + 1;
    const int rows = static_cast<int>(height / cell_size) + 1;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const ImVec2 cell_min{origin.x + column * cell_size, origin.y + row * cell_size};
            const ImVec2 cell_max{
                std::min(cell_min.x + cell_size, origin.x + width),
                std::min(cell_min.y + cell_size, origin.y + height)};
            draw->AddRectFilled(cell_min, cell_max, ((column + row) & 1) ? dark : light);
        }
    }
}

void DrawRaceBackdrop(bool overlay) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 bottom_right{origin.x + size.x, origin.y + size.y};
    const float horizon = origin.y + size.y * 0.60F;
    const ImU32 sky_top = overlay ? IM_COL32(5, 66, 132, 247) : IM_COL32(14, 128, 221, 255);
    const ImU32 sky_horizon = overlay ? IM_COL32(19, 112, 152, 247) : IM_COL32(92, 205, 235, 255);
    draw->AddRectFilledMultiColor(origin, {bottom_right.x, horizon},
                                  sky_top, sky_top, sky_horizon, sky_horizon);
    draw->AddRectFilledMultiColor({origin.x, horizon}, bottom_right,
        overlay ? IM_COL32(150, 104, 25, 250) : IM_COL32(247, 190, 45, 255),
        overlay ? IM_COL32(171, 118, 26, 250) : IM_COL32(255, 208, 66, 255),
        overlay ? IM_COL32(105, 65, 14, 252) : IM_COL32(221, 135, 18, 255),
        overlay ? IM_COL32(129, 78, 16, 252) : IM_COL32(242, 157, 24, 255));

    // Timber's Island: layered green hills, jewel-bright trees and balloon
    // markers establish the game's playful adventure-racing language without
    // redistributing any original artwork.
    for (int hill = -1; hill < 8; ++hill) {
        const float x = origin.x + size.x * (0.04F + hill * 0.145F);
        const float radius = size.x * (0.10F + ((hill & 1) ? 0.025F : 0.0F));
        draw->AddCircleFilled({x, horizon + size.y * 0.03F}, radius,
            overlay ? IM_COL32(24, 92, 40, 245) : IM_COL32(45, 165, 57, 255), 48);
        draw->AddCircleFilled({x + radius * 0.30F, horizon - radius * 0.10F}, radius * 0.72F,
            overlay ? IM_COL32(34, 116, 48, 245) : IM_COL32(78, 201, 67, 255), 40);
    }

    const float band_height = std::clamp(size.y * 0.025F, 16.0F, 24.0F);
    const float cell = band_height * 0.75F;
    DrawCheckeredBand(draw, origin, size.x, band_height, cell,
                      IM_COL32(29, 117, 209, 255), IM_COL32(12, 67, 151, 255));
    DrawCheckeredBand(draw, {origin.x, bottom_right.y - band_height}, size.x, band_height,
                      cell, IM_COL32(255, 225, 64, 255), IM_COL32(228, 48, 35, 255));

    // A broad track sweeps behind the panels to give the screen movement without
    // depending on any original game artwork.
    const ImVec2 road[] = {
        {origin.x + size.x * 0.18F, bottom_right.y + size.y * 0.08F},
        {origin.x + size.x * 0.47F, horizon - size.y * 0.03F},
        {origin.x + size.x * 0.57F, horizon - size.y * 0.03F},
        {origin.x + size.x * 0.83F, bottom_right.y + size.y * 0.08F},
    };
    draw->AddConvexPolyFilled(road, 4, overlay ? IM_COL32(78, 54, 30, 205) : IM_COL32(180, 112, 35, 235));
    draw->AddLine(road[0], road[1], IM_COL32(240, 56, 39, 230), 10.0F);
    draw->AddLine(road[3], road[2], IM_COL32(255, 230, 64, 230), 10.0F);

    const float dash_count = 8.0F;
    for (int i = 0; i < static_cast<int>(dash_count); ++i) {
        const float t0 = (static_cast<float>(i) + 0.15F) / dash_count;
        const float t1 = (static_cast<float>(i) + 0.62F) / dash_count;
        const ImVec2 a{
            road[0].x + (road[1].x - road[0].x) * t0 + size.x * 0.31F,
            road[0].y + (road[1].y - road[0].y) * t0};
        const ImVec2 b{
            road[0].x + (road[1].x - road[0].x) * t1 + size.x * 0.31F,
            road[0].y + (road[1].y - road[0].y) * t1};
        draw->AddLine(a, b, IM_COL32(255, 239, 189, 210), 5.0F);
    }

    const ImVec2 balloon_anchor{bottom_right.x - size.x * 0.075F, origin.y + size.y * 0.13F};
    const ImU32 balloon_colors[] = {
        IM_COL32(238, 48, 38, 235), IM_COL32(255, 218, 47, 235),
        IM_COL32(38, 108, 219, 235), IM_COL32(50, 190, 90, 235)};
    for (int i = 0; i < 4; ++i) {
        const ImVec2 centre{balloon_anchor.x + (i - 2) * 24.0F,
                            balloon_anchor.y + (i & 1) * 18.0F};
        draw->AddCircleFilled(centre, 14.0F, balloon_colors[i], 24);
        draw->AddLine({centre.x, centre.y + 14.0F},
                      {balloon_anchor.x, balloon_anchor.y + 88.0F},
                      IM_COL32(255, 248, 215, 180), 1.5F);
    }
}

void DrawRaceBadge(const char* label, const ImVec4& color, float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0F);
    if (width > 0.0F) {
        ImGui::Button(label, {width, 30.0F});
    } else {
        ImGui::SmallButton(label);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void DrawStartingLights(bool ready) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(cursor, {cursor.x + 112.0F, cursor.y + 34.0F},
                        IM_COL32(12, 25, 34, 255), 17.0F);
    const ImU32 off = IM_COL32(78, 91, 91, 255);
    draw->AddCircleFilled({cursor.x + 24.0F, cursor.y + 17.0F}, 9.0F,
                          ready ? off : IM_COL32(236, 54, 39, 255));
    draw->AddCircleFilled({cursor.x + 56.0F, cursor.y + 17.0F}, 9.0F,
                          ready ? off : IM_COL32(255, 174, 24, 255));
    draw->AddCircleFilled({cursor.x + 88.0F, cursor.y + 17.0F}, 9.0F,
                          ready ? IM_COL32(32, 218, 129, 255) : off);
    ImGui::Dummy({112.0F, 34.0F});
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
        if (old_profile == dkr::runtime::enhancements::PresentationProfile::Modern &&
            (config.rr_option == RefreshRate::Display ||
             config.rr_option == RefreshRate::Manual)) {
            g_modern_refresh_mode = config.rr_option;
            g_modern_refresh_target = std::clamp(config.rr_manual_value, 30, 500);
        }
        const auto next_profile =
            static_cast<dkr::runtime::enhancements::PresentationProfile>(profile);
        dkr::runtime::enhancements::set_presentation_profile(next_profile);
        if (next_profile == dkr::runtime::enhancements::PresentationProfile::Modern) {
            config.rr_option = g_modern_refresh_mode;
            config.rr_manual_value = g_modern_refresh_target;
            config.res_option = Resolution::Auto;
            config.ar_option = AspectRatio::Expand;
            config.hpfb_option = HighPrecisionFramebuffer::On;
            resolution = static_cast<int>(config.res_option);
            aspect = static_cast<int>(config.ar_option);
            hpfb = static_cast<int>(config.hpfb_option);
            dkr::runtime::enhancements::set_maximum_detail_enabled(true);
        } else {
            config.rr_option = RefreshRate::Original;
            config.rr_manual_value = 30;
        }
        changed = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        ImGui::TextWrapped("High-refresh presentation and maximum vehicle detail. Game logic, race timers and audio keep their original speed.");
    } else {
        ImGui::TextWrapped("Original 30 FPS presentation with release-proven game and audio timing. Widescreen correctness fixes remain enabled.");
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextUnformatted("Window mode");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##window-mode", &window, "Windowed\0Fullscreen\0");
    ImGui::TextUnformatted("Internal resolution");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##internal-resolution", &resolution,
                            "Original (240p)\0Original 2x\0Automatic integer scale\0");
    ImGui::TextUnformatted("Aspect ratio");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##aspect-ratio", &aspect, "Original 4:3\0Expand to window\0");
    ImGui::TextUnformatted("Anti-aliasing");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##anti-aliasing", &aa, "None\0MSAA 2x\0MSAA 4x\0MSAA 8x\0");
    ImGui::TextUnformatted("High precision framebuffer");
    ImGui::SetNextItemWidth(setting_width);
    changed |= ImGui::Combo("##high-precision-framebuffer", &hpfb, "Automatic\0On\0Off\0");
    config.wm_option = static_cast<WindowMode>(window);
    config.res_option = static_cast<Resolution>(resolution);
    config.ar_option = static_cast<AspectRatio>(aspect);
    config.msaa_option = static_cast<Antialiasing>(aa);
    config.hpfb_option = static_cast<HighPrecisionFramebuffer>(hpfb);
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
                g_modern_refresh_target = std::clamp(g_modern_refresh_target, 30, 500);
                config.rr_manual_value = g_modern_refresh_target;
                changed = true;
            }
        }
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Intermediate presentation frames are generated without advancing physics, AI, timers, input polling or the audio mixer. Manual targets are capped by the display and available GPU performance.");
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
        ultramodern::renderer::set_graphics_config(config);
        SaveSettings();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    if (!live) {
        ImGui::TextWrapped("Changes made before launch are applied when the adventure begins.");
    }
    ImGui::TextWrapped("Graphics API: Automatic. Restart-time API selection is intentionally hidden until both backends complete release validation.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    bool maximum_detail = dkr::runtime::enhancements::maximum_detail_enabled();
    if (ImGui::Checkbox("Maximum vehicle detail", &maximum_detail)) {
        dkr::runtime::enhancements::set_maximum_detail_enabled(maximum_detail);
        SaveSettings();
        changed = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Keeps racer vehicles on their highest available model. Time-trial ghosts and gameplay logic retain their original models. Off is original N64 behaviour.");
    ImGui::PopStyleColor();
    return changed;
}

void DrawControlsReference() {
    using dkr::runtime::input::Action;
    ImGui::TextUnformatted("DRIVER BINDINGS");
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Select any binding, then press the key, button or stick direction you want. Interface navigation always keeps A, B, the D-pad and left stick as a safe recovery route.");
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

    const float available_width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    if (available_width >= 620.0F &&
        ImGui::BeginTable("controls", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
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
    if (ImGui::Button("RESTORE T.T.'S DEFAULTS", {-1.0F, 44.0F})) {
        dkr::runtime::input::reset_defaults();
        SaveSettings();
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
        float volume_percent = dkr::runtime::platform::master_volume() * 100.0F;
        ImGui::TextUnformatted("Master volume");
        ImGui::SetNextItemWidth(std::min(content_width, 720.0F));
        if (ImGui::SliderFloat("##overlay-master-volume", &volume_percent, 0.0F, 100.0F, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            dkr::runtime::platform::set_master_volume(volume_percent / 100.0F);
            SaveSettings();
        }
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
        DrawControlsReference();
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
    if (const char* test_page = std::getenv("DKR_TEST_STARTUP_PAGE")) {
        char* end = nullptr;
        const long parsed = std::strtol(test_page, &end, 10);
        if (end != test_page && *end == '\0' && parsed >= 0 && parsed <= 2) {
            page = static_cast<int>(parsed);
        }
    }
    bool running = true;
    bool launch_requested = false;
    while (running) {
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
                    page = (page + 2) % 3;
                } else if (!g_rom_browser.open &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    page = (page + 1) % 3;
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
        const bool tabs_inline = right_inner_width >= 540.0F;
        const float tab_gap = ImGui::GetStyle().ItemSpacing.x;
        const float tab_width = tabs_inline
            ? (right_inner_width - tab_gap * 2.0F) / 3.0F
            : right_inner_width;
        ImGui::PushStyleColor(ImGuiCol_Button, page == 0 ? kRaceRed : kRaceBlue);
        if (ImGui::Button("ADVENTURE", {tab_width, 46.0F})) page = 0;
        ImGui::PopStyleColor();
        if (tabs_inline) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, page == 1 ? kRaceRed : kRaceBlue);
        if (ImGui::Button("TAJ'S TUNE-UP", {tab_width, 46.0F})) page = 1;
        ImGui::PopStyleColor();
        if (tabs_inline) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, page == 2 ? kRaceRed : kRaceBlue);
        if (ImGui::Button("DRIVER GUIDE", {tab_width, 46.0F})) page = 2;
        ImGui::PopStyleColor();
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
            ImGui::BeginChild("race-pass", {right_inner_width, 218.0F}, true);
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
            float volume_percent = dkr::runtime::platform::master_volume() * 100.0F;
            ImGui::TextUnformatted("Master volume");
            ImGui::SetNextItemWidth(std::min(right_inner_width, 720.0F));
            if (ImGui::SliderFloat("##startup-master-volume", &volume_percent, 0.0F, 100.0F, "%.0f%%")) {
                dkr::runtime::platform::set_master_volume(volume_percent / 100.0F);
                SaveSettings();
            }
        } else {
            PushHeadingFont();
            ImGui::TextUnformatted("TT DRIVER GUIDE");
            PopHeadingFont();
            ImGui::TextDisabled("Keyboard or gamepad - pick your machine and hit the track.");
            ImGui::Dummy({0.0F, 12.0F});
            DrawControlsReference();
        }
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
        SidebarButton("RETURN TO THE ISLAND", 0, nav_inner_width);
        SidebarButton("TAJ'S TUNE-UP", 1, nav_inner_width);
        SidebarButton("ISLAND SOUND", 2, nav_inner_width);
        SidebarButton("T.T.'S DRIVER GUIDE", 3, nav_inner_width);
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
