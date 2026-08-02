#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dkrport::f3ddkr {

struct CommandDescriptor {
    std::uint8_t opcode;
    const char* name;
    const char* purpose;
    bool implemented;
};

const std::vector<CommandDescriptor>& CommandRegistry();
bool ValidateRegistry(std::string& details);
std::string RegistryJson();

constexpr std::uint8_t kPolygonOpcode = 0x05;
constexpr std::uint8_t kDmaDisplayListOpcode = 0x07;
constexpr std::uint8_t kBillboardMoveWordIndex = 0x02;
constexpr std::uint8_t kMvpMatrixMoveWordIndex = 0x0A;

} // namespace dkrport::f3ddkr
