// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Theme - every colour Nazg chooses, in one place, so the visual design to come changes this
// file and the board renderer's sizes, and nothing else.
//
// PLACEHOLDER values: the styling is not designed yet (ui-design.md). What is decided is the
// names. Panels -- a section's included -- use the four named colours and no other; the board
// is coloured by what a key means (ui/NazgBoardDescription.h), mapped to colours here.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include "imgui.h"

#include "ui/NazgBoardDescription.h"

namespace nazg
{
    // The named colours for panels.
    enum class PanelColour
    {
        Error,
        Warning,
        Success,
        Muted,
    };

    [[nodiscard]] ImVec4 ColourOf(PanelColour colour);

    // ImGui::Text in one of them.
    void ColouredText(PanelColour colour, const char* format, ...) IM_FMTARGS(2);

    // The board's colours, by meaning.
    namespace BoardColours
    {
        [[nodiscard]] ImU32 Fill(KeyFill fill, float heat);
        [[nodiscard]] ImU32 Legend(LegendRole role);
        [[nodiscard]] ImU32 EdgeLabel(uint8_t marks);
        [[nodiscard]] ImU32 Line(uint8_t marks);

        // The states, drawn over the fill.
        inline constexpr ImU32 c_Hovered     = IM_COL32(255, 255, 255, 34);    // overlay
        inline constexpr ImU32 c_Pressed     = IM_COL32(60, 110, 255, 170);    // overlay
        inline constexpr ImU32 c_Dimmed      = IM_COL32(12, 12, 16, 150);      // overlay
        inline constexpr ImU32 c_Selected    = IM_COL32(240, 180, 60, 255);    // outline
        inline constexpr ImU32 c_Highlighted = IM_COL32(110, 200, 255, 255);   // outline
        inline constexpr ImU32 c_Warning     = IM_COL32(255, 120, 60, 255);    // outline
    }
}
