// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Icon - the Tabler icons Nazg draws, by their code point in Tabler Icons 3.47.0's outline
// font, resources/fonts/tabler-icons.ttf (see its README.md). Which icon each section takes is
// ui-design.md, "The icon of every section".
//
// Only the icons in use are listed. Adding one is a line: its name as Tabler spells it, and its
// code point from the same version's tabler-icons.css (`.ti-<name>:before`). Tabler never moves
// a code point, but a new version must be checked against its CSS all the same.
//
// Pure: drawing them is the workspace's (ui/NazgWorkspace.h).

#pragma once

#include <string>

namespace nazg
{
    enum class Icon : char32_t
    {
        None = 0,   // no icon: a monogram of the label is drawn instead

        AdjustmentsHorizontal = 0xEC38,
        ArrowBarToDown        = 0xEC88,
        ArrowsJoin            = 0xEDAF,
        ArrowsLeftRight       = 0xEDB0,
        Bulb                  = 0xEA51,
        CircleDot             = 0xEFB1,
        Components            = 0xEFA5,
        DeviceDesktop         = 0xEA89,
        DeviceGamepad2        = 0xF1D2,
        DeviceMobileVibration = 0xEB86,
        Gauge                 = 0xEAB1,
        HandClick             = 0xEF4F,
        Keyboard              = 0xEBD6,
        Layout                = 0xEADB,
        PlayerPlay            = 0xED46,
        Repeat                = 0xEB72,
        Replace               = 0xEBC7,
        RotateClockwise       = 0xEB15,
        Settings              = 0xEB20,
        Sparkles              = 0xF6D7,
        Volume                = 0xEB51,
    };

    // The icon as UTF-8, to draw it as text. Empty for Icon::None.
    [[nodiscard]] std::string Utf8Of(Icon icon);
}
