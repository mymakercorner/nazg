// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Theme - every colour Nazg chooses, in one place: the board's and the window's. A theme is
// one table of named colours (ui-design.md, "The board's look"): Light, Dark and Dracula, from
// the mockup ui-design/board-look.html. Panels -- a section's included -- use the four named
// colours and no other; the board is coloured by what a key means (ui/NazgBoardDescription.h),
// mapped to colours here.
//
// The theme and the keycap style are settings, set once a frame from Main.cpp with
// SetBoardStyle(); everything drawing reads them from here.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "imgui.h"

#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeycapLayout.h"

namespace nazg
{
    enum class ThemeId
    {
        Light,
        Dark,
        Dracula,
    };

    // How a keycap is drawn, independent of the theme. (The legend family, the other half of
    // the look, is in ui/NazgKeycapLayout.h, where legends are placed.) Outlined is the default (Rico,
    // 2026-10-03); Bottom lip -- a darker strip under the key -- reads best on light themes.
    enum class KeycapStyle
    {
        Outlined,
        BottomLip,
    };

    struct BoardStyle
    {
        ThemeId      theme   = ThemeId::Dark;
        KeycapStyle  keycaps = KeycapStyle::Outlined;
        LegendFamily legends = LegendFamily::Cylindrical;
    };

    void                     SetBoardStyle(const BoardStyle& style);
    [[nodiscard]] BoardStyle CurrentBoardStyle();

    // Ids are what imgui.ini keeps -- stable, lower case; names are for people.
    [[nodiscard]] std::span<const ThemeId>      Themes();
    [[nodiscard]] std::string_view              IdOf(ThemeId theme);
    [[nodiscard]] std::string_view              NameOf(ThemeId theme);
    [[nodiscard]] std::optional<ThemeId>        ThemeFromId(std::string_view id);
    [[nodiscard]] std::span<const KeycapStyle>  KeycapStyles();
    [[nodiscard]] std::string_view              IdOf(KeycapStyle style);
    [[nodiscard]] std::string_view              NameOf(KeycapStyle style);
    [[nodiscard]] std::optional<KeycapStyle>    KeycapStyleFromId(std::string_view id);
    [[nodiscard]] std::span<const LegendFamily> LegendFamilies();
    [[nodiscard]] std::string_view              IdOf(LegendFamily family);
    [[nodiscard]] std::string_view              NameOf(LegendFamily family);
    [[nodiscard]] std::optional<LegendFamily>   LegendFamilyFromId(std::string_view id);

    // The window's colours -- ImGui's style -- for `theme`. Colours only: sizes and their DPI
    // scaling are left alone.
    void ApplyWindowTheme(ThemeId theme);

    // The named colours for panels.
    enum class PanelColour
    {
        Error,
        Warning,
        Success,
        Muted,
    };

    [[nodiscard]] ImVec4 ColourOf(PanelColour colour);

    // ImGui::Text in one of them.
    void ColouredText(PanelColour colour, const char* format, ...) IM_FMTARGS(2);

    // The board's colours, by meaning, in the current theme.
    namespace BoardColours
    {
        // A keycap of one class: its face, the legends on it.
        struct Keycap
        {
            ImU32 face;
            ImU32 legend;
        };

        [[nodiscard]] Keycap Fill(KeyFill fill, float heat);
        [[nodiscard]] ImU32  Legend(LegendInk ink, KeyFill fill);
        [[nodiscard]] ImU32  EdgeLabel(uint8_t marks);
        [[nodiscard]] ImU32  Line(uint8_t marks);

        [[nodiscard]] ImU32 Plate();
        [[nodiscard]] ImU32 Lip();       // the Bottom lip's, one for every key
        [[nodiscard]] ImU32 Outline();   // the Outlined keycap's border

        // The states. Overlays are painted over the fill; outlines nest around the face.
        [[nodiscard]] ImU32 Hovered();                 // overlay
        [[nodiscard]] ImU32 Pressed();                 // overlay
        [[nodiscard]] ImU32 Dimmed();                  // overlay
        [[nodiscard]] ImU32 HighlightedTint();         // overlay
        [[nodiscard]] ImU32 HighlightedSecondTint();   // overlay
        [[nodiscard]] ImU32 CheckedTint();             // overlay
        [[nodiscard]] ImU32 Selected();                // outline
        [[nodiscard]] ImU32 Highlighted();             // outline
        [[nodiscard]] ImU32 HighlightedSecond();       // outline
        [[nodiscard]] ImU32 Warning();                 // outline
    }
}
