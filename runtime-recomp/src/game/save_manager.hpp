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

void configure(const std::filesystem::path& config_directory);
SaveInfo adventure_info();
std::filesystem::path backup_directory();
std::vector<std::filesystem::path> adventure_backups();
bool backup_adventure(std::filesystem::path& created, std::string& error);
bool export_adventure(const std::filesystem::path& destination, std::string& error);
bool import_adventure(const std::filesystem::path& source, std::string& error);
bool reset_adventure(std::string& error);

} // namespace dkr::runtime::saves
