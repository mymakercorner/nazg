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
#include <string_view>
#include <vector>

#include "model/NazgKeyboard.h"
#include "ui/NazgSection.h"

struct ImFont;

namespace nazg
{
    // Tabler's icon font (ui/NazgIcons.h), loaded by the caller; until it is set, or when it is
    // missing, sections are drawn with their monograms.
    void SetIconFont(ImFont* font);

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
        bool isVia       = false;   // Change definition... and Forget choice are VIA's
        bool hasChoice   = false;   // a remembered choice to forget
        bool hasAdvanced = false;   // the Advanced submenu, with the setting on
        bool canExport   = false;
        bool isBusy      = false;   // a load or a write in flight: nothing may replace the board

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

    // How the height under the strip is shared between the board and the panel: a splitter
    // between them, dragged, and remembered in imgui.ini (ui-design.md, "The keycode picker",
    // "Its size"). The board's height is what it may take at most, in pixels before DPI scaling:
    // the splitter changes the board's size, the window only the room around it -- the board is
    // centred -- until the board fills its width, and then the board too, down to its
    // legibility floor. The caller keeps the window from shrinking past `spareWidth` and
    // `spareHeight` (Rico, 2026-10-05). 0 until first drawn, then 60% of the height.
    //
    // And the section column's width (ui-design.md, the open point "Folding the section column"):
    // the list's, dragged by its edge between 120 and 200 px, and whether it is folded to icons
    // only -- dragged below 100 px, or by a double-click on the edge. One for every board.
    struct WorkspaceLayout
    {
        float boardHeight    = 0.0f;
        float columnWidth    = 180.0f;   // every custom menu label in VIA's registry, whole
        bool  isColumnFolded = false;
        bool  changed        = false;  // dragged this frame: the caller saves it

        // Each frame, how much narrower the window could be before the board reaches its floor
        // or the panel its minimum width, and how much shorter before the board or the panel
        // shrinks -- in ImGui's coordinates, the window's. Never negative: Nazg never grows the
        // window.
        float spareWidth  = 0.0f;
        float spareHeight = 0.0f;
    };

    // `sections` are the open board's -- the column lists them, and is hidden when there is
    // only one -- and `active` the one shown, which the column changes. `keyboard` gives the
    // board the sections start from; `protocol` heads Nazg's own sections when another group
    // follows them: "VIA" or "Vial". Every section not shown hears WhileHidden().
    void DrawSections(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, const Keyboard& keyboard,
                      std::string_view protocol, WorkspaceLayout& layout);

    // One section's strip, board and panel, with no column.
    void DrawView(Section& section, const Keyboard& keyboard, WorkspaceLayout& layout);
}
