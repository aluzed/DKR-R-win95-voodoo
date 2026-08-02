#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkrport {

struct ZipEntry {
    std::string name;
    std::vector<std::uint8_t> data;
};

bool WriteStoredZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries, std::string& error);

} // namespace dkrport
