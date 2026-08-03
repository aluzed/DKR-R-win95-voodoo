#include "save_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <vector>

namespace {

constexpr std::size_t kAdventureSaveSize = 0x200U;
std::filesystem::path g_config_directory;
std::mutex g_save_manager_mutex;

std::filesystem::path AdventurePath() {
    return g_config_directory / "saves" / "dkr.us.v77.bin";
}

bool ReadAdventure(const std::filesystem::path& path,
                   std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return input.eof() && bytes.size() == kAdventureSaveSize;
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S")
           << '-' << std::setw(3) << std::setfill('0') << milliseconds.count();
    return output.str();
}

bool WriteAtomic(const std::filesystem::path& destination,
                 const std::vector<std::uint8_t>& bytes, std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the save folder: " + filesystem_error.message();
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".importing";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create the temporary save file.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "The temporary save file could not be written completely.";
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    std::vector<std::uint8_t> check;
    if (!ReadAdventure(temporary, check)) {
        error = "The temporary save failed validation.";
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    std::filesystem::remove(destination, filesystem_error);
    filesystem_error.clear();
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
        error = "Could not activate the imported save: " + filesystem_error.message();
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    return true;
}

bool BackupUnlocked(std::filesystem::path& created, std::string& error) {
    const auto source = AdventurePath();
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(source, bytes)) {
        error = "No valid 512-byte Adventure save is available to back up.";
        return false;
    }
    std::error_code filesystem_error;
    const auto directory = g_config_directory / "save-backups";
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup garage: " + filesystem_error.message();
        return false;
    }
    created = directory / ("adventure-" + Timestamp() + ".bin");
    std::filesystem::copy_file(source, created,
                               std::filesystem::copy_options::overwrite_existing,
                               filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace

void dkr::runtime::saves::configure(
    const std::filesystem::path& config_directory) {
    std::scoped_lock lock(g_save_manager_mutex);
    g_config_directory = config_directory;
}

dkr::runtime::saves::SaveInfo dkr::runtime::saves::adventure_info() {
    std::scoped_lock lock(g_save_manager_mutex);
    SaveInfo info{};
    info.path = AdventurePath();
    std::error_code error;
    info.exists = std::filesystem::exists(info.path, error);
    if (info.exists && !error) {
        info.size = std::filesystem::file_size(info.path, error);
        std::vector<std::uint8_t> bytes;
        info.valid = !error && ReadAdventure(info.path, bytes);
    }
    return info;
}

std::filesystem::path dkr::runtime::saves::backup_directory() {
    std::scoped_lock lock(g_save_manager_mutex);
    return g_config_directory / "save-backups";
}

std::vector<std::filesystem::path> dkr::runtime::saves::adventure_backups() {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::filesystem::path> result;
    std::error_code error;
    const auto directory = g_config_directory / "save-backups";
    if (!std::filesystem::is_directory(directory, error)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied,
             error)) {
        if (entry.is_regular_file(error) &&
            entry.path().filename().string().rfind("adventure-", 0) == 0 &&
            entry.path().extension() == ".bin") {
            std::vector<std::uint8_t> bytes;
            if (ReadAdventure(entry.path(), bytes)) {
                result.push_back(entry.path());
            }
        }
        error.clear();
    }
    std::sort(result.begin(), result.end(), std::greater<>());
    return result;
}

bool dkr::runtime::saves::backup_adventure(std::filesystem::path& created,
                                            std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    return BackupUnlocked(created, error);
}

bool dkr::runtime::saves::export_adventure(
    const std::filesystem::path& destination, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(AdventurePath(), bytes)) {
        error = "No valid Adventure save is available to export.";
        return false;
    }
    return WriteAtomic(destination, bytes, error);
}

bool dkr::runtime::saves::import_adventure(
    const std::filesystem::path& source, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(source, bytes)) {
        error = "That file is not a valid 512-byte DKR Adventure save.";
        return false;
    }
    std::error_code equivalent_error;
    if (std::filesystem::exists(AdventurePath(), equivalent_error)) {
        std::filesystem::path backup;
        if (!BackupUnlocked(backup, error)) {
            return false;
        }
    }
    return WriteAtomic(AdventurePath(), bytes, error);
}

bool dkr::runtime::saves::reset_adventure(std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::error_code exists_error;
    if (std::filesystem::exists(AdventurePath(), exists_error)) {
        std::filesystem::path backup;
        if (!BackupUnlocked(backup, error)) {
            return false;
        }
    }
    return WriteAtomic(AdventurePath(),
                       std::vector<std::uint8_t>(kAdventureSaveSize, 0U), error);
}
