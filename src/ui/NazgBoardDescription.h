// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardDescription - what a section wants the board to show, and what the board tells it
// back. Point 2 of the section contract (ui/NazgSection.h).
//
// Nazg draws the board; a section only fills it. So a section says WHAT each key means --
// its legends by position, the meaning of its fill, its state -- and never how it looks:
// colours, fonts and sizes belong to the board renderer (ui/NazgBoardView.h) and the theme
// (ui/NazgTheme.h), and a restyle touches only those. No pixels either: positions are in
// key units, as in the definition. The six rules below are ui-design.md's, "How a section
// describes the board", checked there against the Leyden Jar tool's keys, keycap colour
// themes and sublegends.
//
// Pure data, no ImGui, so it tests with literals.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/NazgKeyboard.h"

namespace nazg
{
    // Rule 1: legends by position -- KLE's twelve, in KLE's order: the top face's three
    // rows of three, left to right, then the front face.
    enum class LegendSlot : uint8_t
    {
        TopLeft,    TopCentre,    TopRight,
        MiddleLeft, MiddleCentre, MiddleRight,
        BottomLeft, BottomCentre, BottomRight,
        FrontLeft,  FrontCentre,  FrontRight,
    };

    inline constexpr size_t c_LegendSlotCount = 12;

    // What a legend is, for the renderer to style.
    enum class LegendRole : uint8_t
    {
        Label,       // what the key does: "A", "!", "LT 1"
        Sublegend,   // a second script some keycap sets print: Hiragana, Hangul
        Value,       // a reading: a signal level, a bin number
    };

    struct Legend
    {
        std::string text;   // empty: the slot is empty
        LegendRole  role = LegendRole::Label;
    };

    // Rule 2: the fill, by meaning.
    enum class KeyFill : uint8_t
    {
        Neutral,

        // The keycap's colour class, which keycap themes (Olivia, Dolch...) colour. Needs the
        // definition parser to keep each key's KLE colour, which it drops today.
        Alpha,
        Modifier,
        Accent,

        Heat,   // BoardKey::heat, 0 to 1 -- a heat map, overriding the theme while shown
    };

    // Rule 2 too: states, drawn as an outline or an overlay and never as the fill, so they
    // read on every theme. A bit set. Hovered is not one: the renderer knows it by itself.
    namespace Mark
    {
        inline constexpr uint8_t Selected    = 0x01;
        inline constexpr uint8_t Highlighted = 0x02;
        inline constexpr uint8_t Dimmed      = 0x04;
        inline constexpr uint8_t Warning     = 0x08;
        inline constexpr uint8_t Pressed     = 0x10;
        inline constexpr uint8_t Struck      = 0x20;   // edge labels: a position no key uses
    }

    struct BoardKey
    {
        // Rule 3: where it is. The definition's key unless the section brings its own
        // geometry -- the Leyden Jar's controller matrix is a plain grid. A decal takes
        // space and is never drawn; its legends and marks are ignored.
        DefinitionKey geometry;

        std::array<Legend, c_LegendSlotCount> legends;
        KeyFill fill  = KeyFill::Neutral;
        float   heat  = 0.0f;   // with KeyFill::Heat
        uint8_t marks = 0;

        Legend&       operator[](LegendSlot slot) { return legends[static_cast<size_t>(slot)]; }
        const Legend& operator[](LegendSlot slot) const { return legends[static_cast<size_t>(slot)]; }
    };

    // Rule 4: a line over the board through the centres of these keys, in this order --
    // the matrix view's wiring.
    struct BoardLine
    {
        std::vector<size_t> keys;        // indices into BoardDescription::keys
        uint8_t             marks = 0;   // Highlighted, Dimmed
    };

    // Rule 5: labels around the board's edges -- the matrix view's rulers, the Leyden Jar's
    // R0.../C0... connectors. Left and top are the only edges anything uses.
    enum class BoardEdge : uint8_t
    {
        Left,   // one per row: placed by the keys' centres, down the left side
        Top,    // one per column: placed by the keys' centres, along the top
    };

    struct EdgeLabel
    {
        BoardEdge           edge = BoardEdge::Left;
        std::string         text;
        std::vector<size_t> keys;        // it sits at their average position along its edge
        uint8_t             marks = 0;   // Highlighted, Dimmed, Struck
    };

    struct BoardDescription
    {
        std::vector<BoardKey>  keys;
        std::vector<BoardLine> lines;
        std::vector<EdgeLabel> labels;
    };

    // Rule 6: hover shared both ways. What the board saw this frame, told to the section --
    // which answers the other way by setting marks, from its panel as much as from here.
    struct BoardEvents
    {
        std::optional<size_t> hoveredKey;     // indices into BoardDescription::keys
        std::optional<size_t> clickedKey;
        std::optional<size_t> hoveredLabel;   // indices into BoardDescription::labels
        std::optional<size_t> clickedLabel;
    };

    // What a section starts from: the board as its definition draws it, at its layout
    // choice, decals included, every legend empty and nothing marked.
    [[nodiscard]] BoardDescription DescribeKeyboard(const Keyboard& keyboard);

    // A key's centre in key units, where it is drawn -- its rotation applied. An L-shaped
    // key's is its first rectangle's.
    [[nodiscard]] std::pair<float, float> KeyCentre(const DefinitionKey& key);

    // Where labels wanted at `wanted` along one edge go, each `extents` long: as wanted, but
    // pushed on just enough that none overlaps the one before it, `gap` apart. Labels keep
    // the order of their wanted positions; the result is by input index.
    [[nodiscard]] std::vector<float> SpreadApart(const std::vector<float>& wanted,
                                                 const std::vector<float>& extents,
                                                 float                     gap);
}
