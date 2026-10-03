// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardView.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <variant>
#include <vector>

#include "imgui.h"

#include "ui/NazgKeyShape.h"
#include "ui/NazgKeycapLayout.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        LegendFonts g_LegendFonts;

        // The board's size follows the window, down to the smallest legible text
        // (c_SmallestUnit, ui/NazgKeycapLayout.h); below it the board scrolls. Pixels before
        // DPI scaling.
        constexpr float c_MaxUnit = 96.0f;

        // Fractions of a key unit; the gap between keys and the legends' padding are the
        // layout's.
        constexpr float c_KeyRounding   = 0.12f;
        constexpr float c_InnerRounding = 0.02f;   // the inner corner of an L-shaped key: a sixth (Rico)
        constexpr float c_Lip           = 0.06f;   // Bottom lip: in the key's own bottom gap, no more
        constexpr float c_PlateMargin   = 0.35f;   // the plate past the keys, on every side
        constexpr float c_PlateRounding = 0.25f;

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

        // A fill with the key's states painted over it, in a fixed order.
        ImU32 WithStates(ImU32 colour, uint8_t marks, bool hovered)
        {
            if ((marks & Mark::Checked) != 0)
                colour = Over(colour, BoardColours::CheckedTint());
            if ((marks & Mark::Pressed) != 0)
                colour = Over(colour, BoardColours::Pressed());
            if ((marks & Mark::Highlighted) != 0)
                colour = Over(colour, BoardColours::HighlightedTint());
            if ((marks & Mark::HighlightedSecond) != 0)
                colour = Over(colour, BoardColours::HighlightedSecondTint());
            if (hovered)
                colour = Over(colour, BoardColours::Hovered());
            if ((marks & Mark::Dimmed) != 0)
                colour = Over(colour, BoardColours::Dimmed());
            return colour;
        }

        // The key's outline in screen pixels, unrotated, `inset` pixels in from its edge.
        std::vector<ContourPoint> ContourOnScreen(const DefinitionKey& key, ImVec2 origin, float unit, float inset)
        {
            std::vector<ContourPoint> contour = KeyContour(key);
            for (ContourPoint& point : contour)
                point = { origin.x + point.x * unit, origin.y + point.y * unit };
            return InsetContour(contour, inset);
        }

        // The outline as a path, its corners rounded: the outer ones by `outerRadius`, the inner
        // corner of an L the other way, by `innerRadius`. An outline drawn inside another keeps
        // its distance all round when its outer radius shrinks by that distance and its inner
        // one grows by it.
        void TraceContour(ImDrawList* drawList, const std::vector<ContourPoint>& contour, float outerRadius,
                          float innerRadius)
        {
            constexpr float c_Pi    = 3.14159265358979f;
            const size_t    count   = contour.size();

            for (size_t i = 0; i < count; ++i)
            {
                const ContourPoint before = contour[(i + count - 1) % count];
                const ContourPoint here   = contour[i];
                const ContourPoint after  = contour[(i + 1) % count];

                const float lengthIn  = std::hypot(here.x - before.x, here.y - before.y);
                const float lengthOut = std::hypot(after.x - here.x, after.y - here.y);
                if (lengthIn <= 0.0f || lengthOut <= 0.0f)
                    continue;

                const bool  inner  = IsInnerCorner(contour, i);
                const float radius = std::min(inner ? innerRadius : outerRadius, std::min(lengthIn, lengthOut) / 2.0f);
                if (radius < 0.5f)
                {
                    drawList->PathLineTo(ImVec2(here.x, here.y));
                    continue;
                }

                // Each edge's inward side; the arc's centre is inside an outer corner, outside
                // an inner one, and it touches both edges.
                const ImVec2 normalIn((before.y - here.y) / lengthIn, (here.x - before.x) / lengthIn);
                const ImVec2 normalOut((here.y - after.y) / lengthOut, (after.x - here.x) / lengthOut);
                const float  side = inner ? -radius : radius;
                const ImVec2 centre(here.x + side * (normalIn.x + normalOut.x), here.y + side * (normalIn.y + normalOut.y));

                const float from  = std::atan2(-side * normalIn.y, -side * normalIn.x);
                float       sweep = std::atan2(-side * normalOut.y, -side * normalOut.x) - from;
                if (sweep > c_Pi)
                    sweep -= 2 * c_Pi;
                if (sweep < -c_Pi)
                    sweep += 2 * c_Pi;
                drawList->PathArcTo(centre, radius, from, from + sweep);
            }
        }

        // A key's corner radii in pixels: the outer corners', and the inner corner's of an L.
        struct Rounding
        {
            float outer;
            float inner;
        };

        void FillContour(ImDrawList* drawList, const std::vector<ContourPoint>& contour, Rounding rounding,
                         ImU32 colour)
        {
            TraceContour(drawList, contour, rounding.outer, rounding.inner);
            drawList->PathFillConcave(colour);
        }

        // A line along the contour, `inset` pixels in from the face's edge to its middle.
        void StrokeContour(ImDrawList* drawList, const std::vector<ContourPoint>& face, Rounding rounding, float inset,
                           float thickness, ImU32 colour)
        {
            TraceContour(drawList, InsetContour(face, inset), std::max(0.0f, rounding.outer - inset),
                         rounding.inner + inset);
            drawList->PathStroke(colour, thickness, ImDrawFlags_Closed);
        }

        // The legends of the face p0-p1 -- an L-shaped key's first rectangle -- placed by the
        // layout and clipped to the face.
        void DrawLegends(ImDrawList* drawList, const BoardKey& key, ImVec2 p0, ImVec2 p1, float unit, bool dimmed)
        {
            const ImGuiTextMeasurer measurer(g_LegendFonts, ImGui::GetFont());
            const FaceBox           face{ p0.x, p0.y, p1.x, p1.y };
            const DefinitionKey&    geometry = key.geometry;
            const bool oneUnit = !geometry.HasSecondRectangle() && geometry.width <= 1.0f && geometry.height <= 1.0f;

            const KeycapPrimitives primitives =
                std::holds_alternative<KeycapLegend>(key.legends)
                    ? LayOutKeycap(std::get<KeycapLegend>(key.legends), CurrentBoardStyle().legends, face, unit, oneUnit,
                                   measurer)
                    : LayOutSlots(std::get<SlotLegends>(key.legends), face, unit, measurer);

            const auto colourOf = [&](LegendInk ink)
            {
                const ImU32 colour = BoardColours::Legend(ink, key.fill);
                return dimmed ? Over(colour, BoardColours::Dimmed()) : colour;
            };

            const ImVec4 clip(p0.x, p0.y, p1.x, p1.y);
            for (const PlacedText& text : primitives.texts)
                drawList->AddText(measurer.FontFor(text.weight), measurer.ImGuiSize(text.size), ImVec2(text.x, text.y),
                                  colourOf(text.ink), text.text.c_str(), nullptr, 0.0f, &clip);

            // A shaft and a filled head: every arrow alike, at the legend's weight, whatever the font.
            for (const PlacedArrow& arrow : primitives.arrows)
            {
                const ArrowShape shape  = ShapeOf(arrow);
                const ImU32      colour = colourOf(arrow.ink);
                drawList->AddLine(ImVec2(shape.tailX, shape.tailY), ImVec2(shape.shaftEndX, shape.shaftEndY), colour,
                                  arrow.stroke);
                drawList->AddTriangleFilled(ImVec2(shape.tipX, shape.tipY), ImVec2(shape.baseAX, shape.baseAY),
                                            ImVec2(shape.baseBX, shape.baseBY), colour);
            }
        }

        // A key is drawn in passes, each over every key before the next: the lips first, so an
        // L-shaped key shows its lip only where its contour has a bottom; the keycaps; then the
        // lines; then the legends, so a line runs over the keycaps and under their legends.
        enum class KeyPass
        {
            Lip,       // Bottom lip only
            Keycap,    // the face, its border and the state outlines
            Legends,
        };

        // One key's pass; true when the mouse is on it. A rotated key is drawn unrotated, then
        // the vertices it just added to the draw list are turned about its origin -- rounded
        // corners, outlines and legends alike, with no rotated variant of any drawing call.
        // Hovering turns the mouse the other way and tests the unrotated rectangles.
        bool DrawKey(ImDrawList* drawList, ImVec2 origin, float unit, const BoardKey& described, bool canHover,
                     KeyPass pass)
        {
            const DefinitionKey& key      = described.geometry;
            const float          gap      = unit * c_KeyGap;
            const Rounding       rounding{ unit * c_KeyRounding, unit * c_InnerRounding };
            const float          scale    = ImGui::GetStyle().FontScaleDpi;

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

            const bool                 isDimmed = (described.marks & Mark::Dimmed) != 0;
            const BoardColours::Keycap colours  = BoardColours::Fill(described.fill, described.heat);

            if (pass == KeyPass::Legends)
            {
                DrawLegends(drawList, described, p0, p1, unit, isDimmed);
            }
            else if (pass == KeyPass::Lip)
            {
                // The face's shape, lower by the lip, behind every face: it shows only in the
                // key's own bottom gap.
                std::vector<ContourPoint> lip = ContourOnScreen(key, origin, unit, gap);
                for (ContourPoint& point : lip)
                    point.y += unit * c_Lip;
                FillContour(drawList, lip, rounding, WithStates(BoardColours::Lip(), described.marks, hovered));
            }
            else
            {
                const std::vector<ContourPoint> face = ContourOnScreen(key, origin, unit, gap);
                FillContour(drawList, face, rounding, WithStates(colours.face, described.marks, hovered));

                if (CurrentBoardStyle().keycaps == KeycapStyle::Outlined)
                {
                    const float border = std::max(1.0f, scale);
                    ImU32       colour = BoardColours::Outline();
                    if (isDimmed)
                        colour = Over(colour, BoardColours::Dimmed());
                    StrokeContour(drawList, face, rounding, border / 2, border, colour);
                }

                // Outlines nest along the contour, the first outermost, so several states read at
                // once.
                const float thickness = std::max(2.0f, unit * 0.04f);
                float       inset     = 0.0f;
                for (const auto& [mark, colour] :
                     { std::pair{ Mark::Selected, BoardColours::Selected() },
                       std::pair{ Mark::Warning, BoardColours::Warning() },
                       std::pair{ Mark::Highlighted, BoardColours::Highlighted() },
                       std::pair{ Mark::HighlightedSecond, BoardColours::HighlightedSecond() } })
                {
                    if ((described.marks & mark) == 0)
                        continue;

                    StrokeContour(drawList, face, rounding, inset + thickness / 2, thickness, colour);
                    inset += thickness;
                }
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
                                          ImDrawFlags_None,
                                          (line.marks & (Mark::Highlighted | Mark::HighlightedSecond)) != 0
                                              ? thickness * 1.5f
                                              : thickness);
            }
        }

        // Each label centred on its key nearest the edge -- a column's on its top key, a row's on
        // its leftmost, where the eye looks for it; the first of equals wins -- and nudged apart
        // so none overlaps. An average of the keys drifted: a column's keys differ in width, so
        // on a regular board C3 sat right of Esc, above nothing. A label with no keys has no
        // place and is not drawn.
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

                    bool  found = false;
                    float depth = 0.0f;   // of the nearest key so far: how far in from the edge
                    float along = 0.0f;   // and where it is along the edge
                    for (size_t key : label.keys)
                        if (key < board.keys.size())
                        {
                            const ImVec2 centre = CentreOnScreen(board, key, origin, unit);
                            const float  in     = edge == BoardEdge::Left ? centre.x : centre.y;
                            if (!found || in < depth - 0.5f)   // half a pixel: equals stay equal
                            {
                                found = true;
                                depth = in;
                                along = edge == BoardEdge::Left ? centre.y : centre.x;
                            }
                        }
                    if (!found)
                        continue;

                    const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
                    shown.push_back(index);
                    wanted.push_back(along);
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

    void SetLegendFonts(const LegendFonts& fonts)
    {
        g_LegendFonts = fonts;
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

        // The plate reaches past the keys, so it counts in the board's size.
        const ImGuiStyle& style   = ImGui::GetStyle();
        const float       scale   = style.FontScaleDpi;
        const float       width   = bounds.maxX - bounds.minX + 2 * c_PlateMargin;
        const float       height  = bounds.maxY - bounds.minY + 2 * c_PlateMargin;
        const float       avail   = ImGui::GetContentRegionAvail().x;
        const float       fitting = std::min((avail - leftMargin) / width, (maxHeight - topMargin) / height);
        const float       unit    = std::clamp(fitting, c_SmallestUnit * scale, c_MaxUnit * scale);
        const ImVec2      size(leftMargin + width * unit, topMargin + height * unit);

        // In a child of its own, which scrolls sideways when the board stops shrinking before the
        // window does -- with room for the scrollbar under it, then.
        const bool scrolls = size.x > avail;
        ImGui::BeginChild("##board", ImVec2(0.0f, size.y + (scrolls ? style.ScrollbarSize : 0.0f)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        const ImVec2 corner = ImGui::GetCursorScreenPos();
        const ImVec2 origin(corner.x + leftMargin + (c_PlateMargin - bounds.minX) * unit,
                            corner.y + topMargin + (c_PlateMargin - bounds.minY) * unit);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // The plate, always filled: it gives the board a boundary.
        drawList->AddRectFilled(ImVec2(corner.x + leftMargin, corner.y + topMargin), ImVec2(corner.x + size.x, corner.y + size.y),
                                BoardColours::Plate(), c_PlateRounding * unit);

        // Only when this window is under the mouse, so a window stacked above the board
        // neither hovers nor clicks the key beneath it.
        const bool canHover = ImGui::IsWindowHovered();

        if (CurrentBoardStyle().keycaps == KeycapStyle::BottomLip)
            for (const BoardKey& key : board.keys)
                if (!key.geometry.decal)
                    (void)DrawKey(drawList, origin, unit, key, canHover, KeyPass::Lip);

        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const BoardKey& key = board.keys[index];
            if (key.geometry.decal)
                continue;

            if (DrawKey(drawList, origin, unit, key, canHover, KeyPass::Keycap))
                events.hoveredKey = index;
        }

        if (events.hoveredKey && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            events.clickedKey = events.hoveredKey;

        DrawLines(drawList, board, origin, unit);

        for (const BoardKey& key : board.keys)
            if (!key.geometry.decal)
                (void)DrawKey(drawList, origin, unit, key, canHover, KeyPass::Legends);
        DrawEdgeLabels(drawList, board, corner, origin, unit, leftMargin, canHover, events);

        // Claim the space drawn into, so the child scrolls around the board.
        ImGui::Dummy(size);
        ImGui::EndChild();
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
        const ImU32 fill     = BoardColours::Fill(KeyFill::Alpha, 0.0f).face;

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
