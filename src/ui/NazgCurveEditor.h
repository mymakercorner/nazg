// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// CurveEditor - a Hall-effect Keychron's gamepad curve (ui-design.md, "The gamepad curve", after the mockup
// ui-design/switches-curve.html): travel across, what the stick gets up, in percent as the gamepad's test says
// it. Four points dragged, their values beside it typed or turned with the wheel, the Launcher's Linear and
// Smooth. A point stops short of its neighbours and the last stays at the bottom
// (keychron::WithCurvePoint()). The key tested is a dot on the curve.
//
// It only draws and says what was chosen: the Switches section writes it. ImGui only, no SDL.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/keychron/NazgKeychronCurve.h"

namespace nazg
{
    struct CurveEditorInput
    {
        const std::vector<keychron::CurvePoint>& curve;             // four points
        uint8_t                                  fullTravel = 40;   // 0.1 mm

        // The key tested, where the curve plays it, and how far down it is, in 0.1 mm and parts of it.
        std::string          key;
        std::optional<float> travel;
    };

    // What it remembers between frames: a drag in progress, shown and not written until let go.
    struct CurveEditorState
    {
        std::optional<size_t>             draggedPoint;
        std::vector<keychron::CurvePoint> dragged;   // the curve as the drag has it

        char typed[keychron::c_CurvePoints][2][8] = {};
    };

    // The curve to write, once a point is let go, a value typed, or a preset clicked.
    [[nodiscard]] std::optional<std::vector<keychron::CurvePoint>> DrawCurveEditor(CurveEditorState&       state,
                                                                                   const CurveEditorInput& input);
}
