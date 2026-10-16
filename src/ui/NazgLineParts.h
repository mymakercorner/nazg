// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// LineParts - what the panels drawn as lines of settings share: Lighting, Audio and a board's
// own menus. A line's name and where it starts, whether the pointer is over it -- the board
// lights the keys that change what the line under the pointer sets -- and the colour slider.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>

namespace nazg
{
    // A line of a two-column table: its name in the first column, its controls to follow in the
    // second. Returns the line's top, for IsOverLine() once it is drawn.
    [[nodiscard]] float BeginLine(const char* name);

    // The pointer is over the line that began at `top`, now its last item is drawn.
    [[nodiscard]] bool IsOverLine(float top);

    // A 0-255 slider `width` wide over a colour band: the hue circle, or white to `hue`. True
    // while it moves; `letGo` when it was let go after a change.
    bool ColourSlider(const char* id, int& value, float width, bool hueTrack, uint8_t hue, bool& letGo);
}
