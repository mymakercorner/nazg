// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyShape.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace nazg
{
    namespace
    {
        struct Rectangle
        {
            float x0, y0, x1, y1;

            bool Contains(float x, float y) const { return x > x0 && x < x1 && y > y0 && y < y1; }
        };

        // Sorted, with values closer than a rounding error counted once.
        std::vector<float> Distinct(std::vector<float> values)
        {
            std::sort(values.begin(), values.end());
            std::vector<float> distinct;
            for (float value : values)
                if (distinct.empty() || value - distinct.back() > 1e-4f)
                    distinct.push_back(value);
            return distinct;
        }

        std::vector<ContourPoint> CornersOf(const Rectangle& r)
        {
            return { { r.x0, r.y0 }, { r.x1, r.y0 }, { r.x1, r.y1 }, { r.x0, r.y1 } };
        }

        // Unit direction of the edge from `a` to `b`.
        ContourPoint Direction(ContourPoint a, ContourPoint b)
        {
            const float dx     = b.x - a.x;
            const float dy     = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            return length > 0.0f ? ContourPoint{ dx / length, dy / length } : ContourPoint{};
        }

        // The side of an edge the key is on: walking clockwise on screen, the right-hand side.
        ContourPoint InwardOf(ContourPoint direction)
        {
            return { -direction.y, direction.x };
        }
    }

    std::vector<ContourPoint> KeyContour(const DefinitionKey& key)
    {
        std::vector<Rectangle> rectangles{ { key.x, key.y, key.x + key.width, key.y + key.height } };
        if (key.HasSecondRectangle())
        {
            const float x = key.x + key.secondX;
            const float y = key.y + key.secondY;
            rectangles.push_back({ x, y, x + key.secondWidth, y + key.secondHeight });
        }
        if (rectangles.size() == 1)
            return CornersOf(rectangles[0]);

        // The rectangles' edges cut the plane into at most 3 x 3 cells; the key is the cells
        // either covers, and its outline the cell sides with the key on one side only.
        std::vector<float> xs, ys;
        for (const Rectangle& r : rectangles)
        {
            xs.insert(xs.end(), { r.x0, r.x1 });
            ys.insert(ys.end(), { r.y0, r.y1 });
        }
        xs = Distinct(std::move(xs));
        ys = Distinct(std::move(ys));

        const int columns = static_cast<int>(xs.size()) - 1;
        const int rows    = static_cast<int>(ys.size()) - 1;
        auto      covered = [&](int column, int row)
        {
            if (column < 0 || row < 0 || column >= columns || row >= rows)
                return false;
            const float x = (xs[column] + xs[column + 1]) / 2.0f;
            const float y = (ys[row] + ys[row + 1]) / 2.0f;
            return std::any_of(rectangles.begin(), rectangles.end(),
                               [&](const Rectangle& r) { return r.Contains(x, y); });
        };

        // Each boundary side as an edge between grid corners, oriented clockwise on screen:
        // tops run right, right sides down, bottoms left, left sides up.
        using Corner = std::pair<int, int>;
        std::map<Corner, Corner> next;
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
            {
                if (!covered(column, row))
                    continue;
                if (!covered(column, row - 1))
                    next[{ column, row }] = { column + 1, row };
                if (!covered(column + 1, row))
                    next[{ column + 1, row }] = { column + 1, row + 1 };
                if (!covered(column, row + 1))
                    next[{ column + 1, row + 1 }] = { column, row + 1 };
                if (!covered(column - 1, row))
                    next[{ column, row + 1 }] = { column, row };
            }

        // From the leftmost corner of the top edge: the smallest row, then the smallest column.
        Corner start{ columns + 1, rows + 1 };
        for (const auto& [from, to] : next)
            if (from.second < start.second || (from.second == start.second && from.first < start.first))
                start = from;

        // Walk the edges, keeping only the points where the direction turns.
        std::vector<Corner> walked{ start };
        for (Corner at = next[start]; at != start; at = next[at])
        {
            walked.push_back(at);
            if (walked.size() > next.size() || next.count(at) == 0)
                return CornersOf(rectangles[0]);   // two rectangles touching at a corner only
        }

        std::vector<ContourPoint> contour;
        for (size_t i = 0; i < walked.size(); ++i)
        {
            const Corner& before = walked[(i + walked.size() - 1) % walked.size()];
            const Corner& here   = walked[i];
            const Corner& after  = walked[(i + 1) % walked.size()];
            const bool    turns  = (here.first - before.first) * (after.second - here.second) !=
                                   (here.second - before.second) * (after.first - here.first);
            if (turns)
                contour.push_back({ xs[here.first], ys[here.second] });
        }
        return contour;
    }

    std::vector<ContourPoint> InsetContour(const std::vector<ContourPoint>& contour, float distance)
    {
        // At a right-angled corner, moving both edges inward moves the corner along the sum of
        // their inward sides -- which also holds where the outline turns inward.
        std::vector<ContourPoint> inset;
        const size_t              count = contour.size();
        for (size_t i = 0; i < count; ++i)
        {
            const ContourPoint before = contour[(i + count - 1) % count];
            const ContourPoint here   = contour[i];
            const ContourPoint after  = contour[(i + 1) % count];
            const ContourPoint in     = InwardOf(Direction(before, here));
            const ContourPoint out    = InwardOf(Direction(here, after));
            inset.push_back({ here.x + distance * (in.x + out.x), here.y + distance * (in.y + out.y) });
        }
        return inset;
    }

    bool IsInnerCorner(const std::vector<ContourPoint>& contour, size_t corner)
    {
        const size_t       count  = contour.size();
        const ContourPoint before = contour[(corner + count - 1) % count];
        const ContourPoint here   = contour[corner];
        const ContourPoint after  = contour[(corner + 1) % count];
        const ContourPoint in     = Direction(before, here);
        const ContourPoint out    = Direction(here, after);

        // Clockwise on screen, the outer corners turn right: a positive cross product.
        return in.x * out.y - in.y * out.x < 0.0f;
    }
}
