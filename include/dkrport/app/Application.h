#pragma once

#include "dkrport/core/Logger.h"
#include "dkrport/core/Paths.h"
#include "dkrport/core/Rom.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace dkrport {

struct SelfTestSummary {
    int passed = 0;
    int total = 0;
    std::vector<std::pair<std::string, bool>> checks;

    [[nodiscard]] bool Passed() const { return total > 0 && passed == total; }
};

struct AppStatus {
    bool portable = false;
    bool dataDirectoryReady = false;
    bool hasValidatedRom = false;
    bool placeholderArchivePresent = false;
    bool romSourceAvailable = false;
    bool f3ddkrRegistryValid = false;
    std::string f3ddkrDetails;
    RomValidationResult rom;
};

struct RomOperationResult {
    RomValidationResult rom;
    bool persisted = false;
    std::string error;
};

class Application {
  public:
    explicit Application(bool forcePortable);

    bool Initialise();
    RomOperationResult ValidateAndPersistRom(const std::string& path);
    bool ResetGeneratedData(std::string& error);
    SelfTestSummary ExecuteSelfTests();
    AppStatus Status() const;

    int ValidateRomCommand(const std::string& path);
    int RunSelfTest(bool jsonOutput);
    int RunRendererTest(const std::string& outputPath);
    bool LoadValidatedRomImage(std::vector<std::uint8_t>& normalisedRom, std::string& error) const;

    [[nodiscard]] const AppPaths& Paths() const { return m_paths; }
    [[nodiscard]] Logger& Log() { return m_logger; }
    [[nodiscard]] const Logger& Log() const { return m_logger; }
    [[nodiscard]] std::filesystem::path ValidatedRomPath() const;

  private:
    bool PersistSuccessfulValidation(const RomValidationResult& result, const std::filesystem::path& sourcePath, std::string& error);
    void LoadExistingStatus();

    bool m_forcePortable;
    bool m_initialised;
    AppPaths m_paths;
    Logger m_logger;
    mutable std::mutex m_stateMutex;
    RomValidationResult m_lastRom;
    bool m_hasValidatedRom;
    bool m_placeholderArchivePresent;
    std::filesystem::path m_romSourcePath;
};

} // namespace dkrport
