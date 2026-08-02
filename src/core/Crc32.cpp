#include "dkrport/core/Crc32.h"

#include <array>

namespace dkrport {
namespace {
const std::array<std::uint32_t, 256>& Table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256U; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) != 0U ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
            }
            values[i] = value;
        }
        return values;
    }();
    return table;
}
}

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto& table = Table();
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8U);
    }
    return crc ^ 0xFFFFFFFFU;
}

} // namespace dkrport
