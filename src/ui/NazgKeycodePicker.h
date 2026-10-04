// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycodePicker - the tabs and tiles of the keycode picker, the Keymap section's panel under its
// key line. ui-design.md, "The keycode picker", after the mockup ui-design/picker-look.html.
//
// Category tabs, each with its colour's dot, and one search box through them all; in a tab, its
// groups packed side by side on one grid, each title in a box spanning its rows, then its tiles --
// every one 1u, drawn as the board draws a key (DrawKeycodeTile()). What the tabs hold is the
// catalogue's (ui/NazgKeycodeCatalogue.h); writing is the section's: this only says what was
// clicked, and what the mouse rests on, for the hover preview.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgKeycodeCatalogue.h"

namespace nazg
{
    struct KeycodePickerState
    {
        std::string tab        = "keys";   // CatalogueTab::id
        bool        restoreTab = true;     // the tab bar shows `tab` first, then follows the clicks
        char        search[64] = {};

        // A tile just clicked previews nothing until the mouse leaves it: with "next key" on,
        // it would show its keycode on the key after.
        std::optional<std::string> justPicked;

        // The last search's results, found again only when the query or the tabs change.
        std::string                 searched;
        const void*                 searchedTabs = nullptr;
        std::vector<CatalogueGroup> results;
    };

    // What the picker draws with this frame.
    struct KeycodePickerInput
    {
        const std::vector<CatalogueTab>& tabs;
        const LegendContext&             context;   // on no board: KeySide::Neither
        QmkKeycodeVersion                version;

        // The selected key's keycode, outlined among the tiles, and what a pick composes with
        // (ComposeWithKey()); none while no key is selected.
        std::optional<Keycode> current;
    };

    struct KeycodePickerEvents
    {
        std::optional<Keycode> picked;    // clicked this frame, as the tile says
        std::optional<Keycode> preview;   // what a click would write, while a tile's tooltip shows
    };

    [[nodiscard]] KeycodePickerEvents DrawKeycodePicker(KeycodePickerState& state, const KeycodePickerInput& input);

    // QMK's name and label, for a tooltip: "KC_MPLY -- Play/Pause".
    [[nodiscard]] std::string KeycodeHoverText(const Keycode& keycode, QmkKeycodeVersion version);
}
