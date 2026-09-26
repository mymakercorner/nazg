// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Workspace - the regions of an open board's screen, filled by its sections
// (ui-design.md, "The regions"; the sections' side is ui/NazgSection.h).
//
// FIRST STEP of that design: the section column, the strip, the board and the panel, drawn
// into the current window under whatever the caller put above them. The header, one fixed
// window filling SDL's, and the common screens come later. The regions' look is as much a
// first draft as the board's.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "model/NazgKeyboard.h"
#include "ui/NazgSection.h"

namespace nazg
{
    // `sections` are the open board's -- the column lists them, and is hidden when there is
    // only one -- and `active` the one shown, which the column changes. `keyboard` gives the
    // board the sections start from.
    void DrawSections(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, const Keyboard& keyboard);
}
