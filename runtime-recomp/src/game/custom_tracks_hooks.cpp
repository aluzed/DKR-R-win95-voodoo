#include "custom_tracks.hpp"

#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "rom_revision.hpp"

#include "recomp.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Patch Pipeline side of custom track support. The pure table arithmetic lives
// in custom_tracks.cpp and is unit tested; this file only moves it across the
// recompiled boundary.
namespace {

// Both section indices are literals at the retail call sites in
// level_global_init, so they never have to be discovered at runtime:
//
//   8006a6dc  jal  asset_table_load
//   8006a6e0  li   a0,22            <- ASSET_LEVEL_HEADERS_TABLE
//
//   8006a7dc  li   a0,23            <- ASSET_LEVEL_HEADERS
//   8006a7e0  jal  asset_load
//   8006a7e4  li   a3,196           <- sizeof(LevelHeader)
constexpr std::uint32_t kLevelHeadersTableSection = 22U;
constexpr std::uint32_t kLevelHeadersSection = 23U;

// AssetSectionsEnum in the matching decomp's include/asset_enums.h. Each level
// aspect is a (table, data) pair, and the two indices above anchor the numbering
// because level_global_init loads them as literals.
//
//   2/3 3D textures   20/21 object maps   22/23 headers   24/25 names
//   26/27 models
//
// The texture pair is the same numbering read from the other end: the decomp's
// section list runs ASSET_AI_BEHAVIOUR, ASSET_AI_BEHAVIOUR_TABLE,
// ASSET_TEXTURES_3D, ASSET_TEXTURES_3D_TABLE, so the data section is 2 and its
// table is 3 - note the order is data-then-table here, the reverse of the level
// pairs. textures_sprites.c reaches it as
// asset_table_load(ASSET_TEXTURES_3D_TABLE) at boot and asset_load with
// ASSET_TEXTURES_3D per texture, which is the same pair of hooks below.
using dkr::runtime::custom_tracks::Section;

struct SectionMapping {
    std::uint32_t table_index;
    std::uint32_t data_index;
    Section section;
};

constexpr SectionMapping kSectionMappings[] = {
    {3U, 2U, Section::Textures3D},
    {20U, 21U, Section::LevelObjectMaps},
    {22U, 23U, Section::LevelHeaders},
    {24U, 25U, Section::LevelNames},
    {26U, 27U, Section::LevelModels},
};

bool section_for_table(std::uint32_t index, Section& out) {
    for (const SectionMapping& mapping : kSectionMappings) {
        if (mapping.table_index == index) {
            out = mapping.section;
            return true;
        }
    }
    return false;
}

bool section_for_data(std::uint32_t index, Section& out) {
    for (const SectionMapping& mapping : kSectionMappings) {
        if (mapping.data_index == index) {
            out = mapping.section;
            return true;
        }
    }
    return false;
}

// Only the indices below are runtime-assigned. Everything else in the header, skybox
// included, is the author's to write. `geometry` is left alone unless the track
// actually ships a model, so a Phase 1 remix keeps pointing at the retail
// geometry it was built on.
// init_track spawns from both object maps:
//
//   init_track(geometry, skybox, players, vehicle, entrance,
//              header->collectables,   // 0x36 -> track_spawn_objects(.., 1)
//              header->unkBA);         // 0xBA -> track_spawn_objects(.., 0)
//
// Ancient Lake is 98 structural objects in map 5 and 86 collectables in map
// 73. Patching only one leaves the other pointing at retail, so the original
// objects keep spawning next to the author's.
using dkr::runtime::custom_tracks::MapSlot;

struct HeaderFixup {
    std::int32_t offset;
    Section section;
    MapSlot slot;
    const char* label;
};

constexpr HeaderFixup kHeaderFixups[] = {
    {0x34, Section::LevelModels, MapSlot::None, "model"},
    {0x36, Section::LevelObjectMaps, MapSlot::Collectables, "collectables map"},
    {0xBA, Section::LevelObjectMaps, MapSlot::Structure, "structure map"},
};

// asset_table_load allocates its result with this tag; the extended table is
// allocated the same way so it is reclaimed on the identical pool lifecycle.
//   80076c94  lui a1,0x7f7f
//   80076ca0  ori a1,a1,0x7fff
constexpr std::uint32_t kColourTagGrey = 0x7F7F7FFFU;

// mode_menu starts a level when menu_loop returns MENU_RESULT_FLAGS_200 with
// the map id in the low seven bits (menu.h, thread3_main.c).
constexpr std::int32_t kMenuResultStartLevel = 1 << 9;
constexpr std::int32_t kMenuResultMapMask = 0x7F;

// Character enum order in the matching decomp's include/enums.h.
constexpr std::uint8_t kCharacterDiddy = 9U;

constexpr std::uint32_t kRdramLow = 0x80000000U;
constexpr std::uint32_t kRdramHigh = 0x807FFFB6U;
constexpr std::uint32_t kMaxTableEntries = 4096U;

// DKR serialises asset DMA on one thread, and Rev A additionally guards it
// with a one-token queue, so neither loader is reentrant. Recording the
// arguments at entry and consuming them at the epilogue is therefore safe.
std::uint32_t g_requested_table = 0xFFFFFFFFU;

struct AssetLoadRequest {
    std::uint32_t section = 0xFFFFFFFFU;
    std::uint32_t destination = 0;
    std::uint32_t offset = 0;
    std::int32_t size = 0;
};
AssetLoadRequest g_load;

gpr rdram_address(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool addressable(std::uint32_t address) {
    return address >= kRdramLow && address <= kRdramHigh;
}

std::int32_t read_word(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int32_t>(MEM_W(0, rdram_address(address)));
}

void write_word(std::uint8_t* rdram, std::uint32_t address,
                std::int32_t value) {
    MEM_W(0, rdram_address(address)) = static_cast<std::uint32_t>(value);
}

} // namespace

// Entry of asset_table_load. Only the requested section is recorded; the
// argument register is reused later in the function, so the epilogue can no
// longer recover it.
extern "C" void dkr_custom_tracks_table_load_begin(std::uint8_t*,
                                                    recomp_context* context) {
    g_requested_table = static_cast<std::uint32_t>(context->r4);
}

// Common epilogue of asset_table_load. Publishes a longer level table so the
// retail count, range check and world maximum all grow with it.
extern "C" void dkr_custom_tracks_table_load_end(std::uint8_t* rdram,
                                                  recomp_context* context) {
    const std::uint32_t requested = g_requested_table;
    g_requested_table = 0xFFFFFFFFU;
    Section section = Section::LevelHeaders;
    if (!section_for_table(requested, section)) {
        return; // Not a section custom tracks contribute to.
    }

    const auto retail_address = static_cast<std::uint32_t>(context->r2);
    if (!addressable(retail_address)) {
        return; // Retail returned NULL; leave the failure exactly as authored.
    }

    std::vector<std::int32_t> retail;
    retail.reserve(64);
    for (std::uint32_t index = 0; index < kMaxTableEntries; ++index) {
        const std::int32_t entry =
            read_word(rdram, retail_address + (index * 4U));
        retail.push_back(entry);
        if (entry == -1) {
            break;
        }
    }
    if (retail.empty() || retail.back() != -1) {
        return; // Unterminated: refuse rather than guess.
    }

    const std::vector<std::int32_t> extended =
        dkr::runtime::custom_tracks::build_extended_table(section,
                                                          retail.data());
    if (extended.empty()) {
        return; // Nothing added; the retail table stands unchanged.
    }

    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->mempool_alloc_safe == nullptr) {
        return;
    }

    recomp_context call = *context;
    call.r4 = static_cast<gpr>(extended.size() * sizeof(std::int32_t));
    call.r5 = static_cast<gpr>(kColourTagGrey);
    payload->mempool_alloc_safe(rdram, &call);
    const auto allocated = static_cast<std::uint32_t>(call.r2);
    if (!addressable(allocated)) {
        return; // Out of pool: keep the retail table rather than fail the load.
    }

    for (std::size_t index = 0; index < extended.size(); ++index) {
        write_word(rdram, allocated + static_cast<std::uint32_t>(index * 4U),
                   extended[index]);
    }
    context->r2 = static_cast<gpr>(static_cast<std::int32_t>(allocated));
}

// Entry of asset_load. The destination register is clobbered by the DMA call
// before the epilogue is reached, so every argument is captured here.
extern "C" void dkr_custom_tracks_asset_load_begin(std::uint8_t*,
                                                    recomp_context* context) {
    g_load.section = static_cast<std::uint32_t>(context->r4);
    g_load.destination = static_cast<std::uint32_t>(context->r5);
    g_load.offset = static_cast<std::uint32_t>(context->r6);
    g_load.size = static_cast<std::int32_t>(context->r7);
}

// Reached only on asset_load's DMA path. A custom offset lies just past the
// section, so the retail DMA has read unrelated but in-ROM bytes into the
// destination; replacing them here keeps the retail loader untouched and needs
// no instruction patch.
extern "C" void dkr_custom_tracks_asset_load_end(std::uint8_t* rdram,
                                                  recomp_context*) {
    const AssetLoadRequest request = g_load;
    g_load = AssetLoadRequest{};
    Section section = Section::LevelHeaders;
    if (!section_for_data(request.section, section) || request.size <= 0 ||
        !addressable(request.destination)) {
        return;
    }

    const std::uint8_t* payload = dkr::runtime::custom_tracks::payload_for(
        section, request.offset, request.size);
    if (payload == nullptr) {
        return; // Retail range: the bytes the ROM supplied are the right ones.
    }

    for (std::int32_t index = 0; index < request.size; ++index) {
        MEM_B(index, rdram_address(request.destination)) =
            static_cast<std::uint8_t>(payload[index]);
    }

    if (section != Section::LevelHeaders) {
        return;
    }

    // A header names its model and object map by index, and both indices are
    // assigned when those extended tables are built. Whatever the author wrote
    // is a retail value, so leaving it would silently load the original
    // track's geometry or objects. Each field is only touched when the track
    // actually supplies that section.
    for (const HeaderFixup& fixup : kHeaderFixups) {
        if (request.size <= fixup.offset + 1) {
            continue;
        }
        const std::int32_t index = dkr::runtime::custom_tracks::sibling_index(
            Section::LevelHeaders, request.offset, fixup.section, fixup.slot);
        if (index < 0) {
            continue; // Track ships none, or that table is not built yet.
        }
        MEM_B(fixup.offset, rdram_address(request.destination)) =
            static_cast<std::uint8_t>((index >> 8) & 0xFF);
        MEM_B(fixup.offset + 1, rdram_address(request.destination)) =
            static_cast<std::uint8_t>(index & 0xFF);
        std::fprintf(stderr,
                     "[custom-tracks] header at offset %u now points at %s "
                     "%d\n",
                     request.offset, fixup.label, index);
    }
}

// Common return convergence of get_track_id_to_load. All three retail paths -
// new game, settings->courseId, and the gTrackIdToLoad override that Track
// Select and Trophy Race drive - reach this instruction with the chosen level
// already in v0, so Track Lab replaces the answer without disturbing any of
// them or introducing a second way to pick a level.
extern "C" void dkr_custom_tracks_track_id_override(std::uint8_t*,
                                                     recomp_context* context) {
    const std::int32_t armed = dkr::runtime::custom_tracks::track_override();
    if (armed == dkr::runtime::custom_tracks::kNoTrackOverride) {
        return;
    }
    context->r2 = static_cast<gpr>(armed);
}

// Immediately after mode_menu reads menu_loop's result. Auto boot answers with
// the same value the menus produce when the player picks a track, so retail
// runs its own start sequence - vehicle default, entrance, cutscene, game mode
// and load_level_game - instead of this file reproducing it.
extern "C" void dkr_custom_tracks_auto_boot(std::uint8_t* rdram,
                                             recomp_context* context) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    if (!tracks_ns::auto_boot_enabled()) {
        return;
    }

    // The map id travels in the low seven bits of the menu result, so a track
    // beyond 127 cannot be reached this way.
    const std::int32_t level = tracks_ns::track_override();
    if (level < 0 || level > kMenuResultMapMask) {
        return; // Not resolvable yet; stay armed and try the next frame.
    }
    if (!tracks_ns::consume_auto_boot()) {
        return;
    }

    // These two globals are not in AddressTable: its per-revision initialisers
    // are positional, so inserting fields into the middle silently shifts every
    // later address. Selecting here keeps the change local and auditable.
    const bool rev_a = dkr::runtime::revision_addresses::gSelectedRevision ==
                       dkr::runtime::rom::Revision::UsV80;
    const std::uint32_t character_slots = rev_a ? 0x80126990U : 0x801263F0U;
    const std::uint32_t game_num_players = rev_a ? 0x80123A80U : 0x80123500U;

    // Player one races as Diddy. The remaining slots stay as the game left
    // them, which is what the retail AI fill already expects.
    MEM_B(0, rdram_address(character_slots)) = kCharacterDiddy;
    // load_next_ingame_level stores players minus one, so zero is a single
    // player.
    MEM_W(0, rdram_address(game_num_players)) = 0U;

    context->r2 = static_cast<gpr>(kMenuResultStartLevel | level);
}
