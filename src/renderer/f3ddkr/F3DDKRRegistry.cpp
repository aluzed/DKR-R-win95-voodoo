#include "dkrport/renderer/f3ddkr/F3DDKRRegistry.h"

#include "dkrport/core/FileUtil.h"

#include <set>
#include <sstream>

namespace dkrport::f3ddkr {

static_assert(kBillboardMoveWordIndex == 0x02U, "F3DDKR billboard MoveWord index differs from the decomp header.");
static_assert(kMvpMatrixMoveWordIndex == 0x0AU, "F3DDKR MVP matrix MoveWord index differs from the decomp header.");

const std::vector<CommandDescriptor>& CommandRegistry() {
    static const std::vector<CommandDescriptor> registry = {
        {0x01U, "G_MTX / gSPMatrixDKR", "Load one of DKR's indexed matrices", false},
        {0x04U, "G_VTX / gSPVertexDKR", "Load or append vertices using the F3DDKR vertex layout", false},
        {kPolygonOpcode, "G_TRIN / gSPPolygon", "Draw DKR polygon/triangle batches", false},
        {kDmaDisplayListOpcode, "G_DMADL / gDkrDmaDisplayList", "DMA and execute a display-list block", false},
        {0xBCU, "G_MOVEWORD", "Select MVP matrix and toggle billboarding", false},
    };
    return registry;
}

bool ValidateRegistry(std::string& details) {
    std::set<std::uint8_t> opcodes;
    bool polygonFound = false;
    bool dmaFound = false;
    for (const auto& command : CommandRegistry()) {
        if (!opcodes.insert(command.opcode).second) {
            details = "Duplicate F3DDKR opcode registered: " + std::to_string(command.opcode);
            return false;
        }
        polygonFound = polygonFound || command.opcode == kPolygonOpcode;
        dmaFound = dmaFound || command.opcode == kDmaDisplayListOpcode;
    }
    if (!polygonFound || !dmaFound) {
        details = "The two confirmed DKR-specific command opcodes were not registered.";
        return false;
    }
    details = "Registry is structurally valid. Command handlers remain intentionally unimplemented in Milestone 0.";
    return true;
}

std::string RegistryJson() {
    std::ostringstream output;
    output << "[";
    bool first = true;
    for (const auto& command : CommandRegistry()) {
        if (!first) output << ',';
        first = false;
        output << "{\"opcode\":" << static_cast<unsigned int>(command.opcode)
               << ",\"name\":\"" << JsonEscape(command.name) << "\""
               << ",\"purpose\":\"" << JsonEscape(command.purpose) << "\""
               << ",\"implemented\":" << (command.implemented ? "true" : "false") << "}";
    }
    output << "]";
    return output.str();
}

} // namespace dkrport::f3ddkr
