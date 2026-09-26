// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardDescription.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nazg
{
    BoardDescription DescribeKeyboard(const Keyboard& keyboard)
    {
        BoardDescription board;
        for (const DefinitionKey& key : PlaceKeys(keyboard.definition, keyboard.layoutSelection))
        {
            BoardKey described;
            described.geometry = key;
            board.keys.push_back(std::move(described));
        }
        return board;
    }

    std::pair<float, float> KeyCentre(const DefinitionKey& key)
    {
        const float x = key.x + key.width / 2.0f;
        const float y = key.y + key.height / 2.0f;
        if (key.rotation == 0.0f)
            return { x, y };

        // Clockwise on screen, since y grows downward -- as the renderer turns it.
        const float radians = key.rotation * 3.14159265358979f / 180.0f;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        const float dx      = x - key.rotationX;
        const float dy      = y - key.rotationY;
        return { key.rotationX + dx * cosine - dy * sine, key.rotationY + dx * sine + dy * cosine };
    }

    std::vector<float> SpreadApart(const std::vector<float>& wanted, const std::vector<float>& extents, float gap)
    {
        std::vector<size_t> order(wanted.size());
        std::iota(order.begin(), order.end(), size_t{ 0 });
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return wanted[a] < wanted[b]; });

        std::vector<float> placed(wanted);
        for (size_t i = 1; i < order.size(); ++i)
        {
            const size_t previous = order[i - 1];
            const size_t current  = order[i];
            const float  nearest  = placed[previous] + (extents[previous] + extents[current]) / 2.0f + gap;
            placed[current]       = std::max(placed[current], nearest);
        }
        return placed;
    }
}
