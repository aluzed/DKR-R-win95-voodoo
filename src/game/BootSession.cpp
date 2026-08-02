#include "dkrport/game/BootSession.h"

#include "dkrport/app/Application.h"

#include <algorithm>

namespace dkrport::game {
namespace {

std::uint32_t ReadBigEndian32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4U > data.size()) return 0U;
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3U]);
}

std::string ReadInternalName(const std::vector<std::uint8_t>& data) {
    if (data.size() < 0x34U) return {};
    std::string name(data.begin() + 0x20, data.begin() + 0x34);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) name.pop_back();
    return name;
}

} // namespace

bool ParseN64Header(const std::vector<std::uint8_t>& rom, N64HeaderInfo& header, std::string& error) {
    header = {};
    if (rom.size() < 0x40U) {
        error = "The normalised ROM is too small to contain a complete Nintendo 64 header.";
        return false;
    }
    if (!(rom[0] == 0x80U && rom[1] == 0x37U && rom[2] == 0x12U && rom[3] == 0x40U)) {
        error = "The boot host expected canonical big-endian Nintendo 64 ROM data.";
        return false;
    }
    header.entryPoint = ReadBigEndian32(rom, 0x08U);
    header.crc1 = ReadBigEndian32(rom, 0x10U);
    header.crc2 = ReadBigEndian32(rom, 0x14U);
    header.internalName = ReadInternalName(rom);
    return true;
}

BootSession::BootSession(Application& application) : m_application(application) {}

bool BootSession::Start(std::string& error) {
    Stop();
    m_snapshot.stage = BootStage::LoadingRom;
    m_snapshot.status = "Loading and revalidating the configured ROM";
    if (!m_application.LoadValidatedRomImage(m_rom, error)) {
        m_snapshot.stage = BootStage::Failed;
        m_snapshot.status = error;
        return false;
    }
    N64HeaderInfo header;
    if (!ParseN64Header(m_rom, header, error)) {
        m_snapshot.stage = BootStage::Failed;
        m_snapshot.status = error;
        m_rom.clear();
        return false;
    }

    m_snapshot.stage = BootStage::InitialisingMemory;
    m_snapshot.status = "Allocating the 4 MiB DKR host memory arena";
    try {
        m_rdram.assign(4U * 1024U * 1024U, 0U);
    } catch (...) {
        error = "The host could not allocate the 4 MiB RDRAM compatibility arena.";
        m_snapshot.stage = BootStage::Failed;
        m_snapshot.status = error;
        m_rom.clear();
        return false;
    }

    m_snapshot.romBytes = m_rom.size();
    m_snapshot.rdramBytes = m_rdram.size();
    m_snapshot.entryPoint = header.entryPoint;
    m_snapshot.crc1 = header.crc1;
    m_snapshot.crc2 = header.crc2;
    m_snapshot.internalName = header.internalName;
    m_snapshot.frame = 0U;
    m_snapshot.controller = {};
    m_snapshot.stage = BootStage::HostReady;
    m_snapshot.status = "ROM mapped, host memory ready, and N64 controller packets active";
    m_application.Log().Info("DKR boot host entered with " + std::to_string(m_rom.size()) + " ROM bytes.");
    return true;
}

void BootSession::Stop() {
    m_rom.clear();
    m_rdram.clear();
    m_snapshot = {};
}

void BootSession::Tick(const input::N64ControllerState& controller) {
    if (!Running()) return;
    ++m_snapshot.frame;
    m_snapshot.controller = controller;
}

bool BootSession::Running() const {
    return m_snapshot.stage == BootStage::HostReady;
}

BootSnapshot BootSession::Snapshot() const {
    return m_snapshot;
}

const char* BootStageName(BootStage stage) {
    switch (stage) {
        case BootStage::Stopped: return "STOPPED";
        case BootStage::LoadingRom: return "LOADING ROM";
        case BootStage::InitialisingMemory: return "INITIALISING MEMORY";
        case BootStage::HostReady: return "HOST READY";
        case BootStage::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

} // namespace dkrport::game
