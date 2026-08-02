#pragma once

#include <cstddef>
#include <cstdint>

namespace dkrport {
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);
}
