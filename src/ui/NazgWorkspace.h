// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Workspace - the regions of Nazg's one window (ui-design.md, "The regions"): the header,
// and under it the section column, the strip, the board and the panel, filled by the open
// board's sections (ui/NazgSection.h). The screens with no sections -- the keyboard list,
// settings -- are ui/NazgKeyboardList.h and ui/NazgSettingsScreen.h.
//
// The header draws and reports what was clicked, as the screens do; acting on it is the
// caller's. The regions' look is as much a first draft as the board's.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/NazgKeyboard.h"
#include "ui/NazgSection.h"

namespace nazg
{
    // A keyboard plugged in besides the open one, for the board menu's "Switch to".
    struct OtherKeyboard
    {
        std::string name;
        std::string protocol;   // as the keyboard list has it
    };

    // What the header shows, gathered by the caller.
    struct HeaderView
    {
        std::string name;       // the board's -- a menu; empty when no board is open
        std::string protocol;   // "Vial", "VIA"; empty until it is known
        std::string details;    // on hovering the name: the definition drawing it, and more

        // A Vial board's lock: locked or unlocked, or unset on a board that has none.
        std::optional<bool> isLocked;

        // The board menu.
        std::vector<OtherKeyboard> others;
        bool isVia     = false;   // Change definition... and Forget choice are VIA's
        bool hasChoice = false;   // a remembered choice to forget
        bool canExport = false;
        bool isBusy    = false;   // a load or a write in flight: nothing may replace the board

        bool isSettingsShown = false;
    };

    struct HeaderAction
    {
        std::optional<size_t> switchTo;   // index into HeaderView::others
        bool changeDefinition = false;
        bool forgetChoice     = false;
        bool exportDefinition = false;
        bool allKeyboards     = false;
        bool toggleLock       = false;   // the lock state was clicked: unlock, or lock again
        bool settings         = false;   // the settings button: show or leave them
    };

    // Into the current window's menu bar -- the window needs ImGuiWindowFlags_MenuBar.
    [[nodiscard]] HeaderAction DrawHeader(const HeaderView& view);

    // `sections` are the open board's -- the column lists them, and is hidden when there is
    // only one -- and `active` the one shown, which the column changes. `keyboard` gives the
    // board the sections start from.
    void DrawSections(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, const Keyboard& keyboard);
}
