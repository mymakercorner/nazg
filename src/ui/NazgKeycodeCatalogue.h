// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycodeCatalogue - what the keycode picker offers a board: its tabs, their groups, the
// keycodes in each. ui-design.md, "The keycode picker", "The tabs".
//
// Every keycode the board's keycode version can store is in a tab -- the encoder decides, so
// nothing is offered that the board would refuse -- and a group is left out only when the
// board says it cannot work: the lighting its definition declares and its firmware state, the
// macro and tap dance counts, Vial's feature bits and alt repeat count. What nothing tells --
// audio, haptic, Unicode, steno -- is offered. A tab left with no group is not listed.
//
// The Basic tab is a full-size keyboard in the host layout's shape, the Extra tab what that
// leaves of the keys a keyboard sends (ui-design.md, "The Keys tab as a keyboard").
//
// Pure code, no ImGui, so it tests with literals; the picker draws it (ui/NazgKeycodePicker.h).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "model/NazgKeyboard.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    struct CatalogueGroup
    {
        std::string          title;   // beside its tiles: "Letters", "Ctrl↔Caps"
        std::string          more;    // the rest, on hover; may be empty
        std::vector<Keycode> keycodes;
    };

    // How a tab draws its keys (ui-design.md, "The Keys tab as a keyboard"): as tiles; as a
    // full-size keyboard, its keys where a keyboard has them; or as that keyboard's keycaps, at its
    // unit, in groups as the tiles are.
    enum class CatalogueLook : uint8_t
    {
        Tiles,
        Keyboard,
        Keycaps,
    };

    // A key of a drawn keyboard: where it is, in key units, and what it sends.
    struct CatalogueKey
    {
        via::DefinitionKey geometry;
        Keycode            keycode;
    };

    struct CatalogueTab
    {
        std::string_view            id;     // stable, for remembering the tab: "basic", "media"
        std::string_view            name;   // "Media & mouse"
        CommandCategory             category = CommandCategory::None;   // its dot; Basic and Extra have none
        std::vector<CatalogueGroup> groups;
        CatalogueLook               look = CatalogueLook::Tiles;

        // With CatalogueLook::Keyboard, its keys; `groups` holds their keycodes too, for search.
        std::vector<CatalogueKey> keyboard;
    };

    // The tabs for `keyboard`, in order: Basic, Extra, Layers, Media & mouse, Lighting, Features,
    // Macros, Special, Custom, Devices, Firmware. `legends` gives the group titles that are a header
    // -- "Ctrl↔Win" or "Ctrl↔Cmd" by the modifier names -- and Basic's shape, the host layout's.
    // `everyLightingKey`, with Advanced tools on: every lighting keycode for the systems declared,
    // not only those the firmware state allows.
    [[nodiscard]] std::vector<CatalogueTab> BuildKeycodeCatalogue(const Keyboard& keyboard, const LegendSettings& legends,
                                                                  bool everyLightingKey = false);

    // A standard full-size keyboard in `shape` -- 104 keys on ANSI, 105 on ISO, 109 on JIS -- and
    // nothing else: six rows, no gap under the function keys, as Vial draws its own; ISO's Enter
    // the L a board's definition gives it. 22.5 units wide.
    [[nodiscard]] std::vector<CatalogueKey> FullSizeKeyboard(KeyboardShape shape);

    // Whether `current`, the selected key's keycode, sends `keycode`: it is `keycode`, or holds it
    // as its tap or under its modifiers -- KC_A in LT(1, KC_A) or LCTL(KC_A). Outlined where the
    // picker draws keys as keycaps.
    [[nodiscard]] bool IsSentBy(const Keycode& keycode, const Keycode& current);

    // What a pick writes, the key's line keeping what it set (ui-design.md, "The key line"): a
    // plain key picked onto a key that holds -- MT, LT -- or is sent with modifiers keeps that:
    // Space onto LT(1, KC_A) is LT(1, KC_SPC). Shift alone is not kept: 2 onto LSFT(KC_1) is KC_2,
    // as Shift and a key is a symbol. Anything else, or a key the hold cannot take -- only basic
    // keycodes, QMK's bottom byte -- is written as picked.
    [[nodiscard]] Keycode ComposeWithKey(const Keycode& picked, const Keycode& current, qmk::KeycodeVersion version);

    // What a search looks through for one keycode, lower case: QMK's name and label, and the
    // words its keycap prints.
    [[nodiscard]] std::string SearchTextOf(const Keycode& keycode, qmk::KeycodeVersion version, const LegendContext& context);

    // Whether `query` -- any case, spaces at its ends ignored -- is in `text`, a SearchTextOf().
    [[nodiscard]] bool MatchesSearch(std::string_view text, std::string_view query);
}
