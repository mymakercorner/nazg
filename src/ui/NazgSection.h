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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/NazgBoardDescription.h"
#include "ui/NazgIcons.h"

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

    // The column's groups (ui-design.md, "Nazg's sections, then the board's menus"): the sections
    // Nazg provides for the protocol, headed by its name -- VIA or Vial -- then the board's own
    // menus from its definition, headed "Board". Tools, the third, comes with the first tool.
    enum class SectionGroup
    {
        Protocol,
        Board,
    };

    class Section
    {
    public:
        virtual ~Section() = default;

        // Its row in the section column: the name, its icon -- Icon::None draws a monogram of the
        // name -- and the group it is listed in.
        [[nodiscard]] virtual std::string_view Name() const = 0;
        [[nodiscard]] virtual Icon             ColumnIcon() const { return Icon::None; }
        [[nodiscard]] virtual SectionGroup     Group() const { return SectionGroup::Protocol; }

        // 1. The strip, the entry the user picked in it, and the one under the mouse -- told
        // every frame the strip shows, before DescribeBoard(), none when the mouse is elsewhere:
        // Keymap peeks at a hovered layer.
        [[nodiscard]] virtual Strip DescribeStrip() const { return {}; }
        virtual void OnStripChosen(size_t /*entry*/) {}
        virtual void OnStripHovered(std::optional<size_t> /*entry*/) {}

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
