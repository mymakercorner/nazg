// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// WorkspaceLayout - the sizes of the open board's regions that the user drags, and the room
// each frame leaves: plain data, apart from ui/NazgWorkspace.h, which draws with it, so the
// settings (settings/NazgSettings.h) can keep it without ImGui.

#pragma once

namespace nazg
{
    // How the height under the strip is shared between the board and the panel: a splitter
    // between them, dragged, and remembered in settings.json (ui-design.md, "The keycode picker",
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
}
