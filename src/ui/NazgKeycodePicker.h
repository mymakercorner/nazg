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

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "model/NazgKeyboard.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgBoardView.h"
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

        // The tabs, built by CatalogueOf() again only when the settings they depend on change.
        std::vector<CatalogueTab> catalogue;
        std::string               catalogueFor;
    };

    // The picker's tabs for `keyboard` (BuildKeycodeCatalogue()), kept in `state` until the host
    // layout, the modifier names or Advanced tools change.
    [[nodiscard]] const std::vector<CatalogueTab>& CatalogueOf(KeycodePickerState& state, const Keyboard& keyboard,
                                                               const LegendSettings& legends, bool advancedTools);

    // What the picker draws with this frame.
    struct KeycodePickerInput
    {
        const std::vector<CatalogueTab>& tabs;
        const LegendContext&             context;   // on no board: KeySide::Neither
        QmkKeycodeVersion                version;

        // The selected key's keycode, outlined among the tiles, and what a pick composes with
        // (ComposeWithKey()); none while no key is selected.
        std::optional<Keycode> current;

        // The board, for what hover says of a lighting key and whether its tile is faint
        // (LightingNoteOf()); none on no board.
        const Keyboard* keyboard = nullptr;

        // Why a keycode cannot be picked here, or nothing when it can: its tile is faint, a click
        // on it does nothing, and hover says why -- a VIA board's macros hold basic keys only.
        std::function<std::optional<std::string>(const Keycode&)> unavailable;
    };

    struct KeycodePickerEvents
    {
        std::optional<Keycode> picked;    // clicked this frame, as the tile says
        std::optional<Keycode> preview;   // what a click would write, while a tile's tooltip shows
    };

    [[nodiscard]] KeycodePickerEvents DrawKeycodePicker(KeycodePickerState& state, const KeycodePickerInput& input);

    // A keycode drawn as the picker draws it, 1u -- for a keycode shown elsewhere, as a macro's key
    // steps -- and the size it is drawn at, in screen pixels.
    [[nodiscard]] KeycodeTile TileOf(const Keycode& keycode, const LegendContext& context, bool selected);
    [[nodiscard]] ImVec2      TileSize();

    // QMK's name and label, for a tooltip: "KC_MPLY -- Play/Pause".
    [[nodiscard]] std::string KeycodeHoverText(const Keycode& keycode, QmkKeycodeVersion version);

    // A keycode for people, in a sentence: QMK's label when it has one -- "Escape" -- else its
    // expression, "MO(1)".
    [[nodiscard]] std::string KeycodeLabel(const Keycode& keycode, QmkKeycodeVersion version);
}
