#include "dkrport/core/Paths.h"

#include <cstdint>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace dkrport {
namespace {
std::filesystem::path ExecutablePath(std::string& error) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        error = "GetModuleFileNameW failed.";
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        error = "_NSGetExecutablePath failed.";
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#else
    std::string buffer(4096, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
    if (length <= 0) {
        error = "Could not resolve /proc/self/exe.";
        return {};
    }
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer);
#endif
}

std::filesystem::path EnvironmentPath(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    const errno_t result = _dupenv_s(&value, &length, name);
    if (result != 0 || value == nullptr) {
        return {};
    }

    std::filesystem::path path;
    if (length > 1U && *value != '\0') {
        path = std::filesystem::path(value);
    }
    std::free(value);
    return path;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::filesystem::path(value) : std::filesystem::path();
#endif
}
}

AppPaths ResolveAppPaths(bool forcePortable, std::string& error) {
    AppPaths paths;
    paths.executable = ExecutablePath(error);
    if (paths.executable.empty()) {
        return paths;
    }
    paths.executableDirectory = paths.executable.parent_path();
    paths.portable = forcePortable || std::filesystem::exists(paths.executableDirectory / "portable.txt") ||
                     std::filesystem::exists(std::filesystem::current_path() / "portable.txt");

    if (paths.portable) {
        const bool sourceTree = std::filesystem::exists(std::filesystem::current_path() / "CMakeLists.txt") &&
                                std::filesystem::exists(std::filesystem::current_path() / "portable.txt");
        paths.dataRoot = sourceTree ? std::filesystem::current_path() / "runtime" : paths.executableDirectory / "runtime";
    } else {
#ifdef _WIN32
        auto base = EnvironmentPath("LOCALAPPDATA");
        if (base.empty()) base = paths.executableDirectory;
        paths.dataRoot = base / "DKRPort";
#elif defined(__APPLE__)
        auto home = EnvironmentPath("HOME");
        paths.dataRoot = home.empty() ? paths.executableDirectory / "DKRPortData"
                                      : home / "Library" / "Application Support" / "DKRPort";
#else
        auto base = EnvironmentPath("XDG_DATA_HOME");
        if (base.empty()) {
            const auto home = EnvironmentPath("HOME");
            base = home.empty() ? paths.executableDirectory : home / ".local" / "share";
        }
        paths.dataRoot = base / "dkr-port";
#endif
    }

    paths.configDirectory = paths.dataRoot / "config";
    paths.gameDirectory = paths.dataRoot / "game";
    paths.logsDirectory = paths.dataRoot / "logs";
    paths.cacheDirectory = paths.dataRoot / "cache";
    paths.savesDirectory = paths.dataRoot / "saves";
    paths.screenshotsDirectory = paths.dataRoot / "screenshots";
    paths.modsDirectory = paths.dataRoot / "mods";
    return paths;
}

bool EnsureAppDirectories(const AppPaths& paths, std::string& error) {
    const std::filesystem::path directories[] = {paths.dataRoot,       paths.configDirectory, paths.gameDirectory,
                                                 paths.logsDirectory, paths.cacheDirectory,  paths.savesDirectory,
                                                 paths.screenshotsDirectory, paths.modsDirectory};
    for (const auto& directory : directories) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            error = "Could not create " + directory.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

} // namespace dkrport
