// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeychronCurve.h"

#include <algorithm>

namespace nazg::keychron
{
    bool IsWritableCurve(const std::vector<CurvePoint>& curve) noexcept
    {
        if (curve.size() != c_CurvePoints)
            return false;
        for (size_t point = 0; point < curve.size(); ++point)
            if (curve[point].y > c_CurveOutputMax || (point > 0 && curve[point].x <= curve[point - 1].x))
                return false;
        return true;
    }

    std::vector<CurvePoint> LinearCurve(uint8_t fullTravel)
    {
        return { { 0, 0 },
                 { static_cast<uint8_t>(fullTravel / 3), 42 },
                 { static_cast<uint8_t>(fullTravel * 2 / 3), 84 },
                 { fullTravel, c_CurveOutputMax } };
    }

    std::vector<CurvePoint> SmoothCurve(uint8_t fullTravel)
    {
        // Rounded where Linear is rounded down: 12 on the K10 HE V3, beside Linear's 11.
        return { { 0, 0 },
                 { static_cast<uint8_t>((fullTravel * 2 + 3) / 6), 19 },
                 { static_cast<uint8_t>((fullTravel * 4 + 3) / 6), 48 },
                 { fullTravel, c_CurveOutputMax } };
    }

    std::vector<CurvePoint> WithCurvePoint(std::vector<CurvePoint> curve, size_t point, int x, int y)
    {
        if (point >= curve.size())
            return curve;

        const bool isFirst = point == 0, isLast = point + 1 == curve.size();
        const int  fromX = isFirst ? 0 : curve[point - 1].x + 1;
        const int  toX   = isLast ? curve[point].x : curve[point + 1].x - 1;
        const int  fromY = isFirst ? 0 : curve[point - 1].y;
        const int  toY   = isLast ? c_CurveOutputMax : curve[point + 1].y;

        // Between neighbours that leave no room -- a curve nobody here wrote -- a point stays.
        if (!isLast && fromX <= toX)
            curve[point].x = static_cast<uint8_t>(std::clamp(x, fromX, toX));
        if (fromY <= toY)
            curve[point].y = static_cast<uint8_t>(std::clamp(y, fromY, toY));
        return curve;
    }

    float CurveOutput(const std::vector<CurvePoint>& curve, float travel) noexcept
    {
        if (curve.empty() || travel <= 0.0f || travel < curve.front().x)
            return 0.0f;
        for (size_t point = 1; point < curve.size(); ++point)
        {
            const CurvePoint from = curve[point - 1], to = curve[point];
            if (travel >= to.x || to.x <= from.x)
                continue;
            return static_cast<float>(from.y) +
                   static_cast<float>(to.y - from.y) * (travel - static_cast<float>(from.x)) / static_cast<float>(to.x - from.x);
        }
        return curve.back().y;
    }
}
