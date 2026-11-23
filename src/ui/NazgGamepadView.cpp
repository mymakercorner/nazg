// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgGamepadView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // The mockup's measures, in its pixels at a 14 px line.
        constexpr float c_Line       = 14.0f;
        constexpr float c_Width      = 580.0f;
        constexpr float c_Height     = 112.0f;
        constexpr float c_ControlWidth  = 31.0f;
        constexpr float c_ControlHeight = 24.0f;
        constexpr float c_WideTile   = 54.0f;
        constexpr float c_MiddleTile = 44.0f;
        constexpr float c_TopRow     = 13.0f;   // the middle of the first row
        constexpr float c_Crosses    = 72.0f;   // of the second
        constexpr float c_CrossDown  = 26.0f;   // a cross's tiles from its middle
        constexpr float c_CrossRight = 34.0f;

        constexpr float c_ScaleLeast = 1.3f;
        constexpr float c_ScaleMost  = 2.0f;

        // The test's: what its sticks and triggers take, the numbers beside the sticks, or under them, and
        // how much they grow.
        constexpr float c_TestWidth     = 314.0f;
        constexpr float c_TestHeight    = 70.0f;
        constexpr float c_UnderWidth    = 208.0f;
        constexpr float c_UnderStick    = 76.0f;
        constexpr float c_UnderHeight   = 72.0f;
        constexpr float c_TestScaleMost = 2.4f;

        // How long the path of a stick shows, in seconds.
        constexpr double c_TrailTime = 0.5;

        struct Placed
        {
            uint8_t control;
            float   x;
            float   y;
            float   width;
        };

        // The shoulder and middle buttons, then the left stick, the D-pad, the right stick and A B X Y.
        std::vector<Placed> Layout()
        {
            std::vector<Placed> placed = {
                { 4, 48, c_TopRow, c_WideTile },    { 17, 108, c_TopRow, c_WideTile },   { 19, 230, c_TopRow, c_MiddleTile },
                { 27, 280, c_TopRow, c_MiddleTile }, { 20, 330, c_TopRow, c_MiddleTile }, { 18, 452, c_TopRow, c_WideTile },
                { 5, 512, c_TopRow, c_WideTile },
            };
            struct Cross
            {
                float                  x;
                uint8_t                up, down, left, right;
                std::optional<uint8_t> middle;
            };
            for (const Cross& cross : { Cross{ 78, 3, 2, 0, 1, uint8_t{ 21 } }, Cross{ 212, 23, 24, 25, 26, std::nullopt },
                                        Cross{ 348, 9, 8, 6, 7, uint8_t{ 22 } }, Cross{ 482, 16, 13, 15, 14, std::nullopt } })
            {
                placed.push_back({ cross.up, cross.x, c_Crosses - c_CrossDown, c_ControlWidth });
                placed.push_back({ cross.down, cross.x, c_Crosses + c_CrossDown, c_ControlWidth });
                placed.push_back({ cross.left, cross.x - c_CrossRight, c_Crosses, c_ControlWidth });
                placed.push_back({ cross.right, cross.x + c_CrossRight, c_Crosses, c_ControlWidth });
                if (cross.middle)
                    placed.push_back({ *cross.middle, cross.x, c_Crosses, c_ControlWidth });
            }
            return placed;
        }

        ImU32 BadgeColour()
        {
            return BoardColours::Category(CommandCategory::Behaviour, CategoryUse::Text, KeyFill::Alpha, 0.0f)
                .value_or(BoardColours::Legend(LegendInk::Legend, KeyFill::Alpha));
        }

        // Never -0.
        float Percent(float value)
        {
            const float percent = std::round(value * 100.0f);
            return percent == 0.0f ? 0.0f : percent;
        }

        ImVec2 StickOf(const keychron::GamepadLevels& levels, uint8_t left, uint8_t right, uint8_t down, uint8_t up)
        {
            return ImVec2(levels[right] - levels[left], levels[up] - levels[down]);   // y up
        }
    }

    float GamepadViewScale(float width, float height)
    {
        const float px = ImGui::GetFontSize() / c_Line;
        // Taller than the room, it scrolls; wider would be cut off, so a narrow column wins over the least.
        const float wide = width / (c_Width * px);
        return std::max(1.0f, std::min(wide, std::clamp(std::min(wide, height / (c_Height * px)), c_ScaleLeast, c_ScaleMost)));
    }

    ImVec2 DrawGamepadBadge(ImDrawList* list, ImVec2 at, const std::string& label, bool isFilled, bool isCentred, float scale)
    {
        ImFont*      font   = ImGui::GetFont();
        const float  size   = ImGui::GetFontSize() * 0.72f * scale;
        const ImVec2 text   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        const float  pad    = size * 0.36f;
        const ImVec2 badge(text.x + 2 * pad, size * 1.2f);
        const ImVec2 min(isCentred ? at.x - badge.x / 2 : at.x, at.y);
        const ImVec2 max(min.x + badge.x, min.y + badge.y);
        const ImU32  colour = BadgeColour();
        if (isFilled)
            list->AddRectFilled(min, max, colour, badge.y / 2);
        else
            list->AddRect(min, max, (colour & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 115), badge.y / 2);
        list->AddText(font, size, ImVec2(min.x + pad, min.y + (badge.y - text.y) / 2),
                      isFilled ? BoardColours::Fill(KeyFill::Alpha, 0.0f).face
                               : ImGui::GetColorU32(ColourOf(PanelColour::Muted)),
                      label.c_str());
        return badge;
    }

    void GamepadBadgeItem(const std::string& label, bool isFilled)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float  size  = ImGui::GetFontSize();
        const ImVec2 badge = DrawGamepadBadge(ImGui::GetWindowDrawList(),
                                              ImVec2(start.x, start.y + (size - size * 0.72f * 1.2f) / 2), label, isFilled);
        ImGui::Dummy(ImVec2(badge.x, size));
    }

    GamepadViewEvents DrawGamepadView(const GamepadViewInput& input)
    {
        GamepadViewEvents events;
        const float       px    = ImGui::GetFontSize() / c_Line * input.scale;
        const ImVec2      start = ImGui::GetCursorScreenPos();
        ImDrawList*       list  = ImGui::GetWindowDrawList();
        ImFont*           font  = ImGui::GetFont();

        const auto   keycap = BoardColours::Fill(KeyFill::Alpha, 0.0f);
        const float  radius = 5 * px;

        for (const Placed& tile : Layout())
        {
            const uint8_t control = tile.control;
            const ImVec2  size(tile.width * px, c_ControlHeight * px);
            const ImVec2  min(start.x + tile.x * px - size.x / 2, start.y + tile.y * px - size.y / 2);
            const ImVec2  max(min.x + size.x, min.y + size.y);
            const std::string& keys     = input.keys[control];
            const bool         isPlayed = !keys.empty();

            ImGui::PushID(control);
            ImGui::SetCursorScreenPos(min);
            if (ImGui::InvisibleButton("##control", size))
                events.clicked = control;
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
            {
                events.hovered = control;
                ImGui::SetTooltip("%s\n%s", keychron::GamepadControlWords(control).c_str(),
                                  isPlayed ? ("Played by " + keys).c_str() : "No key plays it");
            }
            ImGui::PopID();

            list->AddRectFilled(min, max, keycap.face, radius);
            if (input.levels && (*input.levels)[control] > 0.02f)
            {
                const float level = std::min(1.0f, (*input.levels)[control]);
                list->AddRectFilled(ImVec2(min.x + 1, max.y - 1 - (size.y - 2) * level), ImVec2(max.x - 1, max.y - 1),
                                    ImGui::GetColorU32(ImGuiCol_CheckMark, 0.5f), radius);
            }
            if (hovered)
                list->AddRectFilled(min, max, BoardColours::Hovered(), radius);
            if (input.selected == control)
                list->AddRect(min, max, BoardColours::Selected(), radius, 0, 2.5f * ImGui::GetFontSize() / c_Line);
            else
                list->AddRect(min, max, BoardColours::Outline(), radius);

            const ImVec2 badge = DrawGamepadBadge(list, ImVec2((min.x + max.x) / 2, min.y + 1.5f * px),
                                                  keychron::GamepadControlName(control), isPlayed, true, input.scale);
            if (isPlayed)
            {
                // The key's name under it, smaller where it is long.
                float  nameSize = ImGui::GetFontSize() * 0.74f * input.scale;
                ImVec2 text     = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, keys.c_str());
                if (text.x > size.x - 4 * px)
                {
                    nameSize = std::max(nameSize * 0.62f, nameSize * (size.x - 4 * px) / text.x);
                    text     = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, keys.c_str());
                }
                const float  top  = min.y + 1.5f * px + badge.y;
                const ImVec4 clip(min.x, min.y, max.x, max.y);
                list->AddText(font, nameSize, ImVec2(std::max(min.x + 2 * px, (min.x + max.x - text.x) / 2), (top + max.y - text.y) / 2),
                              keycap.legend, keys.c_str(), nullptr, 0.0f, &clip);
            }
        }

        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(c_Width * px, c_Height * px));
        return events;
    }

    void GamepadTrail::Add(const keychron::GamepadLevels& levels)
    {
        // Every reading, a stick at rest too, so that a path starts where the stick was when it moved.
        const double now = ImGui::GetTime();
        const auto   add = [&](std::vector<Point>& trail, ImVec2 at)
        {
            std::erase_if(trail, [&](const Point& point) { return now - point.time > c_TrailTime; });
            trail.push_back({ at, now });
        };
        add(left, StickOf(levels, 0, 1, 2, 3));
        add(right, StickOf(levels, 6, 7, 8, 9));
    }

    void GamepadTrail::Clear()
    {
        left.clear();
        right.clear();
    }

    float GamepadTestWidth()
    {
        return 320.0f * ImGui::GetFontSize() / c_Line;
    }

    void DrawGamepadTest(const GamepadTestInput& input)
    {
        if (!input.isSeen || !input.levels)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Warning, "The computer does not see this board as a controller.");
            ColouredText(PanelColour::Muted, "%s Games get nothing from its gamepad keys until then.", input.whyNotSeen.c_str());
            ImGui::PopTextWrapPos();
            return;
        }

        const keychron::GamepadLevels& levels = *input.levels;
        ColouredText(PanelColour::Muted, "The computer receives");
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.3f);
        ImGui::Text("a%s Controller", input.isXbox ? "n Xbox" : " Standard");
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "(?)");
        if (ImGui::IsItemHovered())
        {
            std::string tip = "This board's own controller, by its vendor and product numbers";
            if (input.others)
                tip += input.others == 1 ? ", not the other one plugged in" : ", not the " + std::to_string(input.others) + " others plugged in";
            tip += ".";
            if (input.sharesTriggers && !input.isXbox)
                tip += "\n\nAs a Standard Controller the two triggers share one axis: pressed together, only the deeper one "
                       "counts. As an Xbox Controller each has its own.";
            ImGui::SetTooltip("%s", tip.c_str());
        }

        // As large as its column and the height left allow (Rico, 2026-11-23), the buttons' line kept; with
        // the raw values above it, that is less.
        const float  unit   = ImGui::GetFontSize() / c_Line;
        const ImVec2 room   = ImGui::GetContentRegionAvail();
        const float  left   = room.y - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
        const float  beside = std::clamp(std::min(room.x / (c_TestWidth * unit), left / (c_TestHeight * unit)), 1.0f, c_TestScaleMost);

        // The numbers under the sticks where that makes them larger, and leaves the numbers their room.
        const float  lineH  = ImGui::GetTextLineHeight() * 1.25f;
        const float  under  = std::min(std::min(room.x / (c_UnderWidth * unit), (left - lineH) / (c_UnderHeight * unit)), c_TestScaleMost);
        const bool   below  = under > beside * 1.1f &&
                           c_UnderStick * unit * under > ImGui::CalcTextSize("X -100 %  Y -100 %").x + ImGui::GetFontSize() * 1.5f;
        const float  scale  = below ? under : beside;
        const float  px     = unit * scale;
        const float  words  = ImGui::GetFontSize() * std::min(scale, 1.5f);
        ImFont*      font   = ImGui::GetFont();
        ImDrawList*  list   = ImGui::GetWindowDrawList();
        const ImVec2 start  = ImGui::GetCursorScreenPos();
        const float  height = below ? c_UnderHeight * px + lineH : c_TestHeight * px;
        const ImU32  accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const ImU32  border = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32  face   = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImU32  text   = ImGui::GetColorU32(ImGuiCol_Text);

        float x = start.x;
        const auto trigger = [&](uint8_t control)
        {
            const float level = std::clamp(levels[control], 0.0f, 1.0f);
            const ImVec2 min(x + 5 * px, start.y + 2 * px), max(x + 17 * px, start.y + 48 * px);
            list->AddRectFilled(min, max, face, 4 * px);
            if (level > 0.01f)
                list->AddRectFilled(ImVec2(min.x + 1, max.y - 1 - (max.y - min.y - 2) * level), ImVec2(max.x - 1, max.y - 1),
                                    ImGui::GetColorU32(ImGuiCol_CheckMark, 0.65f), 3 * px);
            list->AddRect(min, max, border, 4 * px);
            DrawGamepadBadge(list, ImVec2(x + 11 * px, start.y + 52 * px), keychron::GamepadControlName(control), true, true,
                             std::min(scale, 1.5f));
            if (ImGui::IsMouseHoveringRect(ImVec2(x, start.y), ImVec2(x + 22 * px, start.y + height)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s, %.0f %%", keychron::GamepadControlWords(control).c_str(), Percent(level));
            x += 28 * px;
        };
        const auto stick = [&](const char* label, ImVec2 at, const std::vector<GamepadTrail::Point>* trail)
        {
            const float  r = 29 * px;
            const ImVec2 centre(x + r + (below ? 9.0f : 6.0f) * px, start.y + r + 6 * px);
            const auto   point = [&](ImVec2 p) { return ImVec2(centre.x + p.x * r, centre.y - p.y * r); };
            list->AddCircleFilled(centre, r, face);
            list->AddCircle(centre, r, border);
            list->AddLine(ImVec2(centre.x - r, centre.y), ImVec2(centre.x + r, centre.y), border);
            list->AddLine(ImVec2(centre.x, centre.y - r), ImVec2(centre.x, centre.y + r), border);
            if (trail)
                for (size_t index = 1; index < trail->size(); ++index)
                {
                    const ImVec2 from = (*trail)[index - 1].at, to = (*trail)[index].at;
                    const double age  = ImGui::GetTime() - (*trail)[index].time;
                    if (age >= c_TrailTime || (from.x == to.x && from.y == to.y))
                        continue;
                    list->AddLine(point(from), point(to),
                                  ImGui::GetColorU32(ImGuiCol_CheckMark, 0.45f * static_cast<float>(1.0 - age / c_TrailTime)),
                                  2 * px);
                }
            list->AddCircleFilled(point(at), 5 * px, accent);
            if (below)
            {
                char line[48];
                std::snprintf(line, sizeof(line), "X %.0f %%  Y %.0f %%", Percent(at.x), Percent(at.y));
                // Its badge with its numbers, off the way of the stick.
                const ImVec2 size    = ImGui::CalcTextSize(line);
                const float  lineY   = start.y + c_UnderHeight * px;
                const float  space   = ImGui::GetFontSize() * 0.4f;
                const float  badgeW  = ImGui::GetFontSize() * 1.1f;
                const float  startX  = centre.x - (badgeW + space + size.x) / 2;
                DrawGamepadBadge(list, ImVec2(startX, lineY + ImGui::GetFontSize() * 0.07f), label, true);
                list->AddText(ImVec2(startX + badgeW + space, lineY), text, line);
                x += c_UnderStick * px;
                return;
            }
            const float tx = centre.x + r + 8 * px;
            DrawGamepadBadge(list, ImVec2(tx, start.y + 4 * px), label, true, false, std::min(scale, 1.5f));
            char line[32];
            std::snprintf(line, sizeof(line), "X %.0f %%", Percent(at.x));
            list->AddText(font, words, ImVec2(tx, start.y + 22 * px), text, line);
            std::snprintf(line, sizeof(line), "Y %.0f %%", Percent(at.y));
            list->AddText(font, words, ImVec2(tx, start.y + 39 * px), text, line);
            x = tx + 56 * px;
        };
        trigger(keychron::c_GamepadLeftTrigger);
        stick("L", StickOf(levels, 0, 1, 2, 3), input.trail ? &input.trail->left : nullptr);
        stick("R", StickOf(levels, 6, 7, 8, 9), input.trail ? &input.trail->right : nullptr);
        trigger(keychron::c_GamepadRightTrigger);
        ImGui::Dummy(ImVec2(x - start.x, height));

        ColouredText(PanelColour::Muted, "Buttons");
        bool any = false;
        for (uint8_t control = keychron::c_GamepadFirstButton; control < keychron::c_GamepadControlCount; ++control)
            if (levels[control] > 0.5f)
            {
                any = true;
                ImGui::SameLine();
                GamepadBadgeItem(keychron::GamepadControlName(control));
            }
        if (!any)
        {
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "none");
        }
    }
}
