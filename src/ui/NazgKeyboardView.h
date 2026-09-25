// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyboardView - draws a loaded board with ImGui.
//
// FIRST DRAFT, expected to be replaced: the look of the board needs real visual design
// work, and nothing here is meant to survive it. What should survive is elsewhere --
// legends in NazgKeycapLegend.h, geometry in the definition -- so this file stays a thin
// layer that is cheap to throw away.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include "model/NazgKeyboard.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // The key the user clicked, by matrix cell -- the address a write needs.
    struct KeySelection
    {
        bool    active = false;
        uint8_t row    = 0;
        uint8_t column = 0;
    };

    // Layer tabs and the board itself, into the current ImGui window. `layer` is the
    // selected tab and `selection` the clicked key; both are updated by the user.
    void DrawKeyboardView(const Keyboard& keyboard, int& layer, const HostLayout& layout, KeySelection& selection);

    // A definition drawn small -- blank keys, the first choice of every layout option -- so
    // candidates tell apart at a glance: ANSI from ISO, ortho from staggered. At most
    // `width` pixels wide and `height` high, keeping the board's proportions.
    void DrawDefinitionPreview(const KeyboardDefinition& definition, float width, float height);
}
