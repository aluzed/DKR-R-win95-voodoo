#include "dkrport/app/Application.h"
#include "dkrport/core/Crc32.h"
#include "dkrport/core/FileUtil.h"
#include "dkrport/core/Rom.h"
#include "dkrport/core/Paths.h"
#include "dkrport/core/ResourceSetup.h"
#include "dkrport/core/Sha1.h"
#include "dkrport/core/ZipWriter.h"
#include "dkrport/input/InputConfig.h"
#include "dkrport/game/BootSession.h"
#include "dkrport/renderer/f3ddkr/F3DDKRRegistry.h"
#include "dkrport/renderer/f3ddkr/RendererSelfTest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void Check(bool condition, const std::string& name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}
}

int main() {
    using namespace dkrport;

    Check(Sha1::HashHex(std::vector<std::uint8_t>{}) == "da39a3ee5e6b4b0d3255bfef95601890afd80709",
          "SHA-1 empty vector");
    Check(Sha1::HashHex(std::vector<std::uint8_t>{'a', 'b', 'c'}) ==
              "a9993e364706816aba3e25717850c26c9cd0d89d",
          "SHA-1 abc vector");
    const std::vector<std::uint8_t> crcInput = {'1','2','3','4','5','6','7','8','9'};
    Check(Crc32(crcInput.data(), crcInput.size()) == 0xCBF43926U, "CRC-32 known vector");

    const std::vector<std::uint8_t> canonical = {0x80U,0x37U,0x12U,0x40U,0x10U,0x20U,0x30U,0x40U};
    Check(DetectRomByteOrder(canonical) == RomByteOrder::BigEndianZ64, "Detect .z64 byte order");

    std::vector<std::uint8_t> v64 = canonical;
    for (std::size_t i = 0; i < v64.size(); i += 2U) std::swap(v64[i], v64[i + 1U]);
    Check(DetectRomByteOrder(v64) == RomByteOrder::ByteSwappedV64, "Detect .v64 byte order");
    std::vector<std::uint8_t> normalised;
    std::string error;
    Check(NormaliseRom(v64, RomByteOrder::ByteSwappedV64, normalised, error) && normalised == canonical,
          "Normalise .v64 data");

    std::vector<std::uint8_t> n64 = canonical;
    for (std::size_t i = 0; i < n64.size(); i += 4U) {
        std::swap(n64[i], n64[i + 3U]);
        std::swap(n64[i + 1U], n64[i + 2U]);
    }
    Check(DetectRomByteOrder(n64) == RomByteOrder::LittleEndianN64, "Detect .n64 byte order");
    error.clear();
    Check(NormaliseRom(n64, RomByteOrder::LittleEndianN64, normalised, error) && normalised == canonical,
          "Normalise .n64 data");

    const RomValidationResult shortResult = ValidateRomBytes({0x80U,0x37U,0x12U,0x40U}, "short.z64");
    Check(!shortResult.supported && !shortResult.error.empty(), "Reject truncated N64 file");

    std::vector<std::uint8_t> syntheticHeader(0x40U, 0U);
    syntheticHeader[0] = 0x80U; syntheticHeader[1] = 0x37U; syntheticHeader[2] = 0x12U; syntheticHeader[3] = 0x40U;
    syntheticHeader[8] = 0x80U; syntheticHeader[9] = 0x10U; syntheticHeader[10] = 0x04U; syntheticHeader[11] = 0x00U;
    syntheticHeader[0x10] = 0x12U; syntheticHeader[0x11] = 0x34U; syntheticHeader[0x12] = 0x56U; syntheticHeader[0x13] = 0x78U;
    syntheticHeader[0x14] = 0x9AU; syntheticHeader[0x15] = 0xBCU; syntheticHeader[0x16] = 0xDEU; syntheticHeader[0x17] = 0xF0U;
    const std::string syntheticTitle = "DIDDY KONG RACING";
    std::copy(syntheticTitle.begin(), syntheticTitle.end(), syntheticHeader.begin() + 0x20);
    game::N64HeaderInfo headerInfo;
    error.clear();
    Check(game::ParseN64Header(syntheticHeader, headerInfo, error) &&
              headerInfo.entryPoint == 0x80100400U && headerInfo.crc1 == 0x12345678U &&
              headerInfo.crc2 == 0x9ABCDEF0U && headerInfo.internalName == syntheticTitle,
          "Parse canonical N64 boot header");

    const input::InputConfig defaults = input::DefaultInputConfig();
    Check(defaults.actions[static_cast<std::size_t>(input::Action::A)].keyboard == "Space" &&
              defaults.actions[static_cast<std::size_t>(input::Action::Start)].gamepad == "Start",
          "Create default mappable input profile");
    Check(input::ActionButtonMask(input::Action::A) == 0x8000U &&
              input::ActionButtonMask(input::Action::Start) == 0x1000U &&
              input::ActionButtonMask(input::Action::CRight) == 0x0001U,
          "Match original N64 controller button masks");
    const auto inputConfigPath = std::filesystem::temp_directory_path() / "dkrport-tests-controls.json";
    input::InputConfig changed = defaults;
    changed.deadzone = 0.27F;
    changed.invertX = true;
    changed.actions[static_cast<std::size_t>(input::Action::Z)].keyboard = "F";
    error.clear();
    input::InputConfig loaded;
    const bool inputSaved = input::SaveInputConfig(inputConfigPath, changed, error);
    const bool inputLoaded = inputSaved && input::LoadInputConfig(inputConfigPath, loaded, error);
    Check(inputLoaded && loaded.actions[static_cast<std::size_t>(input::Action::Z)].keyboard == "F" &&
              loaded.invertX && std::abs(loaded.deadzone - 0.27F) < 0.001F,
          "Round-trip mappable input configuration");
    std::error_code inputIgnored;
    std::filesystem::remove(inputConfigPath, inputIgnored);

    std::string registryDetails;
    Check(f3ddkr::ValidateRegistry(registryDetails), "Validate F3DDKR structural registry");
    const auto rendererResult = f3ddkr::RunRendererSelfTest();
    Check(rendererResult.passed && rendererResult.svg.find("F3DDKR Structural Test Scene") != std::string::npos,
          "Generate synthetic renderer SVG");

    const auto temporary = std::filesystem::temp_directory_path() / "dkrport-tests-placeholder.o2r";
    const std::string manifest = "{\"containsGameAssets\":false}\n";
    error.clear();
    Check(WriteStoredZip(temporary, {{"manifest.json", {manifest.begin(), manifest.end()}}}, error),
          "Write ZIP-compatible O2R placeholder");
    std::vector<std::uint8_t> zip;
    Check(ReadBinaryFile(temporary, zip, error, 1024U * 1024U) && zip.size() >= 4U && zip[0] == 'P' && zip[1] == 'K',
          "Read O2R placeholder signature");
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);

    const auto resourceRoot = std::filesystem::temp_directory_path() / "dkrport-tests-resource-set";
    std::filesystem::remove_all(resourceRoot, ignored);
    RomValidationResult syntheticSupported;
    syntheticSupported.recognisedN64 = true;
    syntheticSupported.supported = true;
    syntheticSupported.sourceOrderName = "Synthetic test order";
    syntheticSupported.internalName = "SYNTHETIC TEST";
    syntheticSupported.sha1 = "0000000000000000000000000000000000000000";
    syntheticSupported.size = 64U;
    syntheticSupported.supportedRomId = "synthetic-test";
    syntheticSupported.displayName = "Synthetic test ROM";
    syntheticSupported.region = "TEST";
    syntheticSupported.revision = "0";
    error.clear();
    const bool resourceWritten = WritePlaceholderResourceSet(resourceRoot, syntheticSupported, "Unit Test", error);
    std::vector<std::uint8_t> generatedManifest;
    const bool manifestReadable = resourceWritten &&
        ReadBinaryFile(resourceRoot / "manifest.json", generatedManifest, error, 1024U * 1024U);
    const std::string generatedText(generatedManifest.begin(), generatedManifest.end());
    Check(manifestReadable && generatedText.find("\"containsGameAssets\": false") != std::string::npos &&
              generatedText.find("synthetic-test") != std::string::npos &&
              std::filesystem::exists(resourceRoot / "dkr.o2r"),
          "Create asset-free successful ROM resource set");
    std::filesystem::remove_all(resourceRoot, ignored);

    std::string pathError;
    const AppPaths reloadPaths = ResolveAppPaths(true, pathError);
    bool reloadStatusOk = pathError.empty() && EnsureAppDirectories(reloadPaths, pathError);
    if (reloadStatusOk) {
        std::filesystem::remove(reloadPaths.gameDirectory / "manifest.json", ignored);
        std::filesystem::remove(reloadPaths.gameDirectory / "dkr.o2r", ignored);
        const SupportedRom& supported = SupportedRoms().front();
        RomValidationResult persisted;
        persisted.recognisedN64 = true;
        persisted.supported = true;
        persisted.sourceOrderName = "Big-endian (.z64)";
        persisted.internalName = "DIDDY KONG RACING";
        persisted.sha1 = supported.sha1;
        persisted.size = supported.expectedSize;
        persisted.supportedRomId = supported.id;
        persisted.displayName = supported.displayName;
        persisted.region = supported.region;
        persisted.revision = supported.revision;
        reloadStatusOk = WritePlaceholderResourceSet(reloadPaths.gameDirectory, persisted, "Reload Test", pathError);
        if (reloadStatusOk) {
            Application reloaded(true);
            reloadStatusOk = reloaded.Initialise();
            const AppStatus status = reloadStatusOk ? reloaded.Status() : AppStatus{};
            reloadStatusOk = reloadStatusOk && status.hasValidatedRom && status.placeholderArchivePresent &&
                             status.rom.sha1 == supported.sha1;
        }
        std::filesystem::remove(reloadPaths.gameDirectory / "manifest.json", ignored);
        std::filesystem::remove(reloadPaths.gameDirectory / "dkr.o2r", ignored);
    }
    Check(reloadStatusOk, "Reload formatted native-launcher manifest");

    const auto atomicPath = std::filesystem::temp_directory_path() /
                            PathFromUtf8("dkrport-tests-\xE2\x80\x93-atomic.txt");
    error.clear();
    const bool firstWrite = WriteTextFileAtomic(atomicPath, "first", error);
    const bool secondWrite = firstWrite && WriteTextFileAtomic(atomicPath, "second", error);
    std::vector<std::uint8_t> atomicBytes;
    const bool replaced = secondWrite && ReadBinaryFile(atomicPath, atomicBytes, error, 1024U) &&
                          std::string(atomicBytes.begin(), atomicBytes.end()) == "second";
    Check(replaced, "Atomically replace generated text file");
    std::filesystem::remove(atomicPath, ignored);

    Check(JsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n", "Escape JSON strings");
    const std::string utf8Name = "Diddy Kong Racing \xE2\x80\x93 test.z64";
    Check(PathToUtf8(PathFromUtf8(utf8Name)) == utf8Name, "Round-trip UTF-8 paths");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
}
