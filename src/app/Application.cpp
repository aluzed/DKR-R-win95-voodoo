#include "dkrport/app/Application.h"

#include "dkrport/Version.h"
#include "dkrport/core/FileUtil.h"
#include "dkrport/core/ResourceSetup.h"
#include "dkrport/core/Sha1.h"
#include "dkrport/core/ZipWriter.h"
#include "dkrport/input/InputConfig.h"
#include "dkrport/game/BootSession.h"
#include "dkrport/renderer/f3ddkr/F3DDKRRegistry.h"
#include "dkrport/renderer/f3ddkr/RendererSelfTest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

namespace dkrport {
namespace {

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyStart = json.find(needle);
    if (keyStart == std::string::npos) return {};

    const std::size_t colon = json.find(':', keyStart + needle.size());
    if (colon == std::string::npos) return {};

    std::size_t valueStart = colon + 1U;
    while (valueStart < json.size() &&
           std::isspace(static_cast<unsigned char>(json[valueStart])) != 0) {
        ++valueStart;
    }
    if (valueStart >= json.size() || json[valueStart] != '"') return {};
    ++valueStart;

    std::string output;
    bool escaped = false;
    for (std::size_t index = valueStart; index < json.size(); ++index) {
        const char character = json[index];
        if (escaped) {
            switch (character) {
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case '\\': output.push_back('\\'); break;
                case '"': output.push_back('"'); break;
                default: output.push_back(character); break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return output;
        } else {
            output.push_back(character);
        }
    }
    return {};
}

bool RemoveIfExists(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        error = "Could not remove " + PathToUtf8(path) + ": " + ec.message();
        return false;
    }
    return true;
}

void AddCheck(SelfTestSummary& summary, std::string name, bool passed) {
    ++summary.total;
    if (passed) ++summary.passed;
    summary.checks.emplace_back(std::move(name), passed);
}

std::string SelfTestJson(const SelfTestSummary& summary) {
    std::ostringstream json;
    json << "{\"ok\":" << (summary.Passed() ? "true" : "false")
         << ",\"passed\":" << summary.passed << ",\"total\":" << summary.total << ",\"checks\":[";
    bool first = true;
    for (const auto& [name, passed] : summary.checks) {
        if (!first) json << ',';
        first = false;
        json << "{\"name\":\"" << JsonEscape(name) << "\",\"passed\":" << (passed ? "true" : "false") << '}';
    }
    json << "]}";
    return json.str();
}

} // namespace

Application::Application(bool forcePortable)
    : m_forcePortable(forcePortable), m_initialised(false), m_hasValidatedRom(false),
      m_placeholderArchivePresent(false) {}

bool Application::Initialise() {
    if (m_initialised) return true;

    std::string error;
    m_paths = ResolveAppPaths(m_forcePortable, error);
    if (!error.empty() || m_paths.executable.empty()) {
        std::cerr << "DKR-R could not resolve its application paths: " << error << '\n';
        return false;
    }
    if (!EnsureAppDirectories(m_paths, error)) {
        std::cerr << "DKR-R could not create its data directories: " << error << '\n';
        return false;
    }
    if (!m_logger.Initialise(m_paths.logsDirectory, error)) {
        std::cerr << "DKR-R could not initialise logging: " << error << '\n';
        return false;
    }

    m_logger.Info(std::string("DKR-R ") + DKRPORT_VERSION_STRING + " (" + DKRPORT_BUILD_MILESTONE + ")");
    m_logger.Info("Data root: " + PathToUtf8(m_paths.dataRoot));
    m_logger.Info(m_paths.portable ? "Portable mode is enabled." : "Platform user-data mode is enabled.");
    LoadExistingStatus();
    m_initialised = true;
    return true;
}

RomOperationResult Application::ValidateAndPersistRom(const std::string& path) {
    RomOperationResult operation;
    if (!Initialise()) {
        operation.error = "The application could not be initialised. Check the console or build log.";
        return operation;
    }

    const std::filesystem::path romPath = PathFromUtf8(path);
    operation.rom = ValidateRomFile(romPath);
    if (!operation.rom.supported) {
        operation.error = operation.rom.error;
        m_logger.Warning("ROM validation failed for " + PathToUtf8(romPath.filename()) +
                         ": " + operation.rom.error + " SHA-1=" + operation.rom.sha1);
        return operation;
    }

    if (!PersistSuccessfulValidation(operation.rom, romPath, operation.error)) {
        m_logger.Error("Could not persist ROM validation: " + operation.error);
        return operation;
    }

    operation.persisted = true;
    m_logger.Info("Supported ROM validated: " + operation.rom.displayName + " (" + operation.rom.sourceOrderName + ")");
    return operation;
}

bool Application::ResetGeneratedData(std::string& error) {
    if (!Initialise()) {
        error = "The application could not be initialised.";
        return false;
    }

    if (!RemoveIfExists(m_paths.gameDirectory / "manifest.json", error) ||
        !RemoveIfExists(m_paths.gameDirectory / "dkr.o2r", error) ||
        !RemoveIfExists(m_paths.configDirectory / "rom-source.json", error)) {
        m_logger.Error(error);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastRom = {};
        m_hasValidatedRom = false;
        m_placeholderArchivePresent = false;
        m_romSourcePath.clear();
    }
    m_logger.Info("Generated Milestone 0 ROM manifest data was reset.");
    return true;
}

SelfTestSummary Application::ExecuteSelfTests() {
    SelfTestSummary summary;
    if (!Initialise()) return summary;

    AddCheck(summary, "SHA-1 known vector",
             Sha1::HashHex(std::vector<std::uint8_t>{'a', 'b', 'c'}) ==
                 "a9993e364706816aba3e25717850c26c9cd0d89d");

    const std::vector<std::uint8_t> canonical = {0x80U, 0x37U, 0x12U, 0x40U, 0x11U, 0x22U, 0x33U, 0x44U};
    std::vector<std::uint8_t> v64 = canonical;
    for (std::size_t index = 0; index < v64.size(); index += 2U) std::swap(v64[index], v64[index + 1U]);
    std::vector<std::uint8_t> n64 = canonical;
    for (std::size_t index = 0; index < n64.size(); index += 4U) {
        std::swap(n64[index], n64[index + 3U]);
        std::swap(n64[index + 1U], n64[index + 2U]);
    }

    std::vector<std::uint8_t> normalised;
    std::string error;
    const bool v64Ok = DetectRomByteOrder(v64) == RomByteOrder::ByteSwappedV64 &&
                       NormaliseRom(v64, RomByteOrder::ByteSwappedV64, normalised, error) && normalised == canonical;
    AddCheck(summary, "V64 byte-order normalisation", v64Ok);

    error.clear();
    const bool n64Ok = DetectRomByteOrder(n64) == RomByteOrder::LittleEndianN64 &&
                       NormaliseRom(n64, RomByteOrder::LittleEndianN64, normalised, error) && normalised == canonical;
    AddCheck(summary, "N64 byte-order normalisation", n64Ok);

    std::string registryDetails;
    AddCheck(summary, "F3DDKR structural registry", f3ddkr::ValidateRegistry(registryDetails));
    AddCheck(summary, "Writable data directory", std::filesystem::is_directory(m_paths.dataRoot));

    const auto inputProbePath = m_paths.cacheDirectory / "self-test-controls.json";
    input::InputConfig inputProbe = input::DefaultInputConfig();
    inputProbe.deadzone = 0.24F;
    inputProbe.invertY = true;
    inputProbe.actions[static_cast<std::size_t>(input::Action::A)].keyboard = "F";
    error.clear();
    const bool inputWritten = input::SaveInputConfig(inputProbePath, inputProbe, error);
    input::InputConfig inputReloaded;
    const bool inputRoundTrip = inputWritten && input::LoadInputConfig(inputProbePath, inputReloaded, error) &&
        inputReloaded.actions[static_cast<std::size_t>(input::Action::A)].keyboard == "F" &&
        std::abs(inputReloaded.deadzone - 0.24F) < 0.001F && inputReloaded.invertY;
    AddCheck(summary, "Mappable input configuration round-trip", inputRoundTrip);
    AddCheck(summary, "N64 controller button layout",
        input::ActionButtonMask(input::Action::A) == input::kButtonA &&
        input::ActionButtonMask(input::Action::Start) == input::kButtonStart &&
        input::ActionButtonMask(input::Action::CRight) == input::kButtonCRight);

    std::vector<std::uint8_t> headerProbe(0x40U, 0U);
    headerProbe[0] = 0x80U; headerProbe[1] = 0x37U; headerProbe[2] = 0x12U; headerProbe[3] = 0x40U;
    headerProbe[8] = 0x80U; headerProbe[9] = 0x10U; headerProbe[10] = 0x04U;
    const std::string probeTitle = "DIDDY KONG RACING";
    std::copy(probeTitle.begin(), probeTitle.end(), headerProbe.begin() + 0x20);
    game::N64HeaderInfo headerInfo;
    error.clear();
    AddCheck(summary, "N64 boot-header parser",
        game::ParseN64Header(headerProbe, headerInfo, error) &&
        headerInfo.entryPoint == 0x80100400U && headerInfo.internalName == probeTitle);

    std::error_code ignored;
    std::filesystem::remove(inputProbePath, ignored);

    const auto probePath = m_paths.cacheDirectory / "self-test.zip";
    const std::string probeText = "DKR-R ZIP self-test\n";
    error.clear();
    const bool zipWritten = WriteStoredZip(probePath, {{"probe.txt", {probeText.begin(), probeText.end()}}}, error);
    std::vector<std::uint8_t> zipBytes;
    const bool zipReadable = zipWritten && ReadBinaryFile(probePath, zipBytes, error, 1024U * 1024U) &&
                             zipBytes.size() >= 4U && zipBytes[0] == 'P' && zipBytes[1] == 'K' &&
                             zipBytes[2] == 0x03U && zipBytes[3] == 0x04U;
    AddCheck(summary, "ZIP-compatible O2R writer", zipReadable);
    std::filesystem::remove(probePath, ignored);

    m_logger.Info("Self-test completed: " + std::to_string(summary.passed) + '/' + std::to_string(summary.total) + " passed.");
    return summary;
}

AppStatus Application::Status() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    AppStatus status;
    status.portable = m_paths.portable;
    status.dataDirectoryReady = std::filesystem::is_directory(m_paths.dataRoot);
    status.hasValidatedRom = m_hasValidatedRom;
    status.placeholderArchivePresent = m_placeholderArchivePresent;
    status.romSourceAvailable = !m_romSourcePath.empty() && std::filesystem::is_regular_file(m_romSourcePath);
    status.f3ddkrRegistryValid = f3ddkr::ValidateRegistry(status.f3ddkrDetails);
    status.rom = m_lastRom;
    return status;
}

int Application::ValidateRomCommand(const std::string& path) {
    const RomOperationResult result = ValidateAndPersistRom(path);
    std::cout << RomResultJson(result.rom) << '\n';
    if (!result.rom.supported) return 2;
    return result.persisted ? 0 : 3;
}

int Application::RunSelfTest(bool jsonOutput) {
    const SelfTestSummary result = ExecuteSelfTests();
    if (jsonOutput) {
        std::cout << SelfTestJson(result) << '\n';
    } else {
        std::cout << "DKR-R self-test\n";
        for (const auto& [name, passed] : result.checks) {
            std::cout << "  " << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
        }
        std::cout << result.passed << '/' << result.total << " checks passed.\n";
    }
    return result.Passed() ? 0 : 1;
}

int Application::RunRendererTest(const std::string& outputPath) {
    if (!Initialise()) return 1;
    const std::filesystem::path output = outputPath.empty()
        ? m_paths.screenshotsDirectory / "f3ddkr-structural-test.svg"
        : PathFromUtf8(outputPath);
    std::string error;
    if (!f3ddkr::WriteRendererTestSvg(output, error)) {
        m_logger.Error("Renderer structural test failed: " + error);
        return 1;
    }
    m_logger.Info("Renderer structural test written to " + PathToUtf8(output));
    std::cout << PathToUtf8(output) << '\n';
    return 0;
}

std::filesystem::path Application::ValidatedRomPath() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_romSourcePath;
}

bool Application::LoadValidatedRomImage(std::vector<std::uint8_t>& normalisedRom, std::string& error) const {
    std::filesystem::path sourcePath;
    RomValidationResult expected;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        sourcePath = m_romSourcePath;
        expected = m_lastRom;
    }
    if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath)) {
        error = "The validated ROM file could not be found. Select the ROM again from the launcher.";
        return false;
    }
    std::vector<std::uint8_t> sourceBytes;
    if (!ReadBinaryFile(sourcePath, sourceBytes, error, 128ULL * 1024ULL * 1024ULL)) return false;
    const RomByteOrder order = DetectRomByteOrder(sourceBytes);
    if (!NormaliseRom(sourceBytes, order, normalisedRom, error)) return false;
    const std::string sha1 = Sha1::HashHex(normalisedRom);
    if (LowerAscii(sha1) != LowerAscii(expected.sha1)) {
        normalisedRom.clear();
        error = "The configured ROM file no longer matches the validated Diddy Kong Racing ROM.";
        return false;
    }
    return true;
}

bool Application::PersistSuccessfulValidation(const RomValidationResult& result, const std::filesystem::path& sourcePath, std::string& error) {
    if (!WritePlaceholderResourceSet(m_paths.gameDirectory, result, DKRPORT_BUILD_MILESTONE, error)) return false;
    const std::string sourceConfig =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"path\": \"" + JsonEscape(PathToUtf8(std::filesystem::absolute(sourcePath))) + "\",\n"
        "  \"sha1\": \"" + JsonEscape(result.sha1) + "\"\n"
        "}\n";
    if (!WriteTextFileAtomic(m_paths.configDirectory / "rom-source.json", sourceConfig, error)) return false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastRom = result;
        m_hasValidatedRom = true;
        m_placeholderArchivePresent = true;
        m_romSourcePath = std::filesystem::absolute(sourcePath);
    }
    return true;
}

void Application::LoadExistingStatus() {
    const auto manifestPath = m_paths.gameDirectory / "manifest.json";
    const auto archivePath = m_paths.gameDirectory / "dkr.o2r";
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!ReadBinaryFile(manifestPath, bytes, error, 1024U * 1024U)) {
        m_logger.Info("No existing native-launcher manifest was loaded.");
        return;
    }

    const std::string manifest(bytes.begin(), bytes.end());
    const std::string sha1 = ExtractJsonString(manifest, "sha1");
    const auto supported = std::find_if(SupportedRoms().begin(), SupportedRoms().end(), [&](const SupportedRom& candidate) {
        return LowerAscii(sha1) == LowerAscii(candidate.sha1);
    });
    if (supported == SupportedRoms().end() || ExtractJsonString(manifest, "format") != "dkr-port-o2r-manifest") {
        m_logger.Warning("Existing game manifest was ignored because it is invalid or unsupported.");
        return;
    }

    RomValidationResult result;
    result.recognisedN64 = true;
    result.supported = true;
    result.sha1 = sha1;
    result.supportedRomId = supported->id;
    result.displayName = ExtractJsonString(manifest, "displayName");
    result.region = ExtractJsonString(manifest, "region");
    result.revision = ExtractJsonString(manifest, "revision");
    result.sourceOrderName = ExtractJsonString(manifest, "sourceOrder");
    result.internalName = ExtractJsonString(manifest, "internalName");

    std::error_code archiveError;
    const auto archiveSize = std::filesystem::file_size(archivePath, archiveError);
    m_lastRom = result;
    m_hasValidatedRom = true;
    m_placeholderArchivePresent = !archiveError && archiveSize > 0U;

    std::string sourceJson;
    std::string sourceError;
    if (ReadTextFile(m_paths.configDirectory / "rom-source.json", sourceJson, sourceError, 1024U * 1024U)) {
        const std::string configuredSha1 = ExtractJsonString(sourceJson, "sha1");
        const std::string configuredPath = ExtractJsonString(sourceJson, "path");
        if (LowerAscii(configuredSha1) == LowerAscii(result.sha1) && !configuredPath.empty()) {
            const std::filesystem::path candidate = PathFromUtf8(configuredPath);
            if (std::filesystem::is_regular_file(candidate)) m_romSourcePath = candidate;
        }
    }
    m_logger.Info("Loaded existing validated ROM manifest for " + result.displayName + '.');
}

} // namespace dkrport
