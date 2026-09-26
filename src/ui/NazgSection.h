// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Section - one part of an open board's screen: Keymap, Macros, a plugin such as the Leyden
// Jar diagnostics. ui-design.md, "What a section provides".
//
// The regions never move and Nazg draws every one of them -- the section column, the strip,
// the board, the panel (ui/NazgWorkspace.h). A section only fills them, with four things:
//
//   1. its strip entries, or none;
//   2. what the board shows, and what a click on it does (ui/NazgBoardDescription.h);
//   3. its panel;
//   4. its match rule -- which boards it appears for. Not in this interface yet: Keymap
//      appears on every board, so the rule arrives with the first section that does not.
//
// Compiled in for now. How third-party sections will be delivered is open (ui-design.md,
// "Plugins"); two very different sections test the contract before that is chosen.
//
// A section lives exactly as long as the board it was made for is open and loaded: a new
// load makes new sections, so anything a section keeps a reference to -- the Keyboard, the
// transport -- outlives it.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ui/NazgBoardDescription.h"

namespace nazg
{
    // The section's own row of choices, above the board: layers in Keymap, slots in Macros,
    // modes in the Leyden Jar diagnostics.
    struct Strip
    {
        std::string              label;     // "Layer"
        std::vector<std::string> entries;   // none: the strip is absent
        size_t                   chosen = 0;
    };

    class Section
    {
    public:
        virtual ~Section() = default;

        // Its row in the section column.
        [[nodiscard]] virtual std::string_view Name() const = 0;

        // 1. The strip, and the entry the user picked in it.
        [[nodiscard]] virtual Strip DescribeStrip() const { return {}; }
        virtual void OnStripChosen(size_t /*entry*/) {}

        // 2. The board. Every frame, `board` arrives as the board's definition draws it
        // (DescribeKeyboard()); the section fills it in -- or replaces its keys, to draw
        // something else. After drawing, it hears what the mouse did there.
        virtual void DescribeBoard(BoardDescription& board) = 0;
        virtual void OnBoardEvents(const BoardDescription& /*board*/, const BoardEvents& /*events*/) {}

        // 3. The panel: ImGui, into the region Nazg sized for it. The only colours a panel
        // may use are the named ones, ui/NazgTheme.h.
        virtual void DrawPanel() = 0;

        // Work it started that has not finished -- a write in flight, which refers to the
        // section and the board. Nothing closes or reloads the board while it is busy.
        [[nodiscard]] virtual bool IsBusy() const { return false; }
    };
}
