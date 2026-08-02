#include "game_registration.hpp"

#include "funcs.h"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"

#include "../../RecompiledFuncs/recomp_overlays.inl"

#include <cstdint>

namespace {

constexpr std::uint64_t kRomXxh3 = 0x68512C37A6FDA951ULL;
constexpr gpr kInitialCodeAddress = static_cast<gpr>(static_cast<std::int32_t>(0x80000400U));
constexpr gpr kEntrypointStackTop = static_cast<gpr>(static_cast<std::int32_t>(0x80120AC0U));

void InitialiseEntrypointContext(std::uint8_t*, recomp_context* context) {
    // The CIC 6103/bootstrap path clears BSS and establishes this stack before
    // tail-calling mainproc. RDRAM is already zeroed by N64ModernRuntime.
    context->r29 = kEntrypointStackTop;
}

} // namespace

void dkr::runtime::RegisterGeneratedSections() {
    const recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };
    const recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };
    recomp::overlays::register_overlays(sections, overlays);
}

void dkr::runtime::RegisterGame(const std::filesystem::path& config_directory) {
    recomp::register_config_path(config_directory);
    RegisterGeneratedSections();

    const recomp::GameEntry game{
        .rom_hash = kRomXxh3,
        .internal_name = "DIDDY KONG RACING",
        .game_id = kGameId,
        .mod_game_id = "dkr",
        .save_type = recomp::SaveType::Eep4k,
        .is_enabled = true,
        .decompression_routine = nullptr,
        .has_compressed_code = false,
        .entrypoint_address = kInitialCodeAddress,
        .entrypoint = recomp_entrypoint,
        .thread_create_callback = nullptr,
        .on_init_callback = InitialiseEntrypointContext,
    };
    recomp::register_game(game);
}

bool dkr::runtime::SelectRom(const std::filesystem::path& rom_path, std::string& error) {
    std::u8string game_id{kGameId};
    const recomp::RomValidationError result = recomp::select_rom(rom_path, game_id);
    if (result == recomp::RomValidationError::Good) {
        return true;
    }

    switch (result) {
    case recomp::RomValidationError::FailedToOpen:
        error = "The ROM could not be opened.";
        break;
    case recomp::RomValidationError::NotARom:
        error = "The selected file is not a recognized N64 ROM.";
        break;
    case recomp::RomValidationError::IncorrectVersion:
        error = "The ROM is Diddy Kong Racing, but not the supported US v1.0/v77 revision.";
        break;
    case recomp::RomValidationError::IncorrectRom:
        error = "The selected ROM is not Diddy Kong Racing US v1.0/v77.";
        break;
    default:
        error = "The runtime rejected the selected ROM.";
        break;
    }
    return false;
}
