#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace RT64 {
struct Application;
}

namespace dkr::runtime::texture_packs {

enum class Format {
    NativeRt64,
    RiceRt64,
    LegacyRice,
    LegacyJabo,
    Unknown,
};

struct PackInfo {
    std::string id;
    std::string name;
    std::filesystem::path path;
    std::string zip_base_path;
    Format format = Format::Unknown;
    bool compatible = false;
    bool enabled = false;
    bool hidden = false;
    std::size_t image_count = 0;
    std::string detail;
};

void configure(const std::filesystem::path& config_directory);
void refresh();
std::vector<PackInfo> snapshot(bool include_hidden = false);
bool import_archive(const std::filesystem::path& source, std::string& status);
void set_enabled(const std::string& id, bool enabled);
bool set_hidden(const std::string& id, bool hidden, std::string& status);
bool delete_managed(const std::string& id, std::string& status);
void request_reload();
void apply_pending(RT64::Application& application, bool modern_profile);
std::string status();

const char* format_name(Format format);

} // namespace dkr::runtime::texture_packs
