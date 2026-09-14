#pragma once
#include "legacy_character_menu.hpp"
#include "legacy_runtime_io.hpp"

namespace dkr::mods {
struct CharacterPresentationCalls {
    OriginalPiHandler allocate=nullptr,texture_load=nullptr,bank_relocate=nullptr,
        bank_play=nullptr,parameter=nullptr,spatial_point=nullptr;
};
// Session-owned resources, never donor-global substitutions. Queued native
// audio and result-menu references may outlive character-select and a race.
class CharacterPresentation {
public:
    void initialize(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,std::span<const std::uint32_t> sample_addresses);
    std::uint32_t portrait(const CharacterRoster&,const std::vector<AllocatedCharacter>&,
        unsigned racer)const;
    unsigned sound(const CharacterRoster&,const std::vector<AllocatedCharacter>&,
        unsigned racer,unsigned native_character,unsigned native_sound)const;
    // kind 0: native spatial AudioPoint creation, 1: direct sound-bank play,
    // 2: parameterized sound-table play (e.g. the native spatial horn wrapper).
    bool play(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,unsigned kind)const;
private:
    std::vector<std::uint32_t> portrait_cells_,banks_;
    bool ready_=false;
};
}
