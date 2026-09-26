// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardView.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr float c_MinUnit = 28.0f;   // pixels per key unit, before DPI scaling
        constexpr float c_MaxUnit = 64.0f;

        // Fractions of a key unit.
        constexpr float c_KeyGap      = 0.06f;   // between two keys, on each side
        constexpr float c_KeyRounding = 0.12f;
        constexpr float c_LegendPad   = 0.10f;   // between a keycap's edge and its legends

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

        // Decals count for the board's extent, as in VIA, though they are never drawn.
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

        // `overlay` painted over `base`, as the GPU would blend it -- so a state is one opaque
        // fill, and the overlap of an L-shaped key's two rectangles does not show through.
        ImU32 Over(ImU32 base, ImU32 overlay)
        {
            const ImVec4 under = ImGui::ColorConvertU32ToFloat4(base);
            const ImVec4 over  = ImGui::ColorConvertU32ToFloat4(overlay);
            const float  a     = over.w;
            return ImGui::ColorConvertFloat4ToU32(ImVec4(under.x + (over.x - under.x) * a, under.y + (over.y - under.y) * a,
                                                         under.z + (over.z - under.z) * a, under.w));
        }

        // Legends come in four rows: the top face's three, then the front.
        int RowOf(size_t slot) { return static_cast<int>(slot / 3); }

        int RowsUsed(const BoardKey& key)
        {
            bool used[4] = {};
            for (size_t slot = 0; slot < c_LegendSlotCount; ++slot)
                used[RowOf(slot)] |= !key.legends[slot].text.empty();
            return static_cast<int>(std::count(std::begin(used), std::end(used), true));
        }

        // One legend size for the whole board, from the most rows any key fills -- two in
        // Keymap, three in a level view -- so that many fit on a 1u key, and never larger
        // than the UI font.
        float LegendSize(const BoardDescription& board, float unit)
        {
            int rows = 1;
            for (const BoardKey& key : board.keys)
                if (!key.geometry.decal)
                    rows = std::max(rows, RowsUsed(key));

            const float inner = unit * (1.0f - 2 * c_KeyGap - 2 * c_LegendPad);
            return std::min(ImGui::GetFontSize(), inner / static_cast<float>(rows));
        }

        // The legends of the rectangle p0-p1, clipped to it. Top and front rows sit on the
        // edges, the bottom row above the front one, and the middle row is centred between
        // whichever rows this key fills. A legend too wide for the key shrinks, down to 60%,
        // before it is clipped.
        void DrawLegends(ImDrawList* drawList, const BoardKey& key, ImVec2 p0, ImVec2 p1, float unit, float size,
                         bool dimmed)
        {
            ImFont*      font       = ImGui::GetFont();
            const float  pad        = unit * c_LegendPad;
            const float  innerWidth = (p1.x - p0.x) - 2 * pad;
            const ImVec4 clip(p0.x, p0.y, p1.x, p1.y);

            bool used[4] = {};
            for (size_t slot = 0; slot < c_LegendSlotCount; ++slot)
                used[RowOf(slot)] |= !key.legends[slot].text.empty();

            const float top    = p0.y + pad;
            const float front  = p1.y - pad - size;
            const float bottom = used[3] ? front - size : front;
            float       middle = (p0.y + p1.y - size) / 2.0f;
            if (used[0])
                middle = std::max(middle, top + size);
            if (used[2])
                middle = std::min(middle, bottom - size);
            const float rowTop[4] = { top, middle, bottom, front };

            for (size_t slot = 0; slot < c_LegendSlotCount; ++slot)
            {
                const Legend& legend = key.legends[slot];
                if (legend.text.empty())
                    continue;

                float  shown  = size;
                ImVec2 extent = font->CalcTextSizeA(shown, FLT_MAX, 0.0f, legend.text.c_str());
                if (extent.x > innerWidth && extent.x > 0.0f)
                {
                    shown  = std::max(size * 0.6f, size * innerWidth / extent.x);
                    extent = font->CalcTextSizeA(shown, FLT_MAX, 0.0f, legend.text.c_str());
                }

                const size_t column = slot % 3;
                const float  x      = column == 0 ? p0.x + pad
                                    : column == 1 ? (p0.x + p1.x - extent.x) / 2.0f
                                                  : p1.x - pad - extent.x;
                const float  y      = rowTop[RowOf(slot)] + (size - shown) / 2.0f;

                ImU32 colour = BoardColours::Legend(legend.role);
                if (dimmed)
                    colour = Over(colour, BoardColours::c_Dimmed);

                drawList->AddText(font, shown, ImVec2(x, y), colour, legend.text.c_str(), nullptr, 0.0f, &clip);
            }
        }

        // One key; true when the mouse is on it. A rotated key is drawn unrotated, then the
        // vertices it just added to the draw list are turned about its origin -- rounded
        // corners, outlines and legends alike, with no rotated variant of any drawing call.
        // Hovering turns the mouse the other way and tests the unrotated rectangles.
        bool DrawKey(ImDrawList* drawList, ImVec2 origin, float unit, float legendSize, const BoardKey& described,
                     bool canHover)
        {
            const DefinitionKey& key      = described.geometry;
            const float          gap      = unit * c_KeyGap;
            const float          rounding = unit * c_KeyRounding;

            const Rotation rotation(key, ImVec2(origin.x + key.rotationX * unit, origin.y + key.rotationY * unit));
            const int      firstVertex = drawList->VtxBuffer.Size;

            const ImVec2 p0(origin.x + key.x * unit + gap, origin.y + key.y * unit + gap);
            const ImVec2 p1(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap);

            const bool hasSecond = key.HasSecondRectangle();
            ImVec2     q0, q1;
            if (hasSecond)
            {
                q0 = ImVec2(origin.x + (key.x + key.secondX) * unit + gap, origin.y + (key.y + key.secondY) * unit + gap);
                q1 = ImVec2(q0.x + key.secondWidth * unit - 2 * gap, q0.y + key.secondHeight * unit - 2 * gap);
            }

            // Only a mouse inside the visible part of the window counts, as with
            // ImGui::IsMouseHoveringRect().
            const ImVec2 mouse  = rotation.Undo(ImGui::GetIO().MousePos);
            const bool   inView = canHover &&
                                ImGui::IsMouseHoveringRect(drawList->GetClipRectMin(), drawList->GetClipRectMax());
            const bool hovered = inView && (Contains(p0, p1, mouse) || (hasSecond && Contains(q0, q1, mouse)));

            const bool isDimmed = (described.marks & Mark::Dimmed) != 0;

            ImU32 fill = BoardColours::Fill(described.fill, described.heat);
            if ((described.marks & Mark::Pressed) != 0)
                fill = Over(fill, BoardColours::c_Pressed);
            if (hovered)
                fill = Over(fill, BoardColours::c_Hovered);
            if (isDimmed)
                fill = Over(fill, BoardColours::c_Dimmed);

            drawList->AddRectFilled(p0, p1, fill, rounding);
            if (hasSecond)
                drawList->AddRectFilled(q0, q1, fill, rounding);

            DrawLegends(drawList, described, p0, p1, unit, legendSize, isDimmed);

            // Outlines nest, the first outermost, so several states read at once.
            const float thickness = std::max(2.0f, unit * 0.04f);
            float       inset     = 0.0f;
            for (const auto& [mark, colour] : { std::pair{ Mark::Selected, BoardColours::c_Selected },
                                                std::pair{ Mark::Warning, BoardColours::c_Warning },
                                                std::pair{ Mark::Highlighted, BoardColours::c_Highlighted } })
            {
                if ((described.marks & mark) == 0)
                    continue;

                const float in = inset + thickness / 2;
                drawList->AddRect(ImVec2(p0.x + in, p0.y + in), ImVec2(p1.x - in, p1.y - in), colour, rounding, 0,
                                  thickness);
                if (hasSecond)
                    drawList->AddRect(ImVec2(q0.x + in, q0.y + in), ImVec2(q1.x - in, q1.y - in), colour, rounding, 0,
                                      thickness);
                inset += thickness;
            }

            if (key.rotation != 0.0f)
                for (int vertex = firstVertex; vertex < drawList->VtxBuffer.Size; ++vertex)
                    drawList->VtxBuffer[vertex].pos = rotation.Apply(drawList->VtxBuffer[vertex].pos);

            return hovered;
        }

        ImVec2 CentreOnScreen(const BoardDescription& board, size_t key, ImVec2 origin, float unit)
        {
            const auto [x, y] = KeyCentre(board.keys[key].geometry);
            return ImVec2(origin.x + x * unit, origin.y + y * unit);
        }

        void DrawLines(ImDrawList* drawList, const BoardDescription& board, ImVec2 origin, float unit)
        {
            const float thickness = std::max(2.0f, unit * 0.05f);

            for (const BoardLine& line : board.lines)
            {
                std::vector<ImVec2> points;
                for (size_t key : line.keys)
                    if (key < board.keys.size())
                        points.push_back(CentreOnScreen(board, key, origin, unit));

                if (points.size() >= 2)
                    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), BoardColours::Line(line.marks),
                                          ImDrawFlags_None, (line.marks & Mark::Highlighted) != 0 ? thickness * 1.5f
                                                                                                    : thickness);
            }
        }

        // Each label at the average position of its keys along its edge, nudged apart so none
        // overlaps -- lined up with the keys on a regular board, an ordered list on one whose
        // wiring does not follow its layout. A label with no keys has no place and is not drawn.
        void DrawEdgeLabels(ImDrawList* drawList, const BoardDescription& board, ImVec2 corner, ImVec2 origin,
                            float unit, float leftMargin, bool canHover, BoardEvents& events)
        {
            const float gap = ImGui::GetStyle().ItemInnerSpacing.x;

            for (const BoardEdge edge : { BoardEdge::Left, BoardEdge::Top })
            {
                std::vector<size_t> shown;
                std::vector<float>  wanted;
                std::vector<float>  extents;

                for (size_t index = 0; index < board.labels.size(); ++index)
                {
                    const EdgeLabel& label = board.labels[index];
                    if (label.edge != edge)
                        continue;

                    float sum   = 0.0f;
                    int   count = 0;
                    for (size_t key : label.keys)
                        if (key < board.keys.size())
                        {
                            const ImVec2 centre = CentreOnScreen(board, key, origin, unit);
                            sum += edge == BoardEdge::Left ? centre.y : centre.x;
                            ++count;
                        }
                    if (count == 0)
                        continue;

                    const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
                    shown.push_back(index);
                    wanted.push_back(sum / static_cast<float>(count));
                    extents.push_back(edge == BoardEdge::Left ? size.y : size.x);
                }

                const std::vector<float> placed = SpreadApart(wanted, extents, gap);

                for (size_t i = 0; i < shown.size(); ++i)
                {
                    const EdgeLabel& label = board.labels[shown[i]];
                    const ImVec2     size  = ImGui::CalcTextSize(label.text.c_str());
                    const ImVec2     at    = edge == BoardEdge::Left
                                                 ? ImVec2(corner.x + leftMargin - gap - size.x, placed[i] - size.y / 2)
                                                 : ImVec2(placed[i] - size.x / 2, corner.y);
                    const ImU32 colour = BoardColours::EdgeLabel(label.marks);

                    drawList->AddText(at, colour, label.text.c_str());
                    if ((label.marks & Mark::Struck) != 0)
                        drawList->AddLine(ImVec2(at.x - 1, at.y + size.y / 2), ImVec2(at.x + size.x + 1, at.y + size.y / 2),
                                          colour, 1.5f);

                    if (canHover && ImGui::IsMouseHoveringRect(at, ImVec2(at.x + size.x, at.y + size.y)))
                    {
                        events.hoveredLabel = shown[i];
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            events.clickedLabel = shown[i];
                    }
                }
            }
        }
    }

    BoardEvents DrawBoard(const BoardDescription& board, float maxHeight)
    {
        BoardEvents events;

        Bounds bounds;
        for (const BoardKey& key : board.keys)
            AddKey(bounds, key.geometry);

        if (bounds.IsEmpty())
        {
            ImGui::TextUnformatted("The definition has no keys to draw.");
            return events;
        }

        // Room for the labels around the edges, at the UI font.
        const float gap        = ImGui::GetStyle().ItemInnerSpacing.x;
        float       leftMargin = 0.0f;
        float       topMargin  = 0.0f;
        for (const EdgeLabel& label : board.labels)
        {
            if (label.edge == BoardEdge::Left)
                leftMargin = std::max(leftMargin, ImGui::CalcTextSize(label.text.c_str()).x + 2 * gap);
            else
                topMargin = std::max(topMargin, ImGui::GetTextLineHeight() + gap);
        }

        const float scale   = ImGui::GetStyle().FontScaleDpi;
        const float width   = bounds.maxX - bounds.minX;
        const float height  = bounds.maxY - bounds.minY;
        const float fitting = std::min((ImGui::GetContentRegionAvail().x - leftMargin) / width,
                                       (maxHeight - topMargin) / height);
        const float unit    = std::clamp(fitting, c_MinUnit * scale, c_MaxUnit * scale);

        const ImVec2 corner = ImGui::GetCursorScreenPos();
        const ImVec2 origin(corner.x + leftMargin - bounds.minX * unit, corner.y + topMargin - bounds.minY * unit);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Only when this window is under the mouse, so a window stacked above the board
        // neither hovers nor clicks the key beneath it.
        const bool  canHover   = ImGui::IsWindowHovered();
        const float legendSize = LegendSize(board, unit);

        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const BoardKey& key = board.keys[index];
            if (key.geometry.decal)
                continue;

            if (DrawKey(drawList, origin, unit, legendSize, key, canHover))
                events.hoveredKey = index;
        }

        if (events.hoveredKey && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            events.clickedKey = events.hoveredKey;

        DrawLines(drawList, board, origin, unit);
        DrawEdgeLabels(drawList, board, corner, origin, unit, leftMargin, canHover, events);

        // Claim the space drawn into, so the window scrolls and sizes around the board.
        ImGui::Dummy(ImVec2(leftMargin + width * unit, topMargin + height * unit));
        return events;
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
        const float gap      = std::max(1.0f, unit * c_KeyGap);
        const float rounding = unit * c_KeyRounding;
        const ImU32 fill     = BoardColours::Fill(KeyFill::Neutral, 0.0f);

        for (const DefinitionKey& key : placed)
        {
            if (key.decal)
                continue;

            // Rotated as the board is: drawn straight, then its vertices turned.
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
