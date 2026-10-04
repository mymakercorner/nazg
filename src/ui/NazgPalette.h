// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Palette - every colour Nazg chooses, as data: the themes' tables (ui-design.md, "The board's
// look"), and the command categories' colours, solved per keycap face (ui-design.md, "Legible on
// any theme and keycap colour, by computation").
//
// A theme gives a category only its hue. Its lightness is solved for the face it is drawn on, in
// OKLCH so the hue holds while the lightness moves: from the face's own lightness it walks darker
// on a light cap and lighter on a dark one, and stops at the first that reaches the target
// contrast -- WCAG's 4.5:1 for text and 3:1 for a graphic, Firmware 7:1 and 4.5:1 so it differs
// in lightness too, the one cue no colour deficiency removes. Chroma is clamped to sRGB. Where no
// lightness reaches the target -- a mid-tone cap -- there is no colour, and the text takes the
// legend's.
//
// Pure code, no ImGui: ui/NazgTheme.h hands these colours to ImGui, and the palette test checks
// every theme, keycap class and category against the rules above.

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // A colour packed as ImGui's IM_COL32 packs it -- red in the low byte, alpha in the high one
    // -- so the app hands it to ImGui as is. NazgTheme.cpp checks the two agree.
    using PackedColour = uint32_t;

    // 0xRRGGBB, and how opaque.
    constexpr PackedColour Hex(uint32_t rgb, float alpha = 1.0f)
    {
        return ((rgb >> 16) & 0xFFu) | (((rgb >> 8) & 0xFFu) << 8) | ((rgb & 0xFFu) << 16) |
               (static_cast<uint32_t>(alpha * 255.0f + 0.5f) << 24);
    }

    enum class ThemeId
    {
        Light,
        Dark,
        Dracula,
    };

    // A keycap of one class: its face, the legends on it.
    struct KeycapColours
    {
        PackedColour face;
        PackedColour legend;
    };

    // The command categories' hues, OKLCH degrees -- violet, cyan, amber, red -- found by a
    // search that simulates protanopia, deuteranopia and tritanopia and maximises the smallest
    // distance between categories on light and dark caps (ui-design.md, "The palette is chosen
    // for colour-blind users"). Theme tokens; Light, Dark and Dracula share them.
    struct CategoryHues
    {
        float behaviour = 300.0f;
        float host      = 200.0f;
        float board     = 80.0f;
        float firmware  = 30.0f;
    };

    struct Palette
    {
        // The window.
        PackedColour window, menubar, text, muted, control, border, accent, accentText;

        // The board.
        PackedColour  plate;
        KeycapColours alpha, modifier, accentCap;
        PackedColour  lip;   // Bottom lip: one shadow under every key, whatever its class
        PackedColour  outline;
        PackedColour  highlighted, highlightedTint, second, secondTint, checked, checkedTint, warning;
        PackedColour  pressed, hovered, dimmed;

        // The panels' named colours.
        PackedColour error, warningText, success;

        // The keycode picker's group titles, in a box (ui-design.md, "The tiles"): a recessed
        // neutral -- darker than the panel on Light, lighter on Dark, never a key's or a text
        // field's -- and its text.
        PackedColour groupLabel, groupLabelText;

        CategoryHues categories;
    };

    [[nodiscard]] const Palette& PaletteOf(ThemeId theme);

    // What a category colour is drawn as: a header's text, or the band along the top of the face.
    enum class CategoryUse : uint8_t
    {
        Text,
        Band,
    };

    [[nodiscard]] float HueOf(const CategoryHues& hues, CommandCategory category);

    // The contrast a category colour must reach against its face.
    [[nodiscard]] float TargetOf(CommandCategory category, CategoryUse use);

    // The category's colour on `face`, as vivid as legibility allows; nullopt where no lightness
    // of its hue reaches the target, or for CommandCategory::None. The contrast is that of the
    // colour as packed, so it holds exactly as drawn.
    [[nodiscard]] std::optional<PackedColour> SolveCategoryColour(PackedColour face, float hue, float target);

    [[nodiscard]] std::optional<PackedColour> CategoryColour(const Palette& palette, PackedColour face,
                                                             CommandCategory category, CategoryUse use);

    // The colour science underneath, for the tests: linear sRGB, WCAG's relative luminance and
    // contrast, and OKLab.
    struct LinearRgb
    {
        double r, g, b;
    };

    struct OkLab
    {
        double L, a, b;
    };

    [[nodiscard]] LinearRgb ToLinear(PackedColour colour);
    [[nodiscard]] double    Luminance(const LinearRgb& colour);
    [[nodiscard]] double    Contrast(PackedColour a, PackedColour b);
    [[nodiscard]] OkLab     ToOkLab(const LinearRgb& colour);
}
