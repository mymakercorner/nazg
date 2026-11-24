// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A Hall-effect Keychron's gamepad curve: how far an analog axis goes for how far its key is pressed
// (keychron-commands.md, "Game controller"; read and played on a K10 HE V3, 2026-11-24). Four points, the
// whole board's. The bytes are NazgKeychronProtocol.h's; here, which curves are safe to write, what moving
// a point makes of one, and one played as the firmware plays it.
//
// Pure: no device, no window.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"

namespace nazg::keychron
{
    inline constexpr size_t  c_CurvePoints    = 4;
    inline constexpr uint8_t c_CurveOutputMax = 127;

    // Whether a curve may be written: four points, each further down than the one before -- the firmware
    // checks the curve it already has, so one that does not ascend would be accepted, and never replaced
    // again -- and no output over 127.
    [[nodiscard]] bool IsWritableCurve(const std::vector<CurvePoint>& curve) noexcept;

    // The Launcher's two, scaled to the board's full travel (0.1 mm): (0, 0) (11, 42) (23, 84) (35, 127)
    // and (0, 0) (12, 19) (23, 48) (35, 127) on a K10 HE V3, both read there as the Launcher wrote them.
    [[nodiscard]] std::vector<CurvePoint> LinearCurve(uint8_t fullTravel);
    [[nodiscard]] std::vector<CurvePoint> SmoothCurve(uint8_t fullTravel);

    // `curve` with one point moved, as near as it may go: 0.1 mm from its neighbours, its output between
    // theirs. The last point keeps its travel: it is how a client knows the board's (EstimateFullTravel()),
    // and the point before it at full output does what moving it would.
    [[nodiscard]] std::vector<CurvePoint> WithCurvePoint(std::vector<CurvePoint> curve, size_t point, int x, int y);

    // What the axis gets, 0 to 127, at a travel in 0.1 mm: nothing at rest nor under the first point, a
    // straight line from one to the next, and the last point's past it. The board plays it at whole tenths,
    // rounded down, as recorded on the K10 HE V3; a drawing follows the line between them (Rico, 2026-11-24: a
    // dot at the steps jumped).
    [[nodiscard]] float CurveOutput(const std::vector<CurvePoint>& curve, float travel) noexcept;
}
