// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A key's outline as one polygon: an L-shaped key's two KLE rectangles traced together, so
// its border and state outlines follow the whole key instead of crossing inside it.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgKeyShape.h"

#include "TestSupport.h"

#include <cmath>
#include <vector>

using nazg::ContourPoint;
using nazg::DefinitionKey;
using nazg::InsetContour;
using nazg::IsInnerCorner;
using nazg::KeyContour;

namespace
{
    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }

    bool Same(const std::vector<ContourPoint>& contour, const std::vector<ContourPoint>& expected)
    {
        if (contour.size() != expected.size())
            return false;
        for (size_t i = 0; i < contour.size(); ++i)
            if (!Near(contour[i].x, expected[i].x) || !Near(contour[i].y, expected[i].y))
                return false;
        return true;
    }

    size_t InnerCorners(const std::vector<ContourPoint>& contour)
    {
        size_t inner = 0;
        for (size_t i = 0; i < contour.size(); ++i)
            inner += IsInnerCorner(contour, i) ? 1 : 0;
        return inner;
    }

    void TestOneRectangle()
    {
        std::printf("one rectangle\n");

        DefinitionKey key;
        key.x     = 2.0f;
        key.y     = 1.0f;
        key.width = 1.5f;

        const std::vector<ContourPoint> contour = KeyContour(key);
        Check(Same(contour, { { 2, 1 }, { 3.5f, 1 }, { 3.5f, 2 }, { 2, 2 } }),
              "four corners, clockwise from the top left");
        Check(InnerCorners(contour) == 0, "no inner corner");
    }

    // KLE's ISO Enter: 1.25u x 2u at x = 0.25, with a 1.5u x 1u second rectangle at its top left.
    DefinitionKey IsoEnter()
    {
        DefinitionKey key;
        key.x            = 0.25f;
        key.width        = 1.25f;
        key.height       = 2.0f;
        key.secondX      = -0.25f;
        key.secondWidth  = 1.5f;
        key.secondHeight = 1.0f;
        return key;
    }

    void TestIsoEnter()
    {
        std::printf("ISO Enter\n");

        const std::vector<ContourPoint> contour = KeyContour(IsoEnter());
        Check(Same(contour, { { 0, 0 }, { 1.5f, 0 }, { 1.5f, 2 }, { 0.25f, 2 }, { 0.25f, 1 }, { 0, 1 } }),
              "six corners, traced around both rectangles at once");
        Check(InnerCorners(contour) == 1, "one inner corner");
        Check(contour.size() == 6 && IsInnerCorner(contour, 4), "where the narrow part meets the wide one");
    }

    void TestBigAssEnter()
    {
        std::printf("big-ass Enter\n");

        // 1.5u x 2u, with a 2.25u x 1u second rectangle reaching left along its bottom.
        DefinitionKey key;
        key.x            = 0.75f;
        key.width        = 1.5f;
        key.height       = 2.0f;
        key.secondX      = -0.75f;
        key.secondY      = 1.0f;
        key.secondWidth  = 2.25f;
        key.secondHeight = 1.0f;

        const std::vector<ContourPoint> contour = KeyContour(key);
        Check(Same(contour, { { 0.75f, 0 }, { 2.25f, 0 }, { 2.25f, 2 }, { 0, 2 }, { 0, 1 }, { 0.75f, 1 } }),
              "six corners, the inner one on the top edge's left");
        Check(InnerCorners(contour) == 1, "one inner corner");
    }

    void TestContainedSecondRectangle()
    {
        std::printf("a second rectangle inside the first\n");

        // A stepped Caps Lock: its step is drawn inside the key.
        DefinitionKey key;
        key.width        = 1.75f;
        key.secondWidth  = 1.25f;
        key.secondHeight = 1.0f;

        Check(Same(KeyContour(key), { { 0, 0 }, { 1.75f, 0 }, { 1.75f, 1 }, { 0, 1 } }),
              "the outline is the first rectangle's");
    }

    void TestInset()
    {
        std::printf("inset\n");

        const std::vector<ContourPoint> inset = InsetContour(KeyContour(IsoEnter()), 0.1f);
        Check(Same(inset,
                   { { 0.1f, 0.1f }, { 1.4f, 0.1f }, { 1.4f, 1.9f }, { 0.35f, 1.9f }, { 0.35f, 0.9f }, { 0.1f, 0.9f } }),
              "every edge moves in by the same distance, the inner corner with them");

        const std::vector<ContourPoint> outset = InsetContour(KeyContour(IsoEnter()), -0.1f);
        Check(Same(outset, { { -0.1f, -0.1f }, { 1.6f, -0.1f }, { 1.6f, 2.1f }, { 0.15f, 2.1f }, { 0.15f, 1.1f },
                             { -0.1f, 1.1f } }),
              "a negative distance moves it out");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestOneRectangle();
    TestIsoEnter();
    TestBigAssEnter();
    TestContainedSecondRectangle();
    TestInset();

    return TestResult();
}
