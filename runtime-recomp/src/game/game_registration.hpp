#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime {

inline constexpr char8_t kGameId[] = u8"dkr.us.v77";

void RegisterGame(const std::filesystem::path& config_directory);
bool SelectRom(const std::filesystem::path& rom_path, std::string& error);
void RegisterGeneratedSections();

} // namespace dkr::runtime
