#include "dkrport/core/ZipWriter.h"

#include "dkrport/core/Crc32.h"
#include "dkrport/core/FileUtil.h"

#include <limits>

namespace dkrport {
namespace {
void Append16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}
void Append32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    Append16(output, static_cast<std::uint16_t>(value & 0xFFFFU));
    Append16(output, static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
}
struct CentralRecord {
    const ZipEntry* entry;
    std::uint32_t crc;
    std::uint32_t localOffset;
};
}

bool WriteStoredZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries, std::string& error) {
    std::vector<std::uint8_t> output;
    std::vector<CentralRecord> records;
    for (const auto& entry : entries) {
        if (entry.name.size() > std::numeric_limits<std::uint16_t>::max() ||
            entry.data.size() > std::numeric_limits<std::uint32_t>::max() ||
            output.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "ZIP entry is too large for the simple archive writer.";
            return false;
        }
        const std::uint32_t crc = Crc32(entry.data.data(), entry.data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(output.size());
        Append32(output, 0x04034B50U);
        Append16(output, 20U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, crc);
        Append32(output, static_cast<std::uint32_t>(entry.data.size()));
        Append32(output, static_cast<std::uint32_t>(entry.data.size()));
        Append16(output, static_cast<std::uint16_t>(entry.name.size()));
        Append16(output, 0U);
        output.insert(output.end(), entry.name.begin(), entry.name.end());
        output.insert(output.end(), entry.data.begin(), entry.data.end());
        records.push_back({&entry, crc, offset});
    }

    if (output.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "ZIP central directory offset overflow.";
        return false;
    }
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(output.size());
    for (const auto& record : records) {
        const auto& entry = *record.entry;
        Append32(output, 0x02014B50U);
        Append16(output, 20U);
        Append16(output, 20U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, record.crc);
        Append32(output, static_cast<std::uint32_t>(entry.data.size()));
        Append32(output, static_cast<std::uint32_t>(entry.data.size()));
        Append16(output, static_cast<std::uint16_t>(entry.name.size()));
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, 0U);
        Append32(output, record.localOffset);
        output.insert(output.end(), entry.name.begin(), entry.name.end());
    }

    const std::uint32_t centralSize = static_cast<std::uint32_t>(output.size()) - centralOffset;
    if (records.size() > std::numeric_limits<std::uint16_t>::max()) {
        error = "Too many ZIP entries.";
        return false;
    }
    Append32(output, 0x06054B50U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, static_cast<std::uint16_t>(records.size()));
    Append16(output, static_cast<std::uint16_t>(records.size()));
    Append32(output, centralSize);
    Append32(output, centralOffset);
    Append16(output, 0U);
    return WriteBinaryFileAtomic(path, output, error);
}

} // namespace dkrport
