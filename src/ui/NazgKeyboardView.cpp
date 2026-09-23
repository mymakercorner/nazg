// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardView.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include "imgui.h"

namespace nazg
{
    namespace
    {
        constexpr float c_MinUnit = 28.0f;   // pixels per key unit, before DPI scaling
        constexpr float c_MaxUnit = 64.0f;

        struct Offset
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        // Where a key's top-left corner is, in key units.
        struct Bounds
        {
            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;

            void Add(float x, float y, float width, float height)
            {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x + width);
                maxY = std::max(maxY, y + height);
            }

            bool IsEmpty() const { return minX > maxX; }
        };

        void AddKey(Bounds& bounds, const DefinitionKey& key, Offset offset)
        {
            bounds.Add(key.x + offset.x, key.y + offset.y, key.width, key.height);
            if (key.HasSecondRectangle())
                bounds.Add(key.x + key.secondX + offset.x, key.y + key.secondY + offset.y,
                           key.secondWidth, key.secondHeight);
        }

        // VIA definitions draw each layout alternative away from the board, and VIA's app
        // moves the selected one onto the spot where option 0 sits. Same here: per
        // group, the shift that lines the selected option's top-left up with option 0's.
        std::vector<Offset> LayoutOffsets(const Keyboard& keyboard)
        {
            const size_t groups = keyboard.layoutSelection.size();

            std::vector<Bounds> defaults(groups);
            std::vector<Bounds> selected(groups);

            for (const DefinitionKey& key : keyboard.definition.keys)
            {
                if (key.layoutIndex < 0 || static_cast<size_t>(key.layoutIndex) >= groups)
                    continue;

                const size_t group = static_cast<size_t>(key.layoutIndex);
                if (key.layoutOption == 0)
                    AddKey(defaults[group], key, {});
                if (key.layoutOption == keyboard.layoutSelection[group])
                    AddKey(selected[group], key, {});
            }

            std::vector<Offset> offsets(groups);
            for (size_t group = 0; group < groups; ++group)
                if (!defaults[group].IsEmpty() && !selected[group].IsEmpty())
                    offsets[group] = { defaults[group].minX - selected[group].minX,
                                       defaults[group].minY - selected[group].minY };

            return offsets;
        }

        Offset OffsetOf(const DefinitionKey& key, const std::vector<Offset>& offsets)
        {
            if (key.layoutIndex < 0 || static_cast<size_t>(key.layoutIndex) >= offsets.size())
                return {};
            return offsets[static_cast<size_t>(key.layoutIndex)];
        }

        void DrawKey(ImDrawList* drawList, ImVec2 origin, float unit, const DefinitionKey& key, Offset offset,
                     const KeycapLegend& legend, bool selected, bool& hovered)
        {
            const float gap      = unit * 0.06f;
            const float rounding = unit * 0.12f;

            const ImVec2 p0(origin.x + (key.x + offset.x) * unit + gap, origin.y + (key.y + offset.y) * unit + gap);
            const ImVec2 p1(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap);

            hovered = ImGui::IsMouseHoveringRect(p0, p1);

            ImVec2 q0, q1;
            if (key.HasSecondRectangle())
            {
                q0 = ImVec2(origin.x + (key.x + key.secondX + offset.x) * unit + gap,
                            origin.y + (key.y + key.secondY + offset.y) * unit + gap);
                q1 = ImVec2(q0.x + key.secondWidth * unit - 2 * gap, q0.y + key.secondHeight * unit - 2 * gap);
                hovered = hovered || ImGui::IsMouseHoveringRect(q0, q1);
            }

            const ImU32 fill = hovered ? IM_COL32(88, 92, 110, 255) : IM_COL32(58, 60, 72, 255);

            drawList->AddRectFilled(p0, p1, fill, rounding);
            if (key.HasSecondRectangle())
                drawList->AddRectFilled(q0, q1, fill, rounding);

            if (selected)
            {
                const ImU32 accent = IM_COL32(240, 180, 60, 255);
                drawList->AddRect(p0, p1, accent, rounding, 0, 2.0f);
                if (key.HasSecondRectangle())
                    drawList->AddRect(q0, q1, accent, rounding, 0, 2.0f);
            }

            // Legends from the top-left, the secondary (Shift, hold action) first and
            // smaller, clipped to the key and wrapped to its width.
            ImFont*      font      = ImGui::GetFont();
            const float  fontSize  = ImGui::GetFontSize();
            const float  pad       = unit * 0.10f;
            const ImVec4 clip(p0.x, p0.y, p1.x, p1.y);
            const float  wrapWidth = (p1.x - p0.x) - 2 * pad;

            ImVec2 text(p0.x + pad, p0.y + pad * 0.6f);

            if (!legend.secondary.empty())
            {
                const float small = fontSize * 0.8f;
                drawList->AddText(font, small, text, IM_COL32(170, 175, 190, 255), legend.secondary.c_str(), nullptr,
                                  wrapWidth, &clip);
                text.y += small;
            }

            if (!legend.primary.empty())
                drawList->AddText(font, fontSize, text, IM_COL32(235, 235, 240, 255), legend.primary.c_str(), nullptr,
                                  wrapWidth, &clip);
        }
    }

    void DrawKeyboardView(const Keyboard& keyboard, int& layer, const HostLayout& layout, KeySelection& selection)
    {
        if (ImGui::BeginTabBar("layers"))
        {
            for (int index = 0; index < keyboard.keymap.Layers(); ++index)
            {
                const std::string label = "Layer " + std::to_string(index);
                if (ImGui::BeginTabItem(label.c_str()))
                {
                    layer = index;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        const std::vector<Offset> offsets = LayoutOffsets(keyboard);

        Bounds board;
        for (const DefinitionKey& key : keyboard.definition.keys)
            if (keyboard.IsKeyVisible(key))
                AddKey(board, key, OffsetOf(key, offsets));

        if (board.IsEmpty())
        {
            ImGui::TextUnformatted("The definition has no keys to draw.");
            return;
        }

        const float scale = ImGui::GetStyle().FontScaleDpi;
        const float width = board.maxX - board.minX;
        const float unit  = std::clamp(ImGui::GetContentRegionAvail().x / width, c_MinUnit * scale, c_MaxUnit * scale);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        origin.x -= board.minX * unit;
        origin.y -= board.minY * unit;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const auto  current  = static_cast<uint8_t>(layer);

        for (const DefinitionKey& key : keyboard.definition.keys)
        {
            if (!keyboard.IsKeyVisible(key))
                continue;

            const Keycode keycode  = keyboard.KeycodeFor(key, current);
            const bool    selected = selection.active && selection.row == key.row && selection.column == key.column;
            bool          hovered  = false;
            DrawKey(drawList, origin, unit, key, OffsetOf(key, offsets), LegendFor(keycode, layout), selected, hovered);

            // Only when this window is under the mouse, so a click on a window stacked
            // above the board does not select the key beneath it.
            if (hovered && ImGui::IsWindowHovered())
            {
                ImGui::SetTooltip("%s\nrow %d, column %d", FormatKeycode(keycode).c_str(), key.row, key.column);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    selection = { true, key.row, key.column };
            }
        }

        // Claim the space drawn into, so the window scrolls and sizes around the board.
        ImGui::Dummy(ImVec2(width * unit, (board.maxY - board.minY) * unit));
    }
}
