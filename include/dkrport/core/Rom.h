#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkrport {

enum class RomByteOrder {
    Unknown,
    BigEndianZ64,
    ByteSwappedV64,
    LittleEndianN64
};

struct SupportedRom {
    const char* id;
    const char* displayName;
    const char* region;
    const char* revision;
    const char* sha1;
    std::uint64_t expectedSize;
};

struct RomValidationResult {
    bool recognisedN64 = false;
    bool supported = false;
    RomByteOrder sourceOrder = RomByteOrder::Unknown;
    std::string sourceOrderName;
    std::string fileName;
    std::string internalName;
    std::string sha1;
    std::uint64_t size = 0;
    std::string supportedRomId;
    std::string displayName;
    std::string region;
    std::string revision;
    std::string error;
};

RomByteOrder DetectRomByteOrder(const std::vector<std::uint8_t>& data);
bool NormaliseRom(const std::vector<std::uint8_t>& input, RomByteOrder order, std::vector<std::uint8_t>& output,
                  std::string& error);
RomValidationResult ValidateRomBytes(const std::vector<std::uint8_t>& bytes, const std::string& fileName);
RomValidationResult ValidateRomFile(const std::filesystem::path& path);
const std::vector<SupportedRom>& SupportedRoms();
std::string RomByteOrderName(RomByteOrder order);
std::string RomResultJson(const RomValidationResult& result);

} // namespace dkrport
