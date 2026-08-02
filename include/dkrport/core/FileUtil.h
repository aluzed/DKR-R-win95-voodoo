#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkrport {

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::uint8_t>& data, std::string& error,
                    std::uint64_t maxBytes = 64ULL * 1024ULL * 1024ULL);
bool ReadTextFile(const std::filesystem::path& path, std::string& text, std::string& error,
                  std::uint64_t maxBytes = 4ULL * 1024ULL * 1024ULL);
bool WriteBinaryFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& data,
                           std::string& error);
bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& text, std::string& error);
std::string JsonEscape(const std::string& input);
std::filesystem::path PathFromUtf8(const std::string& input);
std::string PathToUtf8(const std::filesystem::path& path);
std::string LowerAscii(std::string value);

} // namespace dkrport
