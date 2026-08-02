#include "dkrport/renderer/f3ddkr/RendererSelfTest.h"

#include "dkrport/core/FileUtil.h"
#include "dkrport/renderer/f3ddkr/F3DDKRRegistry.h"

#include <sstream>

namespace dkrport::f3ddkr {

RendererTestResult RunRendererSelfTest() {
    RendererTestResult result;
    std::string registryDetails;
    result.passed = ValidateRegistry(registryDetails);
    result.details = registryDetails;

    std::ostringstream svg;
    svg << R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="960" height="540" viewBox="0 0 960 540">
<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#0a1220"/><stop offset="1" stop-color="#16263d"/></linearGradient>
  <pattern id="checker" width="24" height="24" patternUnits="userSpaceOnUse"><rect width="24" height="24" fill="#d9a72e"/><rect width="12" height="12" fill="#172941"/><rect x="12" y="12" width="12" height="12" fill="#172941"/></pattern>
</defs>
<rect width="960" height="540" rx="28" fill="url(#bg)"/>
<text x="48" y="64" fill="#f8fafc" font-family="sans-serif" font-size="30" font-weight="700">F3DDKR Structural Test Scene</text>
<text x="48" y="96" fill="#90a4be" font-family="sans-serif" font-size="16">Original test geometry — no game assets</text>
<polygon points="110,390 235,165 360,390" fill="#e5b63d" stroke="#fff1b6" stroke-width="5"/>
<text x="166" y="430" fill="#d7e2ef" font-family="sans-serif" font-size="16">G_TRIN polygon</text>
<rect x="410" y="170" width="190" height="190" rx="12" fill="url(#checker)" stroke="#f8fafc" stroke-width="5"/>
<text x="426" y="400" fill="#d7e2ef" font-family="sans-serif" font-size="16">Texture test placeholder</text>
<g transform="translate(735 270)">
  <line x1="0" y1="0" x2="120" y2="0" stroke="#ef5757" stroke-width="5"/><line x1="0" y1="0" x2="0" y2="-120" stroke="#55d68b" stroke-width="5"/><line x1="0" y1="0" x2="-75" y2="75" stroke="#58a6ff" stroke-width="5"/>
  <circle r="16" fill="#e5b63d"/>
</g>
<text x="676" y="430" fill="#d7e2ef" font-family="sans-serif" font-size="16">Matrix slots 0 / 1 / 2</text>
<rect x="48" y="474" width="864" height="2" fill="#2e4561"/>
<text x="48" y="510" fill="#90a4be" font-family="monospace" font-size="15">Billboard MoveWord index 0x02 · MVP matrix index 0x0A · DMADL opcode 0x07</text>
</svg>)SVG";
    result.svg = svg.str();
    return result;
}

bool WriteRendererTestSvg(const std::filesystem::path& path, std::string& error) {
    const auto result = RunRendererSelfTest();
    if (!result.passed) {
        error = result.details;
        return false;
    }
    return WriteTextFileAtomic(path, result.svg, error);
}

} // namespace dkrport::f3ddkr
