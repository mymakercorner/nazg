// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardView.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"

namespace nazg
{
    namespace
    {
        constexpr float c_MinUnit = 28.0f;   // pixels per key unit, before DPI scaling
        constexpr float c_MaxUnit = 64.0f;

        // A key's rotation: a turn by its angle about its origin -- clockwise on screen,
        // since y grows downward. In key units for the board's extent, in pixels for
        // drawing and hit-testing.
        struct Rotation
        {
            float  cosine = 1.0f;
            float  sine   = 0.0f;
            ImVec2 pivot;

            Rotation(const DefinitionKey& key, ImVec2 pivotPoint) : pivot(pivotPoint)
            {
                const float radians = key.rotation * 3.14159265358979f / 180.0f;
                cosine = std::cos(radians);
                sine   = std::sin(radians);
            }

            ImVec2 Apply(ImVec2 point) const { return Turn(point, sine); }
            ImVec2 Undo(ImVec2 point) const { return Turn(point, -sine); }

        private:
            ImVec2 Turn(ImVec2 point, float s) const
            {
                const float dx = point.x - pivot.x;
                const float dy = point.y - pivot.y;
                return ImVec2(pivot.x + dx * cosine - dy * s, pivot.y + dx * s + dy * cosine);
            }
        };

        // What the board covers, in key units -- rotated keys by their rotated corners, as
        // VIA measures it.
        struct Bounds
        {
            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;

            void Add(ImVec2 point)
            {
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
            }

            void Add(float x, float y, float width, float height, const Rotation& rotation)
            {
                Add(rotation.Apply(ImVec2(x, y)));
                Add(rotation.Apply(ImVec2(x + width, y)));
                Add(rotation.Apply(ImVec2(x, y + height)));
                Add(rotation.Apply(ImVec2(x + width, y + height)));
            }

            bool IsEmpty() const { return minX > maxX; }
        };

        void AddKey(Bounds& bounds, const DefinitionKey& key)
        {
            const Rotation rotation(key, ImVec2(key.rotationX, key.rotationY));

            bounds.Add(key.x, key.y, key.width, key.height, rotation);
            if (key.HasSecondRectangle())
                bounds.Add(key.x + key.secondX, key.y + key.secondY, key.secondWidth, key.secondHeight, rotation);
        }

        bool Contains(ImVec2 p0, ImVec2 p1, ImVec2 point)
        {
            return point.x >= p0.x && point.x < p1.x && point.y >= p0.y && point.y < p1.y;
        }

        // A rotated key is drawn unrotated, then the vertices it just added to the draw
        // list are turned about its origin -- rounded corners, outline and legends alike,
        // with no rotated variant of any drawing call. Hovering turns the mouse the other
        // way and tests the unrotated rectangles.
        void DrawKey(ImDrawList* drawList, ImVec2 origin, float unit, const DefinitionKey& key,
                     const KeycapLegend& legend, bool selected, bool& hovered)
        {
            const float gap      = unit * 0.06f;
            const float rounding = unit * 0.12f;

            const Rotation rotation(key, ImVec2(origin.x + key.rotationX * unit, origin.y + key.rotationY * unit));
            const int      firstVertex = drawList->VtxBuffer.Size;

            const ImVec2 p0(origin.x + key.x * unit + gap, origin.y + key.y * unit + gap);
            const ImVec2 p1(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap);

            // Only a mouse inside the visible part of the window counts, as with
            // ImGui::IsMouseHoveringRect().
            const ImVec2 mouse = rotation.Undo(ImGui::GetIO().MousePos);
            const bool   inView = ImGui::IsMouseHoveringRect(drawList->GetClipRectMin(), drawList->GetClipRectMax());

            hovered = inView && Contains(p0, p1, mouse);

            ImVec2 q0, q1;
            if (key.HasSecondRectangle())
            {
                q0 = ImVec2(origin.x + (key.x + key.secondX) * unit + gap,
                            origin.y + (key.y + key.secondY) * unit + gap);
                q1 = ImVec2(q0.x + key.secondWidth * unit - 2 * gap, q0.y + key.secondHeight * unit - 2 * gap);
                hovered = hovered || (inView && Contains(q0, q1, mouse));
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

            if (key.rotation != 0.0f)
                for (int vertex = firstVertex; vertex < drawList->VtxBuffer.Size; ++vertex)
                    drawList->VtxBuffer[vertex].pos = rotation.Apply(drawList->VtxBuffer[vertex].pos);
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

    void DrawDefinitionPreview(const KeyboardDefinition& definition, float width, float height)
    {
        // Choice 0 of every group a key names, whether or not the labels describe it.
        int groups = 0;
        for (const DefinitionKey& key : definition.keys)
            groups = std::max(groups, key.layoutIndex + 1);

        const std::vector<DefinitionKey> placed =
            PlaceKeys(definition, std::vector<uint8_t>(static_cast<size_t>(groups), 0));

        Bounds board;
        for (const DefinitionKey& key : placed)
            AddKey(board, key);

        if (board.IsEmpty() || width <= 0.0f || height <= 0.0f)
        {
            ImGui::TextDisabled("(no keys)");
            return;
        }

        const float unit = std::min(width / (board.maxX - board.minX), height / (board.maxY - board.minY));

        ImVec2 origin = ImGui::GetCursorScreenPos();
        origin.x -= board.minX * unit;
        origin.y -= board.minY * unit;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float gap      = std::max(1.0f, unit * 0.06f);
        const float rounding = unit * 0.12f;
        const ImU32 fill     = IM_COL32(58, 60, 72, 255);

        for (const DefinitionKey& key : placed)
        {
            if (key.decal)
                continue;

            // Rotated as the board view does it: drawn straight, then its vertices turned.
            const Rotation rotation(key, ImVec2(origin.x + key.rotationX * unit, origin.y + key.rotationY * unit));
            const int      firstVertex = drawList->VtxBuffer.Size;

            const ImVec2 p0(origin.x + key.x * unit + gap, origin.y + key.y * unit + gap);
            drawList->AddRectFilled(p0, ImVec2(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap),
                                    fill, rounding);

            if (key.HasSecondRectangle())
            {
                const ImVec2 q0(origin.x + (key.x + key.secondX) * unit + gap,
                                origin.y + (key.y + key.secondY) * unit + gap);
                drawList->AddRectFilled(
                    q0, ImVec2(q0.x + key.secondWidth * unit - 2 * gap, q0.y + key.secondHeight * unit - 2 * gap), fill,
                    rounding);
            }

            if (key.rotation != 0.0f)
                for (int vertex = firstVertex; vertex < drawList->VtxBuffer.Size; ++vertex)
                    drawList->VtxBuffer[vertex].pos = rotation.Apply(drawList->VtxBuffer[vertex].pos);
        }

        ImGui::Dummy(ImVec2((board.maxX - board.minX) * unit, (board.maxY - board.minY) * unit));
    }
}
