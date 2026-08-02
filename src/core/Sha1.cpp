#include "dkrport/core/Sha1.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace dkrport {
namespace {
std::uint32_t RotateLeft(std::uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32U - bits));
}
}

Sha1::Sha1()
    : m_state{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U}, m_totalBytes(0),
      m_buffer{}, m_bufferSize(0), m_finalised(false) {
}

void Sha1::ProcessBlock(const std::uint8_t* block) {
    std::uint32_t words[80]{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t offset = i * 4;
        words[i] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                   (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                   (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                   static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t i = 16; i < 80; ++i) {
        words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1U);
    }

    std::uint32_t a = m_state[0];
    std::uint32_t b = m_state[1];
    std::uint32_t c = m_state[2];
    std::uint32_t d = m_state[3];
    std::uint32_t e = m_state[4];

    for (std::size_t i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        const std::uint32_t temp = RotateLeft(a, 5U) + f + e + k + words[i];
        e = d;
        d = c;
        c = RotateLeft(b, 30U);
        b = a;
        a = temp;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

void Sha1::Update(const std::uint8_t* data, std::size_t size) {
    if (m_finalised) {
        throw std::logic_error("SHA-1 instance has already been finalised");
    }
    if (data == nullptr && size != 0) {
        throw std::invalid_argument("SHA-1 data pointer is null");
    }

    m_totalBytes += static_cast<std::uint64_t>(size);
    std::size_t consumed = 0;
    while (consumed < size) {
        const std::size_t available = 64U - m_bufferSize;
        const std::size_t amount = std::min(available, size - consumed);
        std::memcpy(m_buffer + m_bufferSize, data + consumed, amount);
        m_bufferSize += amount;
        consumed += amount;
        if (m_bufferSize == 64U) {
            ProcessBlock(m_buffer);
            m_bufferSize = 0;
        }
    }
}

void Sha1::Update(const std::vector<std::uint8_t>& data) {
    Update(data.data(), data.size());
}

std::string Sha1::FinalHex() {
    if (!m_finalised) {
        const std::uint64_t bitLength = m_totalBytes * 8U;
        m_buffer[m_bufferSize++] = 0x80U;
        if (m_bufferSize > 56U) {
            while (m_bufferSize < 64U) {
                m_buffer[m_bufferSize++] = 0;
            }
            ProcessBlock(m_buffer);
            m_bufferSize = 0;
        }
        while (m_bufferSize < 56U) {
            m_buffer[m_bufferSize++] = 0;
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            m_buffer[m_bufferSize++] = static_cast<std::uint8_t>((bitLength >> static_cast<unsigned int>(shift)) & 0xFFU);
        }
        ProcessBlock(m_buffer);
        m_bufferSize = 0;
        m_finalised = true;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t value : m_state) {
        output << std::setw(8) << value;
    }
    return output.str();
}

std::string Sha1::HashHex(const std::vector<std::uint8_t>& data) {
    Sha1 hash;
    hash.Update(data);
    return hash.FinalHex();
}

} // namespace dkrport
