// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgTheme.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>

namespace nazg
{
    namespace
    {
        // 0xRRGGBB, and how opaque.
        constexpr ImU32 Hex(uint32_t rgb, float alpha = 1.0f)
        {
            return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(alpha * 255.0f + 0.5f));
        }

        struct Theme
        {
            // The window.
            ImU32 window, menubar, text, muted, control, border, accent, accentText;

            // The board.
            ImU32                 plate;
            BoardColours::Keycap  alpha, modifier, accentCap;
            ImU32                 lip;   // Bottom lip: one shadow under every key, whatever its class
            ImU32                 outline;
            ImU32                 highlighted, highlightedTint, second, secondTint, checked, checkedTint, warning;
            ImU32                 pressed, hovered, dimmed;

            // The panels' named colours.
            ImU32 error, warningText, success;
        };

        // Every theme keeps the keycap's three tones in one order of lightness: the faces of the
        // alphas and modifiers lighter than the plate, the lip darker than it, each at least 0.04
        // apart in OKLab -- else a cap and the plate merge, or the lip vanishes into the plate
        // (Rico, 2026-10-03: Light's modifiers had the plate's colour). Accent caps stand apart by
        // their hue. The lip is one colour for every key, the alphas' (Rico, 2026-10-03: a shade
        // of each cap's own colour was disturbing, on Dark most) -- on Light a little darker, to
        // stay below the slate accent face too.
        //
        // The values are the mockup's, ui-design/board-look.html. Light and Dark share their
        // state hues, Dark's lighter; Dracula, Rico's own, takes the shared ones too. The
        // selection is the theme's text colour -- no hue, so it meets no category, highlight or
        // warning colour. The accent caps are slate on Light, a lifted slate on Dark.
        constexpr Theme c_Light{
            Hex(0xf3f3f5), Hex(0xe3e4e9), Hex(0x1d1e22), Hex(0x6b6e78), Hex(0xffffff), Hex(0xc8cad2), Hex(0x3b82f6),
            Hex(0xffffff),

            Hex(0xd3d4d8),
            { Hex(0xffffff), Hex(0x24262b) },
            { Hex(0xe4e5ea), Hex(0x24262b) },
            { Hex(0xafbfd5), Hex(0x18222f) },
            Hex(0xacafb7),
            Hex(0xb4b7c2),
            Hex(0x1c7ed6), Hex(0x1c7ed6, 0.22f), Hex(0xbf308f), Hex(0xbf308f, 0.20f), Hex(0x2f9e44), Hex(0x2f9e44, 0.32f),
            Hex(0xe03131),
            Hex(0x1c64f2, 0.50f), Hex(0x000000, 0.08f), Hex(0xf3f3f5, 0.72f),

            Hex(0xe03131), Hex(0xe8590c), Hex(0x2f9e44),
        };

        constexpr Theme c_Dark{
            Hex(0x0f0f11), Hex(0x24252b), Hex(0xececf1), Hex(0x80838f), Hex(0x20232b), Hex(0x3a3d48), Hex(0x4296fa),
            Hex(0xffffff),

            Hex(0x1c1d23),
            { Hex(0x3a3c48), Hex(0xebebf0) },
            { Hex(0x2c2e38), Hex(0xebebf0) },
            { Hex(0x5a6a80), Hex(0xe8eff9) },
            Hex(0x12131b),
            Hex(0x4b4e5c),
            Hex(0x71b6ff), Hex(0x71b6ff, 0.27f), Hex(0xf387c7), Hex(0xf387c7, 0.27f), Hex(0x78dc82), Hex(0x5ac86e, 0.43f),
            Hex(0xff8b7f),
            Hex(0x3c6eff, 0.67f), Hex(0xffffff, 0.13f), Hex(0x0c0c10, 0.75f),

            Hex(0xff8b7f), Hex(0xffb34d), Hex(0x78dc82),
        };

        constexpr Theme c_Dracula{
            Hex(0x282a36), Hex(0x21222c), Hex(0xf8f8f2), Hex(0x6272a4), Hex(0x343746), Hex(0x44475a), Hex(0xbd93f9),
            Hex(0x282a36),

            Hex(0x21222c),
            { Hex(0x44475a), Hex(0xf8f8f2) },
            { Hex(0x373949), Hex(0xf8f8f2) },
            { Hex(0xbd93f9), Hex(0x282a36) },
            Hex(0x151725),
            Hex(0x6272a4),
            Hex(0x71b6ff), Hex(0x71b6ff, 0.25f), Hex(0xf387c7), Hex(0xf387c7, 0.25f), Hex(0x50fa7b), Hex(0x50fa7b, 0.33f),
            Hex(0xff8b7f),
            Hex(0xf1fa8c, 0.45f), Hex(0xffffff, 0.10f), Hex(0x282a36, 0.75f),

            Hex(0xff5555), Hex(0xffb86c), Hex(0x50fa7b),
        };

        constexpr std::array<ThemeId, 3>     c_Themes{ ThemeId::Light, ThemeId::Dark, ThemeId::Dracula };
        constexpr std::array<KeycapStyle, 2> c_KeycapStyles{ KeycapStyle::Outlined, KeycapStyle::BottomLip };
        constexpr std::array<LegendFamily, 2> c_LegendFamilies{ LegendFamily::Cylindrical, LegendFamily::Spherical };

        BoardStyle g_Style;

        const Theme& Current()
        {
            switch (g_Style.theme)
            {
            case ThemeId::Light:   return c_Light;
            case ThemeId::Dark:    break;
            case ThemeId::Dracula: return c_Dracula;
            }
            return c_Dark;
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

        const Theme& t      = theme == ThemeId::Light ? c_Light : theme == ThemeId::Dracula ? c_Dracula : c_Dark;
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
        const Theme& t = Current();
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
            const Theme& t = Current();
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

        ImU32 Legend(LegendRole role, KeyFill fill)
        {
            if (role == LegendRole::Value)
                return Current().checked;
            return Fill(fill, 0.0f).legend;
        }

        ImU32 EdgeLabel(uint8_t marks)
        {
            const Theme& t = Current();
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
            const Theme& t     = Current();
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
