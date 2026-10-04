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
// Pure code, no ImGui, so it tests with literals; the picker draws it (ui/NazgKeycodePicker.h).

#pragma once

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

    struct CatalogueTab
    {
        std::string_view            id;     // stable, for remembering the tab: "keys", "media"
        std::string_view            name;   // "Media & mouse"
        CommandCategory             category = CommandCategory::None;   // its dot; Keys has none
        std::vector<CatalogueGroup> groups;
    };

    // The tabs for `keyboard`, in order: Keys, Layers, Media & mouse, Lighting, Features, Macros,
    // Special, Custom, Devices, Firmware. `legends` gives the group titles that are a header --
    // "Ctrl↔Win" or "Ctrl↔Cmd" by the modifier names.
    [[nodiscard]] std::vector<CatalogueTab> BuildKeycodeCatalogue(const Keyboard& keyboard, const LegendSettings& legends);

    // What a search looks through for one keycode, lower case: QMK's name and label, and the
    // words its keycap prints.
    [[nodiscard]] std::string SearchTextOf(const Keycode& keycode, QmkKeycodeVersion version, const LegendContext& context);

    // Whether `query` -- any case, spaces at its ends ignored -- is in `text`, a SearchTextOf().
    [[nodiscard]] bool MatchesSearch(std::string_view text, std::string_view query);
}
