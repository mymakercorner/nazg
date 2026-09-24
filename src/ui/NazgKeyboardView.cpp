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

        void AddKey(Bounds& bounds, const DefinitionKey& key)
        {
            bounds.Add(key.x, key.y, key.width, key.height);
            if (key.HasSecondRectangle())
                bounds.Add(key.x + key.secondX, key.y + key.secondY, key.secondWidth, key.secondHeight);
        }

        void DrawKey(ImDrawList* drawList, ImVec2 origin, float unit, const DefinitionKey& key,
                     const KeycapLegend& legend, bool selected, bool& hovered)
        {
            const float gap      = unit * 0.06f;
            const float rounding = unit * 0.12f;

            const ImVec2 p0(origin.x + key.x * unit + gap, origin.y + key.y * unit + gap);
            const ImVec2 p1(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap);

            hovered = ImGui::IsMouseHoveringRect(p0, p1);

            ImVec2 q0, q1;
            if (key.HasSecondRectangle())
            {
                q0 = ImVec2(origin.x + (key.x + key.secondX) * unit + gap,
                            origin.y + (key.y + key.secondY) * unit + gap);
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

        // Decals count for the board's extent, as in VIA, but are never drawn.
        const std::vector<DefinitionKey> placed = PlaceKeys(keyboard.definition, keyboard.layoutSelection);

        Bounds board;
        for (const DefinitionKey& key : placed)
            AddKey(board, key);

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

        for (const DefinitionKey& key : placed)
        {
            if (key.decal)
                continue;

            const Keycode keycode  = keyboard.KeycodeFor(key, current);
            const bool    selected = selection.active && selection.row == key.row && selection.column == key.column;
            bool          hovered  = false;
            DrawKey(drawList, origin, unit, key, LegendFor(keycode, layout), selected, hovered);

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
