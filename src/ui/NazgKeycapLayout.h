// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycapLayout - where a key's legends go on its face, in pixels: the second of the three
// layers of ui-design.md, "How the board's look is built". A key's content in (what it says,
// ui/NazgKeycapLegend.h), with the legend family and the face's size; a few primitives out --
// text with a colour role, drawn arrows -- which ui/NazgBoardView.h draws through the theme.
// Every placement rule lives here, after the mockup ui-design/board-look.html, whose code is
// the reference for the maths:
//
// - Cylindrical, as GMK's Cherry legends: top left, mixed case; a 1u modifier centred both
//   ways; wider modifiers, the F-keys and Esc at the left, centred vertically. Spherical, as
//   SA's: everything centred, capitals, modifiers at the letters' weight.
// - A legend is never scaled to fit: its name on one line, on two, its short form, then cut
//   with "...". Arrows are drawn -- no font has them at a legend's weight.
// - The AltGr character bottom right, always; Bépo's fourth level top right, cylindrical only;
//   the numpad's second legends, cylindrical only.
// - Headers top right, under the band, one above the other: a tap-hold's hold, then a command's
//   own header, each in its category's colour. A command's main legend is centred on the whole
//   key, as any key's, and pushed below the headers only where it would run into them; so is a
//   tap's legend under a long hold -- Alt Gr over W on 1u -- which then drops its Shift
//   character if the pair no longer fits.
// - Some glyphs are placed by their ink, not where the font puts them: "-", "_", "`" centred on
//   a capital's height, "~" resting on the baseline.
//
// Text is measured through an interface: ImGui's fonts in the app, a fake in the rule tests,
// Arimo loaded through ImGui's core in the font test. Sizes are em sizes, as CSS means them --
// the mockup's -- so the decided proportions hold whatever a font's line height is.
//
// Pure code, no ImGui, so it tests with literals.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // How legends are set, after the two keycap families (ui-design.md, "What real keycap sets
    // do"): Cylindrical as GMK prints them -- top left, mixed case -- or Spherical as SA does --
    // centred, capitals. A setting, Cylindrical by default (Rico, 2026-10-03).
    enum class LegendFamily
    {
        Cylindrical,
        Spherical,
    };

    // Sizes, as shares of a key unit (ui-design.md, "The floor is on the header"): letters
    // 0.30 of a unit, modifier text 3/5 of the letters, a header 1/2 -- the smallest text on the
    // board, held at 9 px or more, so a unit is 60 px at least.
    inline constexpr float c_LetterShare   = 0.30f;
    inline constexpr float c_ModifierShare = 0.6f;   // of the letter size
    inline constexpr float c_HeaderShare   = 0.5f;   // of the letter size
    inline constexpr float c_SmallestText  = 9.0f;
    inline constexpr float c_SmallestUnit  = c_SmallestText / (c_LetterShare * c_HeaderShare);

    // Fractions of a key unit.
    inline constexpr float c_KeyGap    = 0.06f;   // between two keys, on each side
    inline constexpr float c_LegendPad = 0.10f;   // between a face's edge and its legends
    inline constexpr float c_BandShare = 0.055f;  // a command's band along the top of the face

    // The band's height in pixels: a share of the unit, never thinner than 2 px.
    [[nodiscard]] inline float BandHeight(float unit)
    {
        return c_BandShare * unit > 2.0f ? c_BandShare * unit : 2.0f;
    }

    // The legend font's two weights. Arimo comes in Regular and Bold; the mockup's 500 for
    // letters became Bold, its 400 Regular.
    enum class LegendWeight : uint8_t
    {
        Regular,
        Bold,
    };

    // Where a run of text leaves ink, in pixels from the top of its line as the measurer
    // places it -- ImGui's AddText position.
    struct InkExtent
    {
        float top    = 0.0f;
        float bottom = 0.0f;
    };

    class TextMeasurer
    {
    public:
        virtual ~TextMeasurer() = default;

        // At `size` -- an em size in pixels, as CSS means it.
        [[nodiscard]] virtual float     Width(std::string_view text, float size, LegendWeight weight) const = 0;
        [[nodiscard]] virtual InkExtent Ink(std::string_view text, float size, LegendWeight weight) const = 0;
    };

    // What colours a primitive, for the theme to resolve against the keycap's class.
    enum class LegendInk : uint8_t
    {
        Legend,
        Muted,   // the AltGr character beside a spherical pair: set apart by lightness
        Value,   // a slot legend's reading
    };

    // Text at (x, y), the top-left of its line as the measurer places it. A header carries its
    // category, whose colour the theme solves for the keycap's face; None: the ink's colour.
    struct PlacedText
    {
        std::string     text;
        float           x        = 0.0f;
        float           y        = 0.0f;
        float           size     = 0.0f;
        float           width    = 0.0f;
        LegendWeight    weight   = LegendWeight::Regular;
        LegendInk       ink      = LegendInk::Legend;
        bool            cut      = false;   // ended with "..." as nothing shorter fitted
        CommandCategory category = CommandCategory::None;
    };

    // An arrow drawn as a shaft and a filled head, centred on (x, y), `size` long.
    struct PlacedArrow
    {
        float          x         = 0.0f;
        float          y         = 0.0f;
        float          size      = 0.0f;
        float          stroke    = 0.0f;
        ArrowDirection direction = ArrowDirection::Up;
        LegendInk      ink       = LegendInk::Legend;
    };

    struct KeycapPrimitives
    {
        std::vector<PlacedText>  texts;
        std::vector<PlacedArrow> arrows;
    };

    // A keycap's face, in pixels -- the rectangle legends go on: an L-shaped key's first.
    struct FaceBox
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    // `unit`: a key unit in pixels; `oneUnit`: the key is 1u or smaller, which centres a
    // modifier on cylindrical sets.
    [[nodiscard]] KeycapPrimitives LayOutKeycap(const KeycapLegend& legend, LegendFamily family, const FaceBox& face,
                                                float unit, bool oneUnit, const TextMeasurer& measurer);

    // Slot legends, placed where the section put them: four rows -- top, middle, bottom,
    // front -- of three, at the modifier text's size, each cut rather than scaled.
    [[nodiscard]] KeycapPrimitives LayOutSlots(const SlotLegends& slots, const FaceBox& face, float unit,
                                               const TextMeasurer& measurer);

    // The arrow's two parts, for whoever draws one: a shaft from `tail` to `shaftEnd`, then a
    // filled head with its point at `tip`.
    struct ArrowShape
    {
        float tailX, tailY, shaftEndX, shaftEndY;
        float tipX, tipY, baseAX, baseAY, baseBX, baseBY;
    };

    [[nodiscard]] ArrowShape ShapeOf(const PlacedArrow& arrow);
}
