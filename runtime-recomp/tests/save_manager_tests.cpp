#include "save_manager.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t pak_checksum(std::vector<std::uint8_t> bytes) {
    std::fill(bytes.begin() + 16U, bytes.begin() + 20U, 0U);
    std::uint32_t hash = 2166136261U;
    for (const std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

std::vector<std::uint8_t> valid_pak() {
    std::vector<std::uint8_t> bytes(32U * 1024U, 0U);
    const std::array<std::uint8_t, 8> magic{
        'D', 'K', 'R', 'M', 'P', 'K', '1', 0};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_u32(bytes, 8U, 1U);
    write_u32(bytes, 12U, 1U);
    write_u32(bytes, 16U, pak_checksum(bytes));
    return bytes;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("dkr-save-manager-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    dkr::runtime::saves::configure(root);
    std::fputs("[test][save-manager] adventure lifecycle\n", stderr);
    assert(!dkr::runtime::saves::adventure_info().exists);

    std::string error;
    if (!dkr::runtime::saves::reset_adventure(error)) {
        std::fprintf(stderr, "reset failed: %s\n", error.c_str());
        assert(false);
    }
    const auto info = dkr::runtime::saves::adventure_info();
    assert(info.exists && info.valid && info.size == 0x200U);

    std::filesystem::path backup;
    assert(dkr::runtime::saves::backup_adventure(backup, error));
    assert(std::filesystem::exists(backup));
    assert(!dkr::runtime::saves::adventure_backups().empty());

    const auto exported = root / "exported.bin";
    assert(dkr::runtime::saves::export_adventure(exported, error));
    assert(dkr::runtime::saves::import_adventure(exported, error));

    const auto invalid = root / "invalid.bin";
    std::ofstream(invalid, std::ios::binary).put('x');
    assert(!dkr::runtime::saves::import_adventure(invalid, error));

    std::fputs("[test][save-manager] controller pak lifecycle\n", stderr);
    const auto pak_source = root / "source.mpk";
    write_bytes(pak_source, valid_pak());
    assert(dkr::runtime::saves::import_controller_pak(0, pak_source, error));
    const auto pak_info = dkr::runtime::saves::controller_pak_info(0);
    assert(pak_info.exists && pak_info.valid && pak_info.size == 32U * 1024U);
    std::filesystem::path pak_backup;
    assert(dkr::runtime::saves::backup_controller_pak(0, pak_backup, error));
    assert(std::filesystem::exists(pak_backup));
    assert(!dkr::runtime::saves::controller_pak_backups(0).empty());
    const auto exported_pak = root / "exported.mpk";
    assert(dkr::runtime::saves::export_controller_pak(0, exported_pak, error));

    auto damaged_pak = valid_pak();
    damaged_pak[128] ^= 0x5A;
    const auto bad_pak = root / "bad.mpk";
    write_bytes(bad_pak, damaged_pak);
    assert(!dkr::runtime::saves::import_controller_pak(0, bad_pak, error));

    std::fputs("[test][save-manager] complete bundle round-trip\n", stderr);
    const auto bundle = root / "garage.dkrsave";
    assert(dkr::runtime::saves::export_bundle(bundle, error));
    assert(dkr::runtime::saves::reset_adventure(error));
    assert(dkr::runtime::saves::import_bundle(bundle, error));
    assert(dkr::runtime::saves::adventure_info().valid);
    assert(dkr::runtime::saves::controller_pak_info(0).valid);

    std::fputs("[test][save-manager] corrupt bundle rejection\n", stderr);
    std::vector<std::uint8_t> corrupt_bundle;
    {
        std::ifstream input(bundle, std::ios::binary);
        corrupt_bundle.assign(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>());
    }
    corrupt_bundle.back() ^= 1U;
    const auto bad_bundle = root / "bad.dkrsave";
    write_bytes(bad_bundle, corrupt_bundle);
    assert(!dkr::runtime::saves::import_bundle(bad_bundle, error));

    const auto oversized_bundle = root / "oversized.dkrsave";
    write_bytes(oversized_bundle, std::vector<std::uint8_t>(200U * 1024U, 0U));
    assert(!dkr::runtime::saves::import_bundle(oversized_bundle, error));

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::puts("[test][save-manager] PASS");
    return 0;
}
