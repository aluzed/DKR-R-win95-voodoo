#include "save_manager.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("dkr-save-manager-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    dkr::runtime::saves::configure(root);
    assert(!dkr::runtime::saves::adventure_info().exists);

    std::string error;
    assert(dkr::runtime::saves::reset_adventure(error));
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

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::puts("[test][save-manager] PASS");
    return 0;
}
