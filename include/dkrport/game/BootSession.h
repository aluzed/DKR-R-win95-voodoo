#pragma once

#include "dkrport/input/InputConfig.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dkrport {
class Application;
}

namespace dkrport::game {

enum class BootStage {
    Stopped,
    LoadingRom,
    InitialisingMemory,
    HostReady,
    Failed
};

struct N64HeaderInfo {
    std::uint32_t entryPoint = 0;
    std::uint32_t crc1 = 0;
    std::uint32_t crc2 = 0;
    std::string internalName;
};

bool ParseN64Header(const std::vector<std::uint8_t>& rom, N64HeaderInfo& header, std::string& error);

struct BootSnapshot {
    BootStage stage = BootStage::Stopped;
    std::uint64_t frame = 0;
    std::size_t romBytes = 0;
    std::size_t rdramBytes = 0;
    std::uint32_t entryPoint = 0;
    std::uint32_t crc1 = 0;
    std::uint32_t crc2 = 0;
    std::string internalName;
    input::N64ControllerState controller;
    std::string status;
};

class BootSession {
  public:
    explicit BootSession(Application& application);

    bool Start(std::string& error);
    void Stop();
    void Tick(const input::N64ControllerState& controller);
    [[nodiscard]] bool Running() const;
    [[nodiscard]] BootSnapshot Snapshot() const;

  private:
    Application& m_application;
    BootSnapshot m_snapshot;
    std::vector<std::uint8_t> m_rom;
    std::vector<std::uint8_t> m_rdram;
};

const char* BootStageName(BootStage stage);

} // namespace dkrport::game
