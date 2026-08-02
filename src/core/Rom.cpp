#include "dkrport/core/Rom.h"

#include "dkrport/core/FileUtil.h"
#include "dkrport/core/Sha1.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dkrport {
namespace {
constexpr std::uint64_t kMaximumRomSize = 64ULL * 1024ULL * 1024ULL;

std::string ExtractInternalName(const std::vector<std::uint8_t>& data) {
    if (data.size() < 0x34U) return {};
    std::string name;
    for (std::size_t i = 0x20U; i < 0x34U; ++i) {
        const unsigned char character = data[i];
        if (character >= 0x20U && character <= 0x7EU) name.push_back(static_cast<char>(character));
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name;
}
}

const std::vector<SupportedRom>& SupportedRoms() {
    static const std::vector<SupportedRom> roms = {
        {"dkr-us-v77", "Diddy Kong Racing — US 1.0", "US", "1.0 / v77",
         "0cb115d8716dbbc2922fda38e533b9fe63bb9670", 12ULL * 1024ULL * 1024ULL},
    };
    return roms;
}

RomByteOrder DetectRomByteOrder(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4U) return RomByteOrder::Unknown;
    if (data[0] == 0x80U && data[1] == 0x37U && data[2] == 0x12U && data[3] == 0x40U) {
        return RomByteOrder::BigEndianZ64;
    }
    if (data[0] == 0x37U && data[1] == 0x80U && data[2] == 0x40U && data[3] == 0x12U) {
        return RomByteOrder::ByteSwappedV64;
    }
    if (data[0] == 0x40U && data[1] == 0x12U && data[2] == 0x37U && data[3] == 0x80U) {
        return RomByteOrder::LittleEndianN64;
    }
    return RomByteOrder::Unknown;
}

std::string RomByteOrderName(RomByteOrder order) {
    switch (order) {
        case RomByteOrder::BigEndianZ64: return "Big-endian (.z64)";
        case RomByteOrder::ByteSwappedV64: return "Byte-swapped (.v64)";
        case RomByteOrder::LittleEndianN64: return "Little-endian (.n64)";
        default: return "Unknown";
    }
}

bool NormaliseRom(const std::vector<std::uint8_t>& input, RomByteOrder order, std::vector<std::uint8_t>& output,
                  std::string& error) {
    output = input;
    if (order == RomByteOrder::BigEndianZ64) return true;
    if (order == RomByteOrder::ByteSwappedV64) {
        if ((output.size() % 2U) != 0U) {
            error = "Byte-swapped ROM has an odd file length.";
            return false;
        }
        for (std::size_t i = 0; i < output.size(); i += 2U) std::swap(output[i], output[i + 1U]);
        return true;
    }
    if (order == RomByteOrder::LittleEndianN64) {
        if ((output.size() % 4U) != 0U) {
            error = "Little-endian ROM length is not divisible by four.";
            return false;
        }
        for (std::size_t i = 0; i < output.size(); i += 4U) {
            std::swap(output[i], output[i + 3U]);
            std::swap(output[i + 1U], output[i + 2U]);
        }
        return true;
    }
    error = "The file does not contain a recognised Nintendo 64 ROM header.";
    return false;
}

RomValidationResult ValidateRomBytes(const std::vector<std::uint8_t>& bytes, const std::string& fileName) {
    RomValidationResult result;
    result.fileName = fileName;
    result.size = static_cast<std::uint64_t>(bytes.size());
    if (bytes.size() < 0x40U) {
        result.error = "The selected file is too small to be a complete Nintendo 64 ROM.";
        return result;
    }
    if (bytes.size() > kMaximumRomSize) {
        result.error = "The selected file is larger than the configured Nintendo 64 ROM safety limit.";
        return result;
    }

    result.sourceOrder = DetectRomByteOrder(bytes);
    result.sourceOrderName = RomByteOrderName(result.sourceOrder);
    result.recognisedN64 = result.sourceOrder != RomByteOrder::Unknown;
    if (!result.recognisedN64) {
        result.error = "The file does not have a recognised .z64, .v64 or .n64 header.";
        return result;
    }

    std::vector<std::uint8_t> canonical;
    if (!NormaliseRom(bytes, result.sourceOrder, canonical, result.error)) return result;
    result.sha1 = Sha1::HashHex(canonical);
    result.internalName = ExtractInternalName(canonical);

    for (const auto& supported : SupportedRoms()) {
        if (LowerAscii(result.sha1) == LowerAscii(supported.sha1)) {
            result.supported = true;
            result.supportedRomId = supported.id;
            result.displayName = supported.displayName;
            result.region = supported.region;
            result.revision = supported.revision;
            if (supported.expectedSize != 0U && result.size != supported.expectedSize) {
                result.error = "ROM checksum matched, but the file length was unexpected.";
                result.supported = false;
            }
            return result;
        }
    }

    result.error = "This is a Nintendo 64 ROM, but it does not match a currently supported Diddy Kong Racing revision.";
    return result;
}

RomValidationResult ValidateRomFile(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!ReadBinaryFile(path, bytes, error)) {
        RomValidationResult result;
        result.fileName = PathToUtf8(path.filename());
        result.error = error;
        return result;
    }
    return ValidateRomBytes(bytes, PathToUtf8(path.filename()));
}

std::string RomResultJson(const RomValidationResult& result) {
    std::ostringstream output;
    output << "{"
           << "\"recognisedN64\":" << (result.recognisedN64 ? "true" : "false") << ','
           << "\"supported\":" << (result.supported ? "true" : "false") << ','
           << "\"sourceOrder\":\"" << JsonEscape(result.sourceOrderName) << "\","
           << "\"fileName\":\"" << JsonEscape(result.fileName) << "\","
           << "\"internalName\":\"" << JsonEscape(result.internalName) << "\","
           << "\"sha1\":\"" << JsonEscape(result.sha1) << "\","
           << "\"size\":" << result.size << ','
           << "\"supportedRomId\":\"" << JsonEscape(result.supportedRomId) << "\","
           << "\"displayName\":\"" << JsonEscape(result.displayName) << "\","
           << "\"region\":\"" << JsonEscape(result.region) << "\","
           << "\"revision\":\"" << JsonEscape(result.revision) << "\","
           << "\"error\":\"" << JsonEscape(result.error) << "\""
           << "}";
    return output.str();
}

} // namespace dkrport
