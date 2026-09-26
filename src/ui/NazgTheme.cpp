// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgTheme.h"

#include <algorithm>
#include <cstdarg>

namespace nazg
{
    ImVec4 ColourOf(PanelColour colour)
    {
        switch (colour)
        {
        case PanelColour::Error:   return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        case PanelColour::Warning: return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
        case PanelColour::Success: return ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
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
        ImU32 Fill(KeyFill fill, float heat)
        {
            constexpr ImVec4 c_Neutral(58 / 255.0f, 60 / 255.0f, 72 / 255.0f, 1.0f);
            constexpr ImVec4 c_Hot(45 / 255.0f, 45 / 255.0f, 1.0f, 1.0f);

            switch (fill)
            {
            case KeyFill::Neutral:
            case KeyFill::Alpha:    return ImGui::ColorConvertFloat4ToU32(c_Neutral);
            case KeyFill::Modifier: return IM_COL32(44, 46, 56, 255);
            case KeyFill::Accent:   return IM_COL32(96, 72, 44, 255);
            case KeyFill::Heat:     break;
            }

            const float t = std::clamp(heat, 0.0f, 1.0f);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c_Neutral.x + (c_Hot.x - c_Neutral.x) * t,
                                                         c_Neutral.y + (c_Hot.y - c_Neutral.y) * t,
                                                         c_Neutral.z + (c_Hot.z - c_Neutral.z) * t, 1.0f));
        }

        ImU32 Legend(LegendRole role)
        {
            switch (role)
            {
            case LegendRole::Label:     break;
            case LegendRole::Sublegend: return IM_COL32(170, 175, 190, 255);
            case LegendRole::Value:     return IM_COL32(150, 235, 150, 255);
            }
            return IM_COL32(235, 235, 240, 255);
        }

        ImU32 EdgeLabel(uint8_t marks)
        {
            if ((marks & Mark::Highlighted) != 0)
                return c_Highlighted;
            if ((marks & (Mark::Dimmed | Mark::Struck)) != 0)
                return ImGui::GetColorU32(ImGuiCol_TextDisabled);
            return ImGui::GetColorU32(ImGuiCol_Text);
        }

        ImU32 Line(uint8_t marks)
        {
            if ((marks & Mark::Highlighted) != 0)
                return IM_COL32(255, 130, 190, 255);
            if ((marks & Mark::Dimmed) != 0)
                return IM_COL32(230, 120, 170, 50);
            return IM_COL32(230, 120, 170, 170);
        }
    }
}
