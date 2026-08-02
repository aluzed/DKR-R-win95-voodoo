#pragma once

#include <filesystem>
#include <string>

namespace dkrport::f3ddkr {

struct RendererTestResult {
    bool passed = false;
    std::string details;
    std::string svg;
};

RendererTestResult RunRendererSelfTest();
bool WriteRendererTestSvg(const std::filesystem::path& path, std::string& error);

} // namespace dkrport::f3ddkr
