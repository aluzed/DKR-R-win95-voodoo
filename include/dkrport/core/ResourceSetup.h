#pragma once

#include "dkrport/core/Rom.h"

#include <filesystem>
#include <string>

namespace dkrport {

std::string BuildPlaceholderManifestJson(const RomValidationResult& result, const std::string& milestone);
bool WritePlaceholderResourceSet(const std::filesystem::path& gameDirectory, const RomValidationResult& result,
                                 const std::string& milestone, std::string& error);

} // namespace dkrport
