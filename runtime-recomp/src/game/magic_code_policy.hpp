#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::magic_codes {

struct MagicCodeDefinition {
    std::uint8_t internal_index;
    const char* phrase;
    const char* effect;
    bool one_shot;
    bool diagnostic;
};

inline constexpr std::array<MagicCodeDefinition, 24> kMagicCodeDefinitions{{
    {4, "ARNOLD", "Large racers", false, false},
    {5, "TEENYWEENIES", "Small racers", false, false},
    {6, "JUKEBOX", "Unlock the Music Test", false, false},
    {7, "FREEFRUIT", "Start races with ten bananas", false, false},
    {8, "BLABBERMOUTH", "Character voices replace vehicle horns", false, false},
    {10, "WHODIDTHIS", "Show the credits after leaving Magic Codes", true, false},
    {11, "BYEBYEBALLOONS", "Disable Weapon Balloons", false, false},
    {12, "NOYELLOWSTUFF", "Disable bananas", false, false},
    {13, "BOGUSBANANAS", "Bananas reduce speed", false, false},
    {14, "VITAMINB", "Remove the banana limit", false, false},
    {15, "BOMBSAWAY", "All Weapon Balloons are red", false, false},
    {16, "TOXICOFFENDER", "All Weapon Balloons are green", false, false},
    {17, "ROCKETFUEL", "All Weapon Balloons are blue", false, false},
    {18, "BODYARMOR", "All Weapon Balloons are yellow", false, false},
    {19, "OPPOSITESATTRACT", "All Weapon Balloons are rainbow", false, false},
    {20, "FREEFORALL", "Weapon Balloons begin fully powered", false, false},
    {21, "ZAPTHEZIPPERS", "Disable zippers", false, false},
    {22, "DOUBLEVISION", "Allow duplicate racers", false, false},
    {23, "OFFROAD", "Enable four-wheel drive", false, false},
    {24, "JOINTVENTURE", "Enable two-player Adventure", false, false},
    {25, "TIMETOLOSE", "Enable Ultimate AI", false, false},
    {26, "EOLAOBFENRLONE", "Grant one Golden Balloon", true, false},
    {27, "EPC", "Enable the EPC lock-up diagnostic", false, true},
    {28, "DODGYROMMER", "Display the ROM checksum", false, true},
}};

constexpr std::uint32_t magic_code_bit(std::uint8_t internal_index) {
    return internal_index < 32U ? (1U << internal_index) : 0U;
}

constexpr std::uint32_t selectable_magic_code_mask() {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        result |= magic_code_bit(definition.internal_index);
    }
    return result;
}

constexpr std::uint32_t one_shot_magic_code_mask() {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        if (definition.one_shot) {
            result |= magic_code_bit(definition.internal_index);
        }
    }
    return result;
}

inline constexpr std::uint32_t kSelectableMagicCodeMask =
    selectable_magic_code_mask();
inline constexpr std::uint32_t kOneShotMagicCodeMask =
    one_shot_magic_code_mask();
inline constexpr std::uint32_t kPersistentMagicCodeMask =
    kSelectableMagicCodeMask & ~kOneShotMagicCodeMask;

constexpr std::uint32_t enable_magic_code(std::uint32_t current,
                                          std::uint8_t internal_index) {
    const std::uint32_t selected = magic_code_bit(internal_index);
    if ((selected & kSelectableMagicCodeMask) == 0U) {
        return current & kSelectableMagicCodeMask;
    }

    current = (current | selected) & kSelectableMagicCodeMask;
    const std::uint32_t big = magic_code_bit(4);
    const std::uint32_t small = magic_code_bit(5);
    if (selected == big) current &= ~small;
    if (selected == small) current &= ~big;

    const std::uint32_t disable_bananas = magic_code_bit(12);
    const std::uint32_t banana_modifiers = magic_code_bit(7) |
        magic_code_bit(13) | magic_code_bit(14);
    if (selected == disable_bananas) current &= ~banana_modifiers;
    if ((selected & banana_modifiers) != 0U) current &= ~disable_bananas;

    const std::uint32_t disable_weapons = magic_code_bit(11);
    const std::uint32_t weapon_modifiers = magic_code_bit(15) |
        magic_code_bit(16) | magic_code_bit(17) | magic_code_bit(18) |
        magic_code_bit(19) | magic_code_bit(20);
    if (selected == disable_weapons) current &= ~weapon_modifiers;
    if ((selected & weapon_modifiers) != 0U) current &= ~disable_weapons;

    const std::uint32_t balloon_colours = magic_code_bit(15) |
        magic_code_bit(16) | magic_code_bit(17) | magic_code_bit(18) |
        magic_code_bit(19);
    if ((selected & balloon_colours) != 0U) {
        current &= ~(balloon_colours & ~selected);
    }
    return current;
}

constexpr std::uint32_t disable_magic_code(std::uint32_t current,
                                           std::uint8_t internal_index) {
    return (current & ~magic_code_bit(internal_index)) &
        kSelectableMagicCodeMask;
}

constexpr std::uint32_t normalise_magic_code_mask(std::uint32_t mask) {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        const std::uint32_t bit = magic_code_bit(definition.internal_index);
        if ((mask & bit) != 0U) {
            result = enable_magic_code(result, definition.internal_index);
        }
    }
    return result;
}

constexpr bool magic_code_enabled(std::uint32_t mask,
                                  std::uint8_t internal_index) {
    return (mask & magic_code_bit(internal_index)) != 0U;
}

} // namespace dkr::runtime::magic_codes
