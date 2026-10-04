// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgTheme.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <map>
#include <tuple>

namespace nazg
{
    namespace
    {
        // The palette's colours go to ImGui as they are: both pack red in the low byte.
        static_assert(Hex(0x123456, 0.5f) == IM_COL32(0x12, 0x34, 0x56, 128), "the palette packs as IM_COL32 does");

        constexpr std::array<ThemeId, 3>     c_Themes{ ThemeId::Light, ThemeId::Dark, ThemeId::Dracula };
        constexpr std::array<KeycapStyle, 2> c_KeycapStyles{ KeycapStyle::Outlined, KeycapStyle::BottomLip };
        constexpr std::array<LegendFamily, 2> c_LegendFamilies{ LegendFamily::Cylindrical, LegendFamily::Spherical };

        BoardStyle g_Style;

        const Palette& Current()
        {
            return PaletteOf(g_Style.theme);
        }

        ImVec4 ToFloat(ImU32 colour, float alpha = 1.0f)
        {
            ImVec4 value = ImGui::ColorConvertU32ToFloat4(colour);
            value.w      = alpha;
            return value;
        }

        ImVec4 Mix(ImU32 from, ImU32 to, float share)
        {
            const ImVec4 a = ToFloat(from);
            const ImVec4 b = ToFloat(to);
            return ImVec4(a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share, a.z + (b.z - a.z) * share, 1.0f);
        }
    }

    void SetBoardStyle(const BoardStyle& style)
    {
        g_Style = style;
    }

    BoardStyle CurrentBoardStyle()
    {
        return g_Style;
    }

    std::span<const ThemeId> Themes()
    {
        return c_Themes;
    }

    std::string_view IdOf(ThemeId theme)
    {
        switch (theme)
        {
        case ThemeId::Light:   return "light";
        case ThemeId::Dark:    break;
        case ThemeId::Dracula: return "dracula";
        }
        return "dark";
    }

    std::string_view NameOf(ThemeId theme)
    {
        switch (theme)
        {
        case ThemeId::Light:   return "Light";
        case ThemeId::Dark:    break;
        case ThemeId::Dracula: return "Dracula";
        }
        return "Dark";
    }

    std::optional<ThemeId> ThemeFromId(std::string_view id)
    {
        for (ThemeId theme : c_Themes)
            if (IdOf(theme) == id)
                return theme;
        return std::nullopt;
    }

    std::span<const KeycapStyle> KeycapStyles()
    {
        return c_KeycapStyles;
    }

    std::string_view IdOf(KeycapStyle style)
    {
        return style == KeycapStyle::BottomLip ? "bottom-lip" : "outlined";
    }

    std::string_view NameOf(KeycapStyle style)
    {
        return style == KeycapStyle::BottomLip ? "Bottom lip" : "Outlined";
    }

    std::optional<KeycapStyle> KeycapStyleFromId(std::string_view id)
    {
        for (KeycapStyle style : c_KeycapStyles)
            if (IdOf(style) == id)
                return style;
        return std::nullopt;
    }

    std::span<const LegendFamily> LegendFamilies()
    {
        return c_LegendFamilies;
    }

    std::string_view IdOf(LegendFamily family)
    {
        return family == LegendFamily::Spherical ? "spherical" : "cylindrical";
    }

    std::string_view NameOf(LegendFamily family)
    {
        return family == LegendFamily::Spherical ? "Spherical" : "Cylindrical";
    }

    std::optional<LegendFamily> LegendFamilyFromId(std::string_view id)
    {
        for (LegendFamily family : c_LegendFamilies)
            if (IdOf(family) == id)
                return family;
        return std::nullopt;
    }

    void ApplyWindowTheme(ThemeId theme)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        if (theme == ThemeId::Light)
            ImGui::StyleColorsLight(&style);
        else
            ImGui::StyleColorsDark(&style);

        const Palette& t      = PaletteOf(theme);
        ImVec4*      colour = style.Colors;

        colour[ImGuiCol_Text]                 = ToFloat(t.text);
        colour[ImGuiCol_TextDisabled]         = ToFloat(t.muted);
        colour[ImGuiCol_WindowBg]             = ToFloat(t.window);
        colour[ImGuiCol_ChildBg]              = ToFloat(t.window, 0.0f);
        colour[ImGuiCol_PopupBg]              = ToFloat(t.control);
        colour[ImGuiCol_MenuBarBg]            = ToFloat(t.menubar);
        colour[ImGuiCol_Border]               = ToFloat(t.border);
        colour[ImGuiCol_FrameBg]              = ToFloat(t.control);
        colour[ImGuiCol_FrameBgHovered]       = Mix(t.control, t.accent, 0.15f);
        colour[ImGuiCol_FrameBgActive]        = Mix(t.control, t.accent, 0.30f);
        colour[ImGuiCol_TitleBg]              = ToFloat(t.menubar);
        colour[ImGuiCol_TitleBgActive]        = ToFloat(t.menubar);
        colour[ImGuiCol_ScrollbarBg]          = ToFloat(t.window);
        colour[ImGuiCol_CheckMark]            = ToFloat(t.accent);
        colour[ImGuiCol_SliderGrab]           = ToFloat(t.accent);
        colour[ImGuiCol_SliderGrabActive]     = ToFloat(t.accent);
        colour[ImGuiCol_Button]               = ToFloat(t.accent, 0.40f);
        colour[ImGuiCol_ButtonHovered]        = ToFloat(t.accent);
        colour[ImGuiCol_ButtonActive]         = Mix(t.accent, t.text, 0.15f);
        colour[ImGuiCol_Header]               = ToFloat(t.accent, 0.31f);
        colour[ImGuiCol_HeaderHovered]        = ToFloat(t.accent, 0.80f);
        colour[ImGuiCol_HeaderActive]         = ToFloat(t.accent);
        colour[ImGuiCol_Tab]                  = ToFloat(t.accent, 0.25f);
        colour[ImGuiCol_TabHovered]           = ToFloat(t.accent, 0.80f);
        colour[ImGuiCol_TabSelected]          = ToFloat(t.accent, 0.60f);
        colour[ImGuiCol_TextSelectedBg]       = ToFloat(t.accent, 0.35f);
        colour[ImGuiCol_NavCursor]            = ToFloat(t.accent);
        colour[ImGuiCol_Separator]            = ToFloat(t.border);
        colour[ImGuiCol_ResizeGrip]           = ToFloat(t.accent, 0.20f);
        colour[ImGuiCol_ResizeGripHovered]    = ToFloat(t.accent, 0.67f);
        colour[ImGuiCol_ResizeGripActive]     = ToFloat(t.accent, 0.95f);
    }

    ImVec4 ColourOf(PanelColour colour)
    {
        const Palette& t = Current();
        switch (colour)
        {
        case PanelColour::Error:   return ToFloat(t.error);
        case PanelColour::Warning: return ToFloat(t.warningText);
        case PanelColour::Success: return ToFloat(t.success);
        case PanelColour::Muted:   break;
        }
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }

    void ColouredText(PanelColour colour, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextColoredV(ColourOf(colour), format, args);
        va_end(args);
    }

    namespace BoardColours
    {
        Keycap Fill(KeyFill fill, float heat)
        {
            const Palette& t = Current();
            switch (fill)
            {
            case KeyFill::Neutral:
            case KeyFill::Alpha:    return t.alpha;
            case KeyFill::Modifier: return t.modifier;
            case KeyFill::Accent:   return t.accentCap;
            case KeyFill::Heat:     break;
            }

            // A heat map: from the alpha face towards a saturated blue, whatever the theme.
            const ImVec4 cold = ToFloat(t.alpha.face);
            const ImVec4 hot  = ToFloat(Hex(0x2d2dff));
            const float  s    = std::clamp(heat, 0.0f, 1.0f);
            const ImU32  face = ImGui::ColorConvertFloat4ToU32(
                ImVec4(cold.x + (hot.x - cold.x) * s, cold.y + (hot.y - cold.y) * s, cold.z + (hot.z - cold.z) * s, 1.0f));
            return { face, s > 0.5f ? Hex(0xffffff) : t.alpha.legend };
        }

        ImU32 Legend(LegendInk ink, KeyFill fill)
        {
            if (ink == LegendInk::Value)
                return Current().checked;

            const Keycap keycap = Fill(fill, 0.0f);
            if (ink == LegendInk::Legend)
                return keycap.legend;

            // Muted: set apart by lightness, since hue belongs to the command categories -- the
            // legend colour taken 45% towards the face (ui-design.md, "spherical AltGr").
            return ImGui::ColorConvertFloat4ToU32(Mix(keycap.legend, keycap.face, 0.45f));
        }

        std::optional<ImU32> Category(CommandCategory category, CategoryUse use, KeyFill fill, float heat)
        {
            // Solving walks up to two hundred lightnesses: once per face, category and use, then
            // remembered -- a theme has a handful of faces, a heat map a few hundred at most.
            static std::map<std::tuple<ImU32, CommandCategory, CategoryUse, const Palette*>, std::optional<ImU32>> s_Solved;

            const Palette& palette = Current();
            const ImU32    face    = Fill(fill, heat).face;
            const auto     key     = std::make_tuple(face, category, use, &palette);

            const auto found = s_Solved.find(key);
            if (found != s_Solved.end())
                return found->second;
            return s_Solved[key] = CategoryColour(palette, face, category, use);
        }

        ImU32 EdgeLabel(uint8_t marks)
        {
            const Palette& t = Current();
            if ((marks & Mark::Highlighted) != 0)
                return t.highlighted;
            if ((marks & Mark::HighlightedSecond) != 0)
                return t.second;
            if ((marks & Mark::Checked) != 0)
                return t.checked;
            if ((marks & (Mark::Dimmed | Mark::Struck)) != 0)
                return ImGui::GetColorU32(ImGuiCol_TextDisabled);
            return ImGui::GetColorU32(ImGuiCol_Text);
        }

        ImU32 Line(uint8_t marks)
        {
            // Lit lines let the keycaps under them show through.
            const Palette& t     = Current();
            const ImU32  alpha = ~IM_COL32_A_MASK;
            if ((marks & Mark::Highlighted) != 0)
                return (t.highlighted & alpha) | IM_COL32(0, 0, 0, 190);
            if ((marks & Mark::HighlightedSecond) != 0)
                return (t.second & alpha) | IM_COL32(0, 0, 0, 190);
            if ((marks & Mark::Dimmed) != 0)
                return (t.second & alpha) | IM_COL32(0, 0, 0, 50);
            return (t.second & alpha) | IM_COL32(0, 0, 0, 170);
        }

        ImU32 Plate() { return Current().plate; }
        ImU32 Lip() { return Current().lip; }
        ImU32 Outline() { return Current().outline; }
        ImU32 GroupLabel() { return Current().groupLabel; }
        ImU32 GroupLabelText() { return Current().groupLabelText; }
        ImU32 Hovered() { return Current().hovered; }
        ImU32 Pressed() { return Current().pressed; }
        ImU32 Dimmed() { return Current().dimmed; }
        ImU32 HighlightedTint() { return Current().highlightedTint; }
        ImU32 HighlightedSecondTint() { return Current().secondTint; }
        ImU32 CheckedTint() { return Current().checkedTint; }
        ImU32 Selected() { return Current().text; }
        ImU32 Highlighted() { return Current().highlighted; }
        ImU32 HighlightedSecond() { return Current().second; }
        ImU32 Warning() { return Current().warning; }
    }
}
