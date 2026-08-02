#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime::pak {

void configure(const std::filesystem::path& config_directory);
bool enabled();
void set_enabled(bool enabled);
bool self_test(const std::filesystem::path& directory, std::string& error);

} // namespace dkr::runtime::pak
