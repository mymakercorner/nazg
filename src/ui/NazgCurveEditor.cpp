// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgCurveEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // The mockup's measures, in its pixels at a 14 px line: the drawing, the room its scales take, what
        // stands beside it; and how much larger the drawing grows with the panel.
        constexpr float c_Line        = 14.0f;
        constexpr float c_Width       = 320.0f;
        constexpr float c_Height      = 128.0f;
        constexpr float c_Left        = 44.0f;
        constexpr float c_Top         = 9.0f;
        constexpr float c_Right       = 12.0f;
        constexpr float c_Bottom      = 18.0f;
        constexpr float c_Apart       = 18.0f;
        constexpr float c_SideWidth   = 176.0f;
        constexpr float c_CardPicture = 28.0f;
        constexpr float c_CardWidth   = 66.0f;
        constexpr float c_FieldWidth  = 40.0f;
        constexpr float c_ScaleLeast  = 0.6f;
        constexpr float c_ScaleMost   = 1.8f;

        std::string Millimetres(int tenths)
        {
            return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        }

        int Percent(int output) { return static_cast<int>(std::lround(output * 100.0 / keychron::c_CurveOutputMax)); }
        int Output(double percent) { return static_cast<int>(std::lround(percent * keychron::c_CurveOutputMax / 100.0)); }

        // The gamepad's colour on this section, as its badges.
        ImU32 Violet()
        {
            return BoardColours::Category(CommandCategory::Behaviour, CategoryUse::Text, KeyFill::Alpha, 0.0f)
                .value_or(ImGui::GetColorU32(ImGuiCol_CheckMark));
        }

        ImU32 Faint(ImU32 colour, float alpha)
        {
            ImVec4 faint = ImGui::ColorConvertU32ToFloat4(colour);
            faint.w *= alpha;
            return ImGui::ColorConvertFloat4ToU32(faint);
        }

        // The wheel over the last item, taken from the panel: +1 up, -1 down.
        int WheelOverItem()
        {
            if (!ImGui::IsItemHovered())
                return 0;
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const float wheel = ImGui::GetIO().MouseWheel;
            return wheel > 0.0f ? 1 : wheel < 0.0f ? -1 : 0;
        }

        struct Preset
        {
            const char*                       name;
            std::vector<keychron::CurvePoint> curve;
        };

        // A preset: its curve and its name.
        bool DrawPreset(const Preset& preset, bool isOn, uint8_t full, float px)
        {
            const ImGuiStyle& style  = ImGui::GetStyle();
            const float       height = c_CardPicture * px + ImGui::GetFontSize() + 8 * px;
            const bool        clicked = ImGui::InvisibleButton(preset.name, ImVec2(c_CardWidth * px, height));
            const bool        hovered = ImGui::IsItemHovered();
            const ImVec2      min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            const ImU32       ink  = ImGui::GetColorU32(ImGuiCol_Text);
            ImDrawList*       list = ImGui::GetWindowDrawList();

            list->AddRectFilled(min, max, ImGui::GetColorU32(isOn ? ImGuiCol_Header : ImGuiCol_FrameBg), style.FrameRounding);
            list->AddRect(min, max, ImGui::GetColorU32(isOn || hovered ? ImGuiCol_CheckMark : ImGuiCol_Border),
                          style.FrameRounding, 0, isOn ? 2 * px : 1.0f);
            const ImVec2 from(min.x + 9 * px, min.y + 5 * px), to(max.x - 9 * px, min.y + (3 + c_CardPicture) * px);
            ImVec2       line[keychron::c_CurvePoints];
            for (size_t point = 0; point < std::size(line); ++point)
                line[point] = ImVec2(from.x + (to.x - from.x) * preset.curve[point].x / full,
                                     to.y - (to.y - from.y) * preset.curve[point].y / keychron::c_CurveOutputMax);
            list->AddPolyline(line, static_cast<int>(std::size(line)), ink, ImDrawFlags_None, 1.8f * px);
            const ImVec2 size = ImGui::CalcTextSize(preset.name);
            list->AddText(ImVec2((min.x + max.x - size.x) / 2, max.y - size.y - 3 * px), ink, preset.name);
            return clicked;
        }
    }

    std::optional<std::vector<keychron::CurvePoint>> DrawCurveEditor(CurveEditorState& state, const CurveEditorInput& input)
    {
        std::optional<std::vector<keychron::CurvePoint>> chosen;
        if (input.curve.size() != keychron::c_CurvePoints)
            return chosen;

        const std::vector<keychron::CurvePoint>& curve = state.draggedPoint ? state.dragged : input.curve;
        const uint8_t full  = std::max<uint8_t>({ input.fullTravel, curve.back().x, uint8_t{ 1 } });
        const float   px    = ImGui::GetFontSize() / c_Line;
        const ImVec2  room  = ImGui::GetContentRegionAvail();
        const float   frame = (c_Left + c_Right) * px;
        // As large as the room: shorter, the panel scrolls; narrower, the drawing gives way to what stands beside.
        const float   wide  = (room.x - frame - (c_Apart + c_SideWidth) * px) / (c_Width * px);
        const float   tall  = std::max(1.0f, (room.y - (c_Top + c_Bottom) * px) / (c_Height * px));
        const float   scale = std::clamp(std::min(wide, tall), c_ScaleLeast, c_ScaleMost);
        const float   width = c_Width * px * scale, height = c_Height * px * scale;

        ImDrawList*  list  = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 min(start.x + c_Left * px, start.y + c_Top * px), max(min.x + width, min.y + height);
        const auto   x = [&](float tenths) { return min.x + width * tenths / full; };
        const auto   y = [&](float output) { return max.y - height * output / keychron::c_CurveOutputMax; };

        const ImU32 muted  = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
        const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const ImU32 violet = Violet();
        ImFont*     font   = ImGui::GetFont();
        const float small  = ImGui::GetFontSize() * 0.8f;

        // The sheet: quarters up, millimetres across.
        list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
        for (int quarter = 1; quarter < 4; ++quarter)
            list->AddLine(ImVec2(min.x, min.y + height * quarter / 4), ImVec2(max.x, min.y + height * quarter / 4),
                          Faint(border, 0.6f));
        const auto label = [&](float at, float top, float align, const std::string& words)
        {
            const ImVec2 size = font->CalcTextSizeA(small, FLT_MAX, 0.0f, words.c_str());
            list->AddText(font, small, ImVec2(at - size.x * align, top), muted, words.c_str());
        };
        for (int tenths = 0; tenths <= full; tenths += 10)
        {
            if (tenths > 0 && tenths < full)
                list->AddLine(ImVec2(x(static_cast<float>(tenths)), min.y), ImVec2(x(static_cast<float>(tenths)), max.y),
                              Faint(border, 0.6f));
            if (full - tenths >= 3)
                label(x(static_cast<float>(tenths)), max.y + 4 * px, 0.5f, std::to_string(tenths / 10));
        }
        label(max.x + c_Right * px, max.y + 4 * px, 1.0f, Millimetres(full) + " mm");
        label(min.x - 9 * px, min.y, 1.0f, "100 %");
        label(min.x - 9 * px, max.y - small, 1.0f, "0");
        list->AddRect(min, max, border);

        // The curve: nothing under its first point, the step up to it where there is one, and its last
        // output to the bottom.
        std::vector<ImVec2> path{ ImVec2(x(0), y(0)) };
        const auto          to = [&](float tenths, float output)
        {
            const ImVec2 next(x(tenths), y(output));
            if (next.x != path.back().x || next.y != path.back().y)
                path.push_back(next);
        };
        to(curve.front().x, 0);
        for (const keychron::CurvePoint& point : curve)
            to(point.x, point.y);
        to(full, curve.back().y);
        for (size_t part = 1; part < path.size(); ++part)
            if (path[part].x > path[part - 1].x)
                list->AddQuadFilled(ImVec2(path[part - 1].x, max.y), path[part - 1], path[part], ImVec2(path[part].x, max.y),
                                    Faint(violet, 0.14f));
        list->AddPolyline(path.data(), static_cast<int>(path.size()), violet, ImDrawFlags_None, 2 * px);

        // The key tested: where it is, what its axis gets.
        if (input.travel && *input.travel > 0.0f)
        {
            const float  tenths = std::min(*input.travel, static_cast<float>(full));
            const ImVec2 at(x(tenths), y(keychron::CurveOutput(curve, tenths)));
            for (float down = at.y; down < max.y; down += 5 * px)
                list->AddLine(ImVec2(at.x, down), ImVec2(at.x, std::min(down + 2 * px, max.y)), accent);
            for (float left = min.x; left < at.x; left += 5 * px)
                list->AddLine(ImVec2(left, at.y), ImVec2(std::min(left + 2 * px, at.x), at.y), accent);
            // Above and left of it, where the curve is not; further left under the top.
            const ImVec2 size  = ImGui::CalcTextSize(input.key.c_str());
            const float  above = at.y - size.y - 4 * px;
            const float  apart = above < min.y + 2 * px ? 30 * px : 9 * px;
            list->AddText(ImVec2(std::max(min.x + 4 * px, at.x - apart - size.x), std::max(min.y + 2 * px, above)), accent,
                          input.key.c_str());
            list->AddCircleFilled(at, 5 * px, accent);
        }

        // The points, dragged.
        for (size_t point = 0; point < curve.size(); ++point)
        {
            ImGui::PushID(static_cast<int>(point));
            const ImVec2 at(x(curve[point].x), y(curve[point].y));
            const float  grab = 9 * px;
            ImGui::SetCursorScreenPos(ImVec2(at.x - grab, at.y - grab));
            ImGui::InvisibleButton("##point", ImVec2(2 * grab, 2 * grab));
            const bool active  = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered() || active;
            const bool letGo   = ImGui::IsItemDeactivated() && state.draggedPoint == point;
            if (ImGui::IsItemActivated())
            {
                state.draggedPoint = point;
                state.dragged      = input.curve;
            }
            if (active && state.draggedPoint == point && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2 * px))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                state.dragged      = keychron::WithCurvePoint(
                    input.curve, point, static_cast<int>(std::lround((mouse.x - min.x) / width * full)),
                    static_cast<int>(std::lround((max.y - mouse.y) / height * keychron::c_CurveOutputMax)));
            }
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            list->AddCircleFilled(at, 6.5f * px, ImGui::GetColorU32(ImGuiCol_WindowBg));
            list->AddCircle(at, 6.5f * px, violet, 0, (hovered ? 3.5f : 2.5f) * px);
            if (letGo)
            {
                if (state.dragged != input.curve)
                    chosen = state.dragged;
                state.draggedPoint.reset();
            }
            ImGui::PopID();
        }

        // Beside it: the presets, then the points' values.
        const float left = max.x + (c_Right + c_Apart) * px;
        const Preset presets[] = { { "Linear", keychron::LinearCurve(full) }, { "Smooth", keychron::SmoothCurve(full) } };
        float        cardLeft  = left;
        for (const Preset& preset : presets)
        {
            const bool isOn = curve == preset.curve;
            ImGui::SetCursorScreenPos(ImVec2(cardLeft, min.y));
            if (DrawPreset(preset, isOn, full, px) && !isOn)
                chosen = preset.curve;
            cardLeft += (c_CardWidth + 6.0f) * px;
        }
        const float  radius = ImGui::GetFontSize() * 0.55f;
        const ImVec2 help(cardLeft + radius + 2 * px, min.y + c_CardPicture * px * 0.7f);
        const ImVec2 mark = ImGui::CalcTextSize("?");
        list->AddCircle(help, radius, border);
        list->AddText(ImVec2(help.x - mark.x / 2, help.y - mark.y / 2), muted, "?");
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(ImVec2(help.x - radius, help.y - radius), ImVec2(help.x + radius, help.y + radius)))
            ImGui::SetTooltip("This setting is global for the whole board: the same in every profile.\n\n"
                              "Drag a point. Under the first one the stick stays still: a dead zone.\n\nFor the sticks, and a "
                              "Standard Controller's triggers; an Xbox Controller's triggers do not use it.");

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3 * px, 1 * px));
        float rowTop = min.y + c_CardPicture * px + ImGui::GetFontSize() + 16 * px;
        for (size_t point = 0; point < curve.size(); ++point)
        {
            ImGui::PushID(static_cast<int>(point) + 10);
            for (int axis = 0; axis < 2; ++axis)
            {
                ImGui::PushID(axis);
                const int   value = axis == 0 ? curve[point].x : Percent(curve[point].y);
                const bool  fixed = axis == 0 && point + 1 == curve.size();
                char*       text  = state.typed[point][axis];
                const ImGuiID id  = ImGui::GetID("##value");
                if (ImGui::GetActiveID() != id)
                    std::snprintf(text, sizeof(state.typed[point][axis]), "%s",
                                  axis == 0 ? Millimetres(value).c_str() : std::to_string(value).c_str());
                const float fieldLeft = left + axis * (c_FieldWidth * px + ImGui::CalcTextSize("mm").x + 16 * px);
                ImGui::SetCursorScreenPos(ImVec2(fieldLeft, rowTop));
                ImGui::BeginDisabled(fixed);
                ImGui::SetNextItemWidth(c_FieldWidth * px);
                ImGui::InputText("##value", text, sizeof(state.typed[point][axis]),
                                 ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::EndDisabled();
                const bool typed = ImGui::IsItemDeactivatedAfterEdit();
                const int  step  = fixed ? 0 : WheelOverItem();
                if (fixed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("The bottom: it stays there.");

                std::optional<double> wanted;
                if (typed)
                {
                    std::string number = text;
                    std::replace(number.begin(), number.end(), ',', '.');
                    char*       end    = nullptr;
                    const float parsed = std::strtof(number.c_str(), &end);
                    if (end != number.c_str() && std::isfinite(parsed))
                        wanted = std::clamp(parsed, 0.0f, 100.0f);
                }
                else if (step != 0)
                {
                    wanted = axis == 0 ? (value + step) / 10.0 : value + step;
                }
                if (wanted)
                {
                    std::vector<keychron::CurvePoint> changed = keychron::WithCurvePoint(
                        input.curve, point, axis == 0 ? static_cast<int>(std::lround(*wanted * 10.0)) : input.curve[point].x,
                        axis == 0 ? input.curve[point].y : Output(*wanted));
                    if (changed != input.curve)
                        chosen = std::move(changed);
                }
                list->AddText(ImVec2(fieldLeft + (c_FieldWidth + 4.0f) * px, rowTop + 1 * px), muted, axis == 0 ? "mm" : "%");
                ImGui::PopID();
            }
            rowTop += ImGui::GetFrameHeight() + 3 * px;
            ImGui::PopID();
        }
        ImGui::PopStyleVar();

        // The room it took.
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(left + c_SideWidth * px - start.x, std::max(max.y + c_Bottom * px, rowTop) - start.y));
        return chosen;
    }
}
