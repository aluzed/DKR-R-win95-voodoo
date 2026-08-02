#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dkrport {

class Sha1 {
  public:
    Sha1();
    void Update(const std::uint8_t* data, std::size_t size);
    void Update(const std::vector<std::uint8_t>& data);
    std::string FinalHex();
    static std::string HashHex(const std::vector<std::uint8_t>& data);

  private:
    void ProcessBlock(const std::uint8_t* block);

    std::uint32_t m_state[5];
    std::uint64_t m_totalBytes;
    std::uint8_t m_buffer[64];
    std::size_t m_bufferSize;
    bool m_finalised;
};

} // namespace dkrport
