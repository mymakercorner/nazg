// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyShape - a key's outline as one polygon. An L-shaped key's two KLE rectangles are traced
// together, so its fill, its border, its lip and its state outlines follow the whole key once,
// instead of two rectangles crossing inside it (ui-design.md, "The board's look": outlines follow
// the key's contour).
//
// Pure geometry, no ImGui, so it tests with literals.

#pragma once

#include <cstddef>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

namespace nazg
{
    struct ContourPoint
    {
        float x = 0.0f;
        float y = 0.0f;

        bool operator==(const ContourPoint&) const = default;
    };

    // The corners of the key's outline, in key units, unrotated: clockwise on screen (y grows
    // downward), from the leftmost corner of the top edge, one point per corner. A key of one
    // rectangle has four; an ISO Enter six.
    [[nodiscard]] std::vector<ContourPoint> KeyContour(const DefinitionKey& key);

    // The same outline moved `distance` inward on every side -- outward when negative. Only for
    // outlines whose edges are all horizontal or vertical, as KeyContour() makes them.
    [[nodiscard]] std::vector<ContourPoint> InsetContour(const std::vector<ContourPoint>& contour, float distance);

    // Whether the outline turns inward at `corner` -- the inner corner of an L.
    [[nodiscard]] bool IsInnerCorner(const std::vector<ContourPoint>& contour, size_t corner);
}
