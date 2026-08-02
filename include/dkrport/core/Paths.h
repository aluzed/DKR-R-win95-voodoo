#pragma once

#include <filesystem>
#include <string>

namespace dkrport {

struct AppPaths {
    std::filesystem::path executable;
    std::filesystem::path executableDirectory;
    std::filesystem::path dataRoot;
    std::filesystem::path configDirectory;
    std::filesystem::path gameDirectory;
    std::filesystem::path logsDirectory;
    std::filesystem::path cacheDirectory;
    std::filesystem::path savesDirectory;
    std::filesystem::path screenshotsDirectory;
    std::filesystem::path modsDirectory;
    bool portable = false;
};

AppPaths ResolveAppPaths(bool forcePortable, std::string& error);
bool EnsureAppDirectories(const AppPaths& paths, std::string& error);

} // namespace dkrport
