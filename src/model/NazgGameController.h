// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// GameController - what the computer receives from a board's own game controller, as the shell reads
// it (PlatformServices::readGameController): the Switches section's gamepad test shows this rather than
// what the board should be sending, which would look right while games get nothing (ui-design.md,
// "Round two: advanced keys"). Plain data: no SDL.

#pragma once

#include <cstddef>
#include <vector>

namespace nazg
{
    struct GameControllerReading
    {
        // Whether the computer sees the board as a controller at all -- found by its vendor and product
        // numbers among whatever controllers there are -- and how many others there are.
        bool   isSeen = false;
        size_t others = 0;

        // An Xbox controller, its parts in this order: axes left X, left Y, right X, right Y, left and
        // right trigger; buttons A, B, X, Y, left and right bumper, View, Menu, the left and right stick
        // pressed, the D-pad's up, down, left and right, Xbox. Else a plain joystick: its axes as HID
        // orders them -- X, Y, Z, Rx, Ry, Rz -- and its buttons by number.
        bool isXbox = false;

        // -1 to 1 -- 0 to 1 for an Xbox controller's triggers -- X to the right and Y down, as both send
        // them.
        std::vector<float> axes;
        std::vector<bool>  buttons;
    };
}
