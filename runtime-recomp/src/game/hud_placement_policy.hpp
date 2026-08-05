#pragma once

#include <cstdint>

namespace dkr::runtime::enhancements {

enum class HudAnchor : std::uint16_t {
    None = 0x800,
    Left = 0x000,
    Right = 0x400,
};

// Entries which are deliberately animated through the middle of the authored
// 4:3 canvas must remain centre-relative. Treating their temporary negative or
// positive X coordinate as an edge anchor would make READY/GO, lap messages,
// wrong-way warnings and finish results jump when they cross the centre.
constexpr bool is_centre_animated_hud_element(int element_index) {
    switch (element_index) {
    case 12: // HUD_RACE_START_GO
    case 13: // HUD_RACE_START_READY
    case 14: // HUD_RACE_END_FINISH
    case 17: // HUD_MAGNET_RETICLE
    case 29: // HUD_LAP_TEXT_FINAL
    case 30: // HUD_LAP_TEXT_LAP
    case 31: // HUD_LAP_TEXT_TWO
    case 33: // HUD_COURSE_ARROWS
    case 35: // HUD_WRONGWAY_1
    case 36: // HUD_WRONGWAY_2
    case 48: // HUD_CHALLENGE_FINISH_POS_1
    case 49: // HUD_CHALLENGE_FINISH_POS_2
    case 57: // HUD_RACE_FINISH_POS_1
    case 59: // HUD_RACE_FINISH_POS_2
        return true;
    default:
        return false;
    }
}

constexpr bool is_centre_animated_hud_asset(int sprite_id) {
    switch (sprite_id) {
    case 5:  // HUD_SPRITE_FINISH
    case 7:  // HUD_SPRITE_GO_BIG
    case 17: // HUD_SPRITE_RETICLE
    case 22: // HUD_SPRITE_GET_READY
    case 24: // HUD_SPRITE_LAP_FINAL
    case 25: // HUD_SPRITE_LAP_LAP
    case 26: // HUD_SPRITE_LAP_2
    case 29: // HUD_SPRITE_INDICATOR_EXCLAMATION
    case 30: // HUD_SPRITE_INDICATOR_DOWN
    case 31: // HUD_SPRITE_INDICATOR_U
    case 32: // HUD_SPRITE_INDICATOR_TURN_90
    case 33: // HUD_SPRITE_INDICATOR_TURN_45
    case 35: // HUD_SPRITE_WRONG
    case 36: // HUD_SPRITE_WAY
    case 37: // HUD_SPRITE_GO_SMALL
    case 38: // HUD_SPRITE_LAP_LAP_SMALL
    case 39: // HUD_SPRITE_GET_READY_SMALL
    case 40: // HUD_SPRITE_PRO_AM
    case 41: // HUD_SPRITE_LAP_FINAL_SMALL
    case 42: // HUD_SPRITE_FINISH_SMALL
    case 43: // HUD_SPRITE_LAP_2_SMALL
    case 44: // HUD_SPRITE_WAY_SMALL
    case 45: // HUD_SPRITE_WRONG_SMALL
    case 67: // HUD_SPRITE_PLACE_1
    case 68: // HUD_SPRITE_PLACE_ST
        return true;
    default:
        return false;
    }
}

// DKR uses screen-space 0..320 coordinates for texture rectangles and
// centre-relative -160..160 coordinates for ortho sprites/models. The neutral
// bands retain intentional centre composition while the edge bands cover the
// stable race HUD groups. The minimap markers are forced right because their
// coordinates move inside the right-anchored minimap plate.
constexpr HudAnchor classify_hud_anchor(int element_index, int sprite_id,
                                         bool texture_rectangle, float x) {
    if (element_index == 15) { // HUD_MINIMAP_MARKER
        return HudAnchor::Right;
    }
    if (is_centre_animated_hud_element(element_index) ||
        is_centre_animated_hud_asset(sprite_id)) {
        return HudAnchor::None;
    }
    if (texture_rectangle) {
        if (x < 128.0F) {
            return HudAnchor::Left;
        }
        if (x > 192.0F) {
            return HudAnchor::Right;
        }
        return HudAnchor::None;
    }
    if (x < -72.0F) {
        return HudAnchor::Left;
    }
    if (x > 72.0F) {
        return HudAnchor::Right;
    }
    return HudAnchor::None;
}

} // namespace dkr::runtime::enhancements
