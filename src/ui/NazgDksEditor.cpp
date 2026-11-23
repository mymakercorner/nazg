// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDksEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // The mockup's measures, in its pixels at a 14 px line: the columns of the four points, the
        // line's ends and its bottom, the travel's top and height, a command's row and its key.
        constexpr float c_Line         = 14.0f;
        constexpr float c_Columns[]    = { 176.0f, 288.0f, 400.0f, 512.0f };
        constexpr float c_LineStart    = 112.0f;
        constexpr float c_LineBottom   = 344.0f;
        constexpr float c_LineEnd      = 576.0f;
        constexpr float c_Width        = 600.0f;
        constexpr float c_TravelTop    = 34.0f;
        constexpr float c_TravelHeight = 50.0f;
        constexpr float c_RowsGap      = 20.0f;
        constexpr float c_Row          = 24.0f;
        constexpr float c_KeyWidth     = 92.0f;
        constexpr float c_FieldWidth   = 34.0f;

        constexpr const char* c_PointNames[] = { "Starting Trigger", "Bottom In", "Bottom Out", "Bounce Back" };
        constexpr const char* c_PointTips[]  = { "Going down past it.", "Going down further, past it.",
                                                 "Coming back up above it, after Bottom In.",
                                                 "Coming back up above it, after Bottom Out." };

        std::string Millimetres(int tenths)
        {
            return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        }

        uint8_t Bounded(size_t point, int depth, uint8_t full)
        {
            const int least = point == 0 ? keychron::c_DksShallowestStart : 1;
            return static_cast<uint8_t>(std::clamp(depth, least, std::max<int>(full, least)));
        }

        // The wheel over the last item, taken from the panel: +1 deeper, -1 shallower, up being shallower
        // as the point moves.
        int WheelOverItem()
        {
            if (!ImGui::IsItemHovered())
                return 0;
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const float wheel = ImGui::GetIO().MouseWheel;
            return wheel > 0.0f ? -1 : wheel < 0.0f ? 1 : 0;
        }

        // A row's bars with `from` dragged to `to`: a tap or a hold removed, added, or one of its ends
        // moved. What the new bar covers goes; one it only touches stays.
        std::vector<keychron::DksBar> WithDrag(std::vector<keychron::DksBar> bars, const DksEditorState::Lane& lane)
        {
            if (lane.from == lane.to)
            {
                const auto hit = std::find_if(bars.begin(), bars.end(), [&](const keychron::DksBar& bar)
                                              { return lane.from >= bar.from && lane.from <= bar.to; });
                if (hit != bars.end())
                    bars.erase(hit);
                else
                    bars.push_back({ lane.from, lane.from });
            }
            else
            {
                const keychron::DksBar added{ std::min(lane.anchor, lane.to), std::max(lane.anchor, lane.to) };
                std::erase_if(bars, [&](const keychron::DksBar& bar)
                              {
                                  if (lane.bar && bar == *lane.bar)
                                      return true;
                                  const bool apart   = bar.to < added.from || bar.from > added.to;
                                  const bool touches = added.from != added.to && bar.from != bar.to &&
                                                       (bar.to == added.from || bar.from == added.to);
                                  return !apart && !touches;
                              });
                bars.push_back(added);
            }
            std::sort(bars.begin(), bars.end(),
                      [](const keychron::DksBar& a, const keychron::DksBar& b) { return a.from < b.from; });
            return bars;
        }
    }

    std::string DksWarning(const keychron::DksSlot& slot, uint8_t fullTravel)
    {
        const int start  = keychron::DksDepth(slot, keychron::DksPoint::StartingTrigger);
        const int in     = keychron::DksDepth(slot, keychron::DksPoint::BottomIn);
        const int out    = keychron::DksDepth(slot, keychron::DksPoint::BottomOut);
        const int bounce = keychron::DksDepth(slot, keychron::DksPoint::BounceBack);

        std::string warning;
        const auto  add = [&](const std::string& text) { warning += (warning.empty() ? "" : " ") + text; };
        if (start < keychron::c_DksShallowestStart)
            add("At " + Millimetres(start) + " mm, Starting Trigger never lets the key go.");
        if (in <= start)
            add("Bottom In is not below Starting Trigger: both play at once.");
        if (out >= fullTravel)
            add("Bottom Out is at full travel: the key never goes past it, so Bottom Out and Bounce Back never play.");
        else if (out > in)
            add("Bottom Out is deeper than Bottom In: it plays only if the key went past " + Millimetres(out) + " mm.");
        if (bounce > start)
            add("Bounce Back counts only above Starting Trigger: from " + Millimetres(std::max(0, start - 1)) + " mm.");
        return warning;
    }

    DksEditorEvents DrawDksEditor(DksEditorState& state, const DksEditorInput& input)
    {
        DksEditorEvents          events;
        const keychron::DksSlot& slot  = state.dragged ? *state.dragged : input.slot;
        const uint8_t            full  = input.fullTravel ? input.fullTravel : uint8_t{ 40 };
        const ImGuiStyle&        style = ImGui::GetStyle();
        const float              px    = ImGui::GetFontSize() / c_Line;
        ImDrawList*              list  = ImGui::GetWindowDrawList();
        const ImVec2             start = ImGui::GetCursorScreenPos();

        // The commands in use, and the first free one to add a key to.
        std::vector<size_t>   rows;
        std::optional<size_t> adding;
        for (size_t command = 0; command < keychron::c_DksCommands; ++command)
        {
            if (slot.keycodes[command])
                rows.push_back(command);
            else if (!adding)
            {
                adding = command;
                rows.push_back(command);
            }
        }

        const float height = (c_TravelTop + c_TravelHeight + c_RowsGap + c_Row * static_cast<float>(rows.size())) * px;
        const auto  x      = [&](float at) { return start.x + at * px; };
        const auto  y      = [&](float at) { return start.y + at * px; };
        const auto  depthY = [&](float tenths) { return y(c_TravelTop + c_TravelHeight * tenths / full); };

        const ImU32 text    = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 muted   = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
        const ImU32 border  = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 accent  = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const ImU32 surface = ImGui::GetColorU32(ImGuiCol_WindowBg);

        const auto centred = [&](float middle, float top, ImU32 colour, const char* words)
        {
            const ImVec2 size = ImGui::CalcTextSize(words);
            list->AddText(ImVec2(middle - size.x / 2, top), colour, words);
        };

        // The points' columns: their names, their tips, a guide down to the rows.
        for (size_t point = 0; point < keychron::c_DksPoints; ++point)
        {
            const float middle = x(c_Columns[point]);
            centred(middle, y(0), muted, c_PointNames[point]);
            const ImVec2 size = ImGui::CalcTextSize(c_PointNames[point]);
            if (ImGui::IsMouseHoveringRect(ImVec2(middle - size.x / 2, y(0)), ImVec2(middle + size.x / 2, y(0) + size.y)) &&
                ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", c_PointTips[point]);
            const float depth = keychron::DksDepth(slot, static_cast<keychron::DksPoint>(point));
            const float guideEnd = start.y + height - (adding ? c_Row : 0.0f) * px - 6 * px;
            for (float at = depthY(depth); at < guideEnd; at += 5 * px)
                list->AddLine(ImVec2(middle, at), ImVec2(middle, at + 2 * px), border);
        }

        // The travel: rest and bottom, then the key going down and coming back through its points.
        const float left = x(c_LineStart), right = x(c_LineEnd);
        for (const float tenths : { 0.0f, static_cast<float>(full) })
        {
            list->AddLine(ImVec2(left - 8 * px, depthY(tenths)), ImVec2(right + 8 * px, depthY(tenths)), border);
            const std::string label = (tenths == 0.0f ? "rest " : "bottom ") + Millimetres(static_cast<int>(tenths));
            const ImVec2      size  = ImGui::CalcTextSize(label.c_str());
            list->AddText(ImVec2(left - 14 * px - size.x, depthY(tenths) - size.y / 2), muted, label.c_str());
        }
        struct Vertex
        {
            float x;
            float depth;
        };
        const auto   depthOf = [&](size_t point)
        { return static_cast<float>(keychron::DksDepth(slot, static_cast<keychron::DksPoint>(point))); };
        const Vertex line[]  = { { left, 0.0f },
                                 { x(c_Columns[0]), depthOf(0) },
                                 { x(c_Columns[1]), depthOf(1) },
                                 { x(c_LineBottom), static_cast<float>(full) },
                                 { x(c_Columns[2]), depthOf(2) },
                                 { x(c_Columns[3]), depthOf(3) },
                                 { right, 0.0f } };
        std::vector<ImVec2> path;
        for (const Vertex& vertex : line)
            path.push_back(ImVec2(vertex.x, depthY(vertex.depth)));
        list->AddPolyline(path.data(), static_cast<int>(path.size()), muted, ImDrawFlags_None, 2 * px);

        // Where the key is, on the way down or back up.
        if (input.player && input.player->Depth() > 0.0f)
        {
            const float  depth = std::min(input.player->Depth(), static_cast<float>(full));
            const size_t first = input.player->IsComingUp() ? 3 : 0;
            for (size_t part = first; part < first + 3; ++part)
            {
                const Vertex a = line[part], b = line[part + 1];
                if (depth < std::min(a.depth, b.depth) || depth > std::max(a.depth, b.depth))
                    continue;
                const float along = a.depth == b.depth ? 0.0f : (depth - a.depth) / (b.depth - a.depth);
                list->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * along, depthY(depth)), 6 * px, accent);
                break;
            }
        }

        // The points: dragged up and down, typed, or stepped with the wheel.
        for (size_t point = 0; point < keychron::c_DksPoints; ++point)
        {
            ImGui::PushID(static_cast<int>(point));
            const auto   which = static_cast<keychron::DksPoint>(point);
            const int    depth = keychron::DksDepth(slot, which);
            const ImVec2 at(x(c_Columns[point]), depthY(static_cast<float>(depth)));
            const float  grab = 9 * px;

            ImGui::SetCursorScreenPos(ImVec2(at.x - grab, at.y - grab));
            ImGui::InvisibleButton("##point", ImVec2(2 * grab, 2 * grab));
            const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (ImGui::IsItemActivated())
            {
                state.draggedPoint = point;
                state.dragged      = input.slot;
            }
            if (ImGui::IsItemActive() && state.draggedPoint == point && state.dragged &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2 * px))
            {
                const float tenths = (ImGui::GetMousePos().y - y(c_TravelTop)) / (c_TravelHeight * px) * full;
                keychron::SetDksDepth(*state.dragged, which, Bounded(point, static_cast<int>(std::lround(tenths)), full));
            }
            const bool letGo = ImGui::IsItemDeactivated() && state.draggedPoint == point;
            int        step  = WheelOverItem();
            if (hovered && !ImGui::IsItemActive())
                ImGui::SetTooltip("Drag up or down, or turn the wheel: 0.1 mm at a time.");
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            list->AddCircleFilled(at, 6.5f * px, ImGui::GetColorU32(ImGuiCol_FrameBg));
            list->AddCircle(at, 6.5f * px, accent, 0, (hovered ? 3.0f : 2.5f) * px);

            // The depth beside it: above and inside for the shallow points, below and outside for the
            // deep ones, off the line and off each other.
            const std::string unit      = "mm";
            const float       fieldW    = c_FieldWidth * px + ImGui::CalcTextSize(" mm").x;
            const bool        rightSide = point == 0 || point == 2;
            const bool        below     = point == 1 || point == 2;
            const float       fieldH    = ImGui::GetFontSize() + 2 * px;
            ImGui::SetCursorScreenPos(ImVec2(rightSide ? at.x + 10 * px : at.x - 10 * px - fieldW,
                                             below ? at.y + 3 * px : at.y - fieldH - 2 * px));
            char& first = state.typed[point][0];
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3 * px, 1 * px));
            ImGui::SetNextItemWidth(c_FieldWidth * px);
            const ImGuiID id = ImGui::GetID("##depth");
            if (ImGui::GetActiveID() != id)
                std::snprintf(&first, sizeof(state.typed[point]), "%s", Millimetres(depth).c_str());
            ImGui::InputText("##depth", &first, sizeof(state.typed[point]),
                             ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll);
            const bool typed = ImGui::IsItemDeactivatedAfterEdit();
            step += WheelOverItem();
            ImGui::PopStyleVar();
            ImGui::SameLine(0.0f, 3 * px);
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, below ? at.y + 4 * px : at.y - fieldH - 1 * px));
            ImGui::TextUnformatted(unit.c_str());

            std::optional<int> chosen;
            if (letGo && state.dragged)
            {
                const int dragged = keychron::DksDepth(*state.dragged, which);
                if (dragged != keychron::DksDepth(input.slot, which))
                    chosen = dragged;
                state.draggedPoint.reset();
                state.dragged.reset();
            }
            else if (typed)
            {
                std::string number = &first;
                std::replace(number.begin(), number.end(), ',', '.');
                char*       end   = nullptr;
                const float value = std::strtof(number.c_str(), &end);
                if (end != number.c_str())
                    chosen = static_cast<int>(std::lround(value * 10.0f));
            }
            else if (step != 0)
            {
                chosen = keychron::DksDepth(input.slot, which) + step;
            }
            if (chosen)
            {
                keychron::DksSlot changed = input.slot;
                keychron::SetDksDepth(changed, which, Bounded(point, *chosen, full));
                if (!(changed == input.slot))
                    events.changed = changed;
            }
            ImGui::PopID();
        }

        // The rows: what a command sends, and its bars.
        float rowTop = y(c_TravelTop + c_TravelHeight + c_RowsGap);
        for (const size_t command : rows)
        {
            ImGui::PushID(static_cast<int>(command) + 100);
            const float    middle = rowTop + c_Row * px / 2;
            const uint16_t code   = input.slot.keycodes[command];

            // Its key, or an outlined place to add one.
            ImGui::SetCursorScreenPos(ImVec2(x(0), rowTop + 1 * px));
            if (!code)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
            }
            const bool        picking = input.picking == command;
            const std::string name    = code ? input.keycodeName(code) : std::string("add a key");
            if (ImGui::Button((name + "###key").c_str(), ImVec2(c_KeyWidth * px, c_Row * px - 2 * px)))
            {
                if (picking)
                    events.cancel = true;
                else
                    events.pick = command;
            }
            if (!code)
                ImGui::PopStyleColor(2);
            if (picking)
                list->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), BoardColours::Selected(), style.FrameRounding,
                              0, 2.5f * px);
            if (!code)
            {
                rowTop += c_Row * px;
                ImGui::PopID();
                continue;
            }

            list->AddLine(ImVec2(x(c_Columns[0]), middle), ImVec2(x(c_Columns[3]), middle), border);
            const std::vector<keychron::DksBar> bars = keychron::DksBarsOf(keychron::DksActionsOf(input.slot, command));
            const bool dragging = state.lane && state.lane->command == command && state.lane->to != state.lane->from;
            const bool held     = input.player && input.player->IsHeld(command);
            const ImU32 fill    = ImGui::GetColorU32(ImGuiCol_CheckMark, held ? 1.0f : 0.65f);
            if (dragging)
            {
                const float from = x(c_Columns[std::min(state.lane->anchor, state.lane->to)]);
                const float to   = x(c_Columns[std::max(state.lane->anchor, state.lane->to)]);
                list->AddRectFilled(ImVec2(from - 9 * px, middle - 8 * px), ImVec2(to + 9 * px, middle + 8 * px),
                                    ImGui::GetColorU32(ImGuiCol_CheckMark, 0.3f), 8 * px);
                list->AddRect(ImVec2(from - 9 * px, middle - 8 * px), ImVec2(to + 9 * px, middle + 8 * px), accent, 8 * px);
            }
            for (const keychron::DksBar& bar : bars)
            {
                if (dragging && state.lane->bar && bar == *state.lane->bar)
                    continue;
                const float from = x(c_Columns[bar.from]), to = x(c_Columns[bar.to]);
                if (bar.from == bar.to)
                {
                    list->AddCircleFilled(ImVec2(from, middle), 8.5f * px, fill);
                    list->AddText(ImVec2(from + 13 * px, middle - ImGui::GetFontSize() / 2), muted, "tap");
                }
                else
                {
                    list->AddRectFilled(ImVec2(from - 9 * px, middle - 8 * px), ImVec2(to + 9 * px, middle + 8 * px), fill,
                                        8 * px);
                    centred((from + x(c_Columns[bar.from + 1])) / 2, middle - ImGui::GetFontSize() / 2, surface, "held");
                }
            }

            for (uint8_t point = 0; point < keychron::c_DksPoints; ++point)
            {
                ImGui::PushID(point);
                const ImVec2 at(x(c_Columns[point]), middle);
                ImGui::SetCursorScreenPos(ImVec2(at.x - 30 * px, rowTop));
                ImGui::InvisibleButton("##lane", ImVec2(60 * px, c_Row * px));
                const auto hit = std::find_if(bars.begin(), bars.end(), [&](const keychron::DksBar& bar)
                                              { return point >= bar.from && point <= bar.to; });
                const bool isEnd = hit != bars.end() && hit->from != hit->to && (point == hit->from || point == hit->to);

                if (ImGui::IsItemActivated())
                {
                    DksEditorState::Lane lane;
                    lane.command = command;
                    lane.from = lane.to = lane.anchor = point;
                    if (isEnd)
                    {
                        lane.bar    = *hit;
                        lane.anchor = point == hit->from ? hit->to : hit->from;
                    }
                    state.lane = lane;
                }
                if (ImGui::IsItemActive() && state.lane)
                {
                    uint8_t nearest = 0;
                    for (uint8_t other = 1; other < keychron::c_DksPoints; ++other)
                        if (std::abs(x(c_Columns[other]) - ImGui::GetMousePos().x) <
                            std::abs(x(c_Columns[nearest]) - ImGui::GetMousePos().x))
                            nearest = other;
                    state.lane->to = nearest;
                }
                if (ImGui::IsItemDeactivated() && state.lane && state.lane->command == command)
                {
                    keychron::DksSlot changed = input.slot;
                    keychron::SetDksActions(changed, command, keychron::DksActionsFrom(WithDrag(bars, *state.lane)));
                    if (!(changed == input.slot))
                        events.changed = changed;
                    state.lane.reset();
                }
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && !state.lane)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    list->AddCircle(at, 9 * px, accent, 0, 1.5f * px);
                    if (hit == bars.end())
                        ImGui::SetTooltip("Click: tap %s at %s.\nDrag to another point: hold %s between them.", name.c_str(),
                                          c_PointNames[point], name.c_str());
                    else if (hit->from == hit->to)
                        ImGui::SetTooltip("Click: remove the tap.\nDrag to another point: hold %s instead.", name.c_str());
                    else
                        ImGui::SetTooltip("%s", isEnd ? "Click: remove the hold.\nDrag: move this end."
                                                      : "Click: remove the hold.");
                }
                list->AddCircleFilled(at, 3.5f * px, ImGui::GetColorU32(ImGuiCol_FrameBg));
                list->AddCircle(at, 3.5f * px, muted);
                ImGui::PopID();
            }
            rowTop += c_Row * px;
            ImGui::PopID();
        }

        // How to tap and how to hold (Rico asked, 2026-11-23): on the row that adds a key, else under the rows.
        float legendX = adding ? x(c_KeyWidth + 14) : x(0);
        float legendY = adding ? rowTop - c_Row * px + (c_Row * px - ImGui::GetFontSize()) / 2 : rowTop + 3 * px;
        const auto words = [&](const char* what, const char* does)
        {
            list->AddText(ImVec2(legendX, legendY), text, what);
            legendX += ImGui::CalcTextSize(what).x;
            list->AddText(ImVec2(legendX, legendY), muted, does);
            legendX += ImGui::CalcTextSize(does).x + 14 * px;
        };
        // While a key is chosen, that is what the line says instead.
        if (input.picking)
        {
            const float buttonY = legendY + (ImGui::GetFontSize() - ImGui::GetFrameHeight()) / 2;
            list->AddText(ImVec2(legendX, legendY), text,
                          input.pickByPressing ? "Click a key on the board above, or press it." : "Click a key on the board above.");
            legendX += ImGui::CalcTextSize(input.pickByPressing ? "Click a key on the board above, or press it."
                                                                : "Click a key on the board above.").x + 12 * px;
            ImGui::SetCursorScreenPos(ImVec2(legendX, buttonY));
            events.more = ImGui::Button("More keys...");
            ImGui::SameLine();
            ImGui::BeginDisabled(*input.picking >= keychron::c_DksCommands || input.slot.keycodes[*input.picking] == 0);
            events.remove = ImGui::Button("Remove");
            ImGui::EndDisabled();
            ImGui::SameLine();
            events.cancel = ImGui::Button("Cancel") || events.cancel;
        }
        else
        {
            const float legendMiddle = legendY + ImGui::GetFontSize() / 2;
            list->AddCircleFilled(ImVec2(legendX + 6 * px, legendMiddle), 6 * px, ImGui::GetColorU32(ImGuiCol_CheckMark, 0.8f));
            legendX += 17 * px;
            words("Click a point", ": tap");
            list->AddRectFilled(ImVec2(legendX, legendMiddle - 6 * px), ImVec2(legendX + 28 * px, legendMiddle + 6 * px),
                                ImGui::GetColorU32(ImGuiCol_CheckMark, 0.65f), 6 * px);
            legendX += 33 * px;
            words("Drag to another", ": hold");
            words("Click again", ": remove");

            const float  radius = ImGui::GetFontSize() * 0.55f;
            const ImVec2 help(legendX + radius, legendMiddle);
            list->AddCircle(help, radius, border);
            centred(help.x, legendY, muted, "?");
            if (ImGui::IsWindowHovered() &&
                ImGui::IsMouseHoveringRect(ImVec2(help.x - radius, help.y - radius), ImVec2(help.x + radius, help.y + radius)))
                ImGui::SetTooltip("Let go before Bottom In, the key releases everything and the last two points play nothing.\n\n"
                                  "It sends these on every layer: the keymap is not used.\n\n%zu of the %zu a profile can hold "
                                  "are used.",
                                  input.slotsUsed, input.slotsCount);
        }

        // The room it took.
        const float bottom = adding ? rowTop : rowTop + std::max(ImGui::GetFontSize() + 4 * px, input.picking ? ImGui::GetFrameHeight() + 2 * px : 0.0f);
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(c_Width * px, bottom - start.y));

        if (const std::string warning = DksWarning(slot, full); !warning.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Warning, "%s", warning.c_str());
            ImGui::PopTextWrapPos();
        }
        return events;
    }
}
