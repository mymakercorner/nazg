// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardView - draws a board as a section described it, with ImGui.
//
// The renderer half of the section contract: the section says what each key means
// (ui/NazgBoardDescription.h), this decides how that looks -- sizes here, colours in
// ui/NazgTheme.h -- so a restyle changes these two files and no section. The look is being
// built from ui-design.md, "How the board's look is built": keycaps, plate and themes are
// done; the legends are still the first draft's.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include "adapters/via/NazgKeyboardDefinition.h"
#include "ui/NazgBoardDescription.h"

struct ImFont;

namespace nazg
{
    // The fonts legends are set in -- Arimo, with the Noto faces merged behind it for what it
    // lacks (resources/fonts/README.md) -- loaded in Main.cpp. A null weight: the interface's
    // font instead.
    struct LegendFonts
    {
        ImFont* regular = nullptr;
        ImFont* bold    = nullptr;
    };

    void SetLegendFonts(const LegendFonts& fonts);

    // Into the current ImGui window, at the cursor: the plate, the keys, the lines over them
    // and the labels around them, as large as the width available allows but no taller than
    // `maxHeight` pixels -- and no smaller than legible text allows, scrolling sideways past
    // that. Returns what the mouse did on it this frame.
    [[nodiscard]] BoardEvents DrawBoard(const BoardDescription& board, float maxHeight);

    // A definition drawn small -- blank keys, the first choice of every layout option -- so
    // candidates tell apart at a glance: ANSI from ISO, ortho from staggered. At most
    // `width` pixels wide and `height` high, keeping the board's proportions.
    void DrawDefinitionPreview(const KeyboardDefinition& definition, float width, float height);
}
