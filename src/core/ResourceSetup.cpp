#include "dkrport/core/ResourceSetup.h"

#include "dkrport/core/FileUtil.h"
#include "dkrport/core/ZipWriter.h"

#include <sstream>
#include <vector>

namespace dkrport {

std::string BuildPlaceholderManifestJson(const RomValidationResult& result, const std::string& milestone) {
    std::ostringstream json;
    json << "{\n"
         << "  \"format\": \"dkr-port-o2r-manifest\",\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"extractorVersion\": 0,\n"
         << "  \"milestone\": \"" << JsonEscape(milestone) << "\",\n"
         << "  \"containsGameAssets\": false,\n"
         << "  \"resourceState\": \"manifest-only-placeholder\",\n"
         << "  \"rom\": {\n"
         << "    \"id\": \"" << JsonEscape(result.supportedRomId) << "\",\n"
         << "    \"displayName\": \"" << JsonEscape(result.displayName) << "\",\n"
         << "    \"region\": \"" << JsonEscape(result.region) << "\",\n"
         << "    \"revision\": \"" << JsonEscape(result.revision) << "\",\n"
         << "    \"sourceOrder\": \"" << JsonEscape(result.sourceOrderName) << "\",\n"
         << "    \"internalName\": \"" << JsonEscape(result.internalName) << "\",\n"
         << "    \"sha1\": \"" << JsonEscape(result.sha1) << "\",\n"
         << "    \"size\": " << result.size << "\n"
         << "  }\n"
         << "}\n";
    return json.str();
}

bool WritePlaceholderResourceSet(const std::filesystem::path& gameDirectory, const RomValidationResult& result,
                                 const std::string& milestone, std::string& error) {
    if (!result.supported || result.sha1.empty() || result.supportedRomId.empty()) {
        error = "A supported ROM validation result is required to create the resource manifest.";
        return false;
    }

    const std::string manifest = BuildPlaceholderManifestJson(result, milestone);
    const std::string notice =
        "DKR Port development manifest archive\n\n"
        "This ZIP-compatible O2R contains no original Diddy Kong Racing assets.\n"
        "It records only the locally validated ROM revision and resource schema.\n"
        "The source ROM path is stored separately in local configuration and is not included here.\n"
        "Full user-side extraction will be implemented in a later milestone.\n";
    const std::vector<ZipEntry> entries = {
        {"manifest.json", {manifest.begin(), manifest.end()}},
        {"LEGAL-NOTICE.txt", {notice.begin(), notice.end()}},
    };

    const auto archivePath = gameDirectory / "dkr.o2r";
    const auto manifestPath = gameDirectory / "manifest.json";
    if (!WriteStoredZip(archivePath, entries, error)) return false;
    if (!WriteTextFileAtomic(manifestPath, manifest, error)) return false;

    std::error_code fileError;
    const auto archiveSize = std::filesystem::file_size(archivePath, fileError);
    if (fileError || archiveSize == 0U) {
        error = "The local manifest O2R archive could not be verified after writing.";
        return false;
    }
    return true;
}

} // namespace dkrport
