#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkr::runtime::saves {

struct SaveInfo {
    std::filesystem::path path;
    bool exists = false;
    bool valid = false;
    std::uintmax_t size = 0;
};

constexpr int kControllerPakCount = 4;

void configure(const std::filesystem::path& config_directory);
SaveInfo adventure_info();
std::filesystem::path backup_directory();
std::vector<std::filesystem::path> adventure_backups();
bool backup_adventure(std::filesystem::path& created, std::string& error);
bool export_adventure(const std::filesystem::path& destination, std::string& error);
bool import_adventure(const std::filesystem::path& source, std::string& error);
bool reset_adventure(std::string& error);

SaveInfo controller_pak_info(int channel);
std::vector<std::filesystem::path> controller_pak_backups(int channel);
bool backup_controller_pak(int channel, std::filesystem::path& created,
                           std::string& error);
bool export_controller_pak(int channel,
                           const std::filesystem::path& destination,
                           std::string& error);
bool import_controller_pak(int channel, const std::filesystem::path& source,
                           std::string& error);

// A DKR Port bundle contains only fixed, typed save-image records. It has no
// filenames or extraction paths, so importing cannot traverse outside the
// configured save directory. Adventure EEPROM and all present Controller Paks
// are validated before any live file is replaced.
bool export_bundle(const std::filesystem::path& destination, std::string& error);
bool import_bundle(const std::filesystem::path& source, std::string& error);

} // namespace dkr::runtime::saves
