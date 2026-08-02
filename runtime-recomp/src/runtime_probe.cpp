#include "recomp.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include <cstdint>
#include <iostream>

#ifndef DKR_RUNTIME_GENERATED_FILE_COUNT
#define DKR_RUNTIME_GENERATED_FILE_COUNT 0
#endif

#ifndef DKR_RUNTIME_HAS_RT64
#define DKR_RUNTIME_HAS_RT64 0
#endif

int main() {
    static_assert(sizeof(gpr) == sizeof(std::uint64_t), "N64Recomp gpr width changed unexpectedly");
    static_assert(sizeof(recomp_context) > 0U, "N64Recomp context must be available");

    std::cout << "DKR runtime preparation probe\n";
    std::cout << "Generated CPU translation files: " << DKR_RUNTIME_GENERATED_FILE_COUNT << "\n";
    std::cout << "N64ModernRuntime targets: linked\n";
    std::cout << "RT64 target: " << (DKR_RUNTIME_HAS_RT64 ? "linked" : "not requested") << "\n";
    std::cout << "\nThis probe validates generated-code and runtime compilation only.\n";
    std::cout << "A playable boot still requires DKR-specific game registration, PI/save callbacks,\n";
    std::cout << "F3DDKR and audio RSP recompilation, and an RT64 render context.\n";
    return DKR_RUNTIME_GENERATED_FILE_COUNT > 0 ? 0 : 2;
}
