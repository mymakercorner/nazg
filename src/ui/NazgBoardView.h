// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardView - draws a board as a section described it, with ImGui.
//
// The renderer half of the section contract: the section says what each key means
// (ui/NazgBoardDescription.h), this decides how that looks -- sizes here, colours in
// ui/NazgTheme.h -- so a restyle changes these two files and no section. The look is being
// built from ui-design.md, "How the board's look is built": keycaps, plate, themes, the
// standard keys' legends and the command keys' bands and headers are done -- placed by
// ui/NazgKeycapLayout.h, drawn here.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include "adapters/via/NazgKeyboardDefinition.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeycapLayout.h"
#include "ui/NazgLegendFont.h"

namespace nazg
{
    // The fonts legends are set in, loaded in Main.cpp. A null weight: the interface's font
    // instead.
    void SetLegendFonts(const LegendFonts& fonts);

    // Into the current ImGui window, at the cursor, centred in the width available: the plate,
    // the keys, the lines over them and the labels around them, as large as that width allows
    // but no taller than `maxHeight` pixels -- and no smaller than legible text allows,
    // scrolling sideways past that. Returns what the mouse did on it this frame.
    [[nodiscard]] BoardEvents DrawBoard(const BoardDescription& board, float maxHeight);

    // A definition drawn small -- blank keys, the first choice of every layout option -- so
    // candidates tell apart at a glance: ANSI from ISO, ortho from staggered. At most
    // `width` pixels wide and `height` high, keeping the board's proportions.
    void DrawDefinitionPreview(const KeyboardDefinition& definition, float width, float height);

    // One tile of the keycode picker (ui-design.md, "The tiles"), drawn as the board draws a key --
    // its face in its class's colour, its band, its legends laid out by LayOutTile(), a transparent
    // or KC_NO key's mark -- into the current window at `box`, screen pixels. Drawing only: the
    // picker handles the mouse.
    struct KeycodeTile
    {
        KeycapLegend    legend;   // what it prints: a header its group's title says left out
        CommandCategory band     = CommandCategory::None;   // the legend's own, kept when its header goes
        KeyFill         fill     = KeyFill::Modifier;
        Fallthrough     mark     = Fallthrough::None;
        bool            selected = false;   // the selected key's keycode
        bool            hovered  = false;
        bool            isFaint  = false;   // may do nothing on this board: drawn faint, as a fallthrough
    };

    void DrawKeycodeTile(const KeycodeTile& tile, const FaceBox& box);
}
