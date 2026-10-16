// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLineParts.h"

#include <cmath>
#include <initializer_list>

#include "imgui.h"

namespace nazg
{
    namespace
    {
        ImU32 HueColour(float hue, float saturation = 1.0f)
        {
            float r = 0, g = 0, b = 0;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, 1.0f, r, g, b);
            return ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
        }
    }

    float BeginLine(const char* name)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const float top = ImGui::GetCursorScreenPos().y;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::TableSetColumnIndex(1);
        return top;
    }

    bool IsOverLine(float top)
    {
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            return false;
        const float y = ImGui::GetMousePos().y;
        return y >= top && y < ImGui::GetItemRectMax().y + ImGui::GetStyle().CellPadding.y;
    }

    bool ColourSlider(const char* id, int& value, float width, bool hueTrack, uint8_t hue, bool& letGo)
    {
        // The band under the slider's grab, the frame left clear over it.
        const float  height = ImGui::GetFrameHeight();
        const ImVec2 at     = ImGui::GetCursorScreenPos();
        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const float  inset  = std::floor(height * 0.3f);
        const ImVec2 a(at.x + 2.0f, at.y + inset);
        const ImVec2 b(at.x + width - 2.0f, at.y + height - inset);
        if (hueTrack)
            for (int part = 0; part < 6; ++part)
            {
                const float x0 = a.x + (b.x - a.x) * part / 6.0f, x1 = a.x + (b.x - a.x) * (part + 1) / 6.0f;
                const ImU32 c0 = HueColour(part / 6.0f), c1 = HueColour((part + 1) / 6.0f);
                draw->AddRectFilledMultiColor(ImVec2(x0, a.y), ImVec2(x1, b.y), c0, c1, c1, c0);
            }
        else
        {
            const ImU32 white = HueColour(0.0f, 0.0f), full = HueColour(hue / 255.0f);
            draw->AddRectFilledMultiColor(a, b, white, full, full, white);
        }

        for (ImGuiCol colour : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive })
            ImGui::PushStyleColor(colour, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(width);
        const bool moved = ImGui::SliderInt(id, &value, 0, 255, "", ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopStyleColor(3);
        letGo = ImGui::IsItemDeactivatedAfterEdit();
        return moved;
    }
}
