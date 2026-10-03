// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// LegendFont - the fonts legends are set in, and the text measurer KeycapLayout places them
// with (ui/NazgKeycapLayout.h).
//
// Arimo, Regular and Bold, each with Noto Sans Arabic, Math and Symbols 2 merged behind it for
// what it lacks (resources/fonts/README.md). Legend sizes are em sizes, as the mockup's CSS
// means them, while ImGui sizes a font by its line height -- ascent to descent, 1.117 em for
// Arimo, up to 2.1 for Noto Sans Arabic -- so each face is scaled by its own line height:
// every glyph then has the em size asked for, whichever face it comes from.
//
// ImGui only, no SDL: shared by the application and the font test, which loads the same files
// the same way.

#pragma once

#include <string>
#include <vector>

#include "ui/NazgKeycapLayout.h"

struct ImFont;
struct ImFontAtlas;

namespace nazg
{
    struct LegendFonts
    {
        ImFont* regular = nullptr;
        ImFont* bold    = nullptr;

        // ImGui's size for one em of the legend font: Arimo's line height in em.
        float sizePerEm = 1.0f;
    };

    // From `folder` -- UTF-8, ending with a separator. A missing Arimo leaves that weight null;
    // each file not found is added to `missing`.
    [[nodiscard]] LegendFonts LoadLegendFonts(ImFontAtlas& atlas, const std::string& folder,
                                              std::vector<std::string>& missing);

    // Text measured as ImGui draws it. A null weight falls back to the other, then to `fallback`
    // -- the interface's font -- so the board still draws without the bundled fonts.
    class ImGuiTextMeasurer : public TextMeasurer
    {
    public:
        ImGuiTextMeasurer(const LegendFonts& fonts, ImFont* fallback) : m_Fonts(fonts), m_Fallback(fallback) {}

        [[nodiscard]] ImFont* FontFor(LegendWeight weight) const;

        // ImGui's size for an em size.
        [[nodiscard]] float ImGuiSize(float size) const { return size * m_Fonts.sizePerEm; }

        [[nodiscard]] float     Width(std::string_view text, float size, LegendWeight weight) const override;
        [[nodiscard]] InkExtent Ink(std::string_view text, float size, LegendWeight weight) const override;

    private:
        LegendFonts m_Fonts;
        ImFont*     m_Fallback;
    };
}
