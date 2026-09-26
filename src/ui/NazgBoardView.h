// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardView - draws a board as a section described it, with ImGui.
//
// The renderer half of the section contract: the section says what each key means
// (ui/NazgBoardDescription.h), this decides how that looks -- sizes here, colours in
// ui/NazgTheme.h. The look itself is still a FIRST DRAFT awaiting real visual design; the
// point is that a restyle changes these two files and no section.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include "adapters/via/NazgKeyboardDefinition.h"
#include "ui/NazgBoardDescription.h"

namespace nazg
{
    // Into the current ImGui window, at the cursor: the keys, the lines over them and the
    // labels around them, as large as the width available allows but no taller than
    // `maxHeight` pixels. Returns what the mouse did on it this frame.
    [[nodiscard]] BoardEvents DrawBoard(const BoardDescription& board, float maxHeight);

    // A definition drawn small -- blank keys, the first choice of every layout option -- so
    // candidates tell apart at a glance: ANSI from ISO, ortho from staggered. At most
    // `width` pixels wide and `height` high, keeping the board's proportions.
    void DrawDefinitionPreview(const KeyboardDefinition& definition, float width, float height);
}
