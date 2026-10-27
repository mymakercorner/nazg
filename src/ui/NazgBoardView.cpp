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

        // A transparent or KC_NO key: the keycode below, faint, and its mark on a badge in the
        // face's corner (ui-design.md, "Transparent keys").
        constexpr float c_FaintStrength = 0.28f;
        // The badge of a transparent or KC_NO key: its side and its distance from the face's
        // corner, in key units, and how much of the legend colour is mixed into the face for it.
        constexpr float c_BadgeSize     = 0.24f;
        constexpr float c_BadgeInset    = 0.035f;
        constexpr float c_BadgeStrength = 0.62f;

        // A key's rotation: a turn by its angle about its origin -- clockwise on screen,
        // since y grows downward. In key units for the board's extent, in pixels for
        // drawing and hit-testing.
        struct Rotation
        {
            float  cosine = 1.0f;
            float  sine   = 0.0f;
            ImVec2 pivot;

            Rotation(const via::DefinitionKey& key, ImVec2 pivotPoint) : pivot(pivotPoint)
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
        void AddKey(Bounds& bounds, const via::DefinitionKey& key)
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

        // `colour` at `strength` of its opacity.
        ImU32 Faded(ImU32 colour, float strength)
        {
            const auto alpha = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF);
            return (colour & ~IM_COL32_A_MASK) |
                   (static_cast<ImU32>(std::lround(alpha * strength)) << IM_COL32_A_SHIFT);
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
        std::vector<ContourPoint> ContourOnScreen(const via::DefinitionKey& key, ImVec2 origin, float unit, float inset)
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

        // A command's band along the top of the face p0-p1, in its category's colour: the face's
        // top edge down to `height`, its rounded corners followed, so the band never pokes past
        // them.
        void FillBand(ImDrawList* drawList, ImVec2 p0, ImVec2 p1, float radius, float height, ImU32 colour)
        {
            constexpr float c_Pi = 3.14159265358979f;

            const float r = std::min(radius, std::min((p1.x - p0.x) / 2.0f, (p1.y - p0.y) / 2.0f));
            const float h = std::min(height, p1.y - p0.y);
            if (r < 0.5f)
            {
                drawList->AddRectFilled(p0, ImVec2(p1.x, p0.y + h), colour);
                return;
            }

            // Where the corner's arc meets the band's bottom edge, or its side when the band is
            // deeper than the corner.
            const float cut  = std::asin((r - std::min(h, r)) / r);
            const float topY = p0.y + r;
            drawList->PathArcTo(ImVec2(p0.x + r, topY), r, c_Pi + cut, 1.5f * c_Pi);
            drawList->PathArcTo(ImVec2(p1.x - r, topY), r, 1.5f * c_Pi, 2.0f * c_Pi - cut);
            if (h > r)
            {
                drawList->PathLineTo(ImVec2(p1.x, p0.y + h));
                drawList->PathLineTo(ImVec2(p0.x, p0.y + h));
            }
            drawList->PathFillConvex(colour);
        }

        // The legends of the face p0-p1 -- an L-shaped key's first rectangle -- placed by the
        // layout and clipped to the face.
        void DrawLegends(ImDrawList* drawList, const BoardKey& key, ImVec2 p0, ImVec2 p1, float unit, bool dimmed)
        {
            const ImGuiTextMeasurer   measurer(g_LegendFonts, ImGui::GetFont());
            const FaceBox             face{ p0.x, p0.y, p1.x, p1.y };
            const via::DefinitionKey& geometry = key.geometry;
            const bool oneUnit = !geometry.HasSecondRectangle() && geometry.width <= 1.0f && geometry.height <= 1.0f;

            const KeycapPrimitives primitives =
                std::holds_alternative<KeycapLegend>(key.legends)
                    ? LayOutKeycap(std::get<KeycapLegend>(key.legends), CurrentBoardStyle().legends, face, unit, oneUnit,
                                   measurer)
                    : LayOutSlots(std::get<SlotLegends>(key.legends), face, unit, measurer);

            // A header in its category's colour, solved for this face; the rest in the legend's.
            // Faint on a transparent or KC_NO key.
            const bool faint    = key.fallthrough != Fallthrough::None;
            const auto colourOf = [&](LegendInk ink, CommandCategory category)
            {
                ImU32 colour = BoardColours::Category(category, CategoryUse::Text, key.fill, key.heat)
                                   .value_or(BoardColours::Legend(ink, key.fill));
                if (dimmed)
                    colour = Over(colour, BoardColours::Dimmed());
                return faint ? Faded(colour, c_FaintStrength) : colour;
            };

            const ImVec4 clip(p0.x, p0.y, p1.x, p1.y);
            for (const PlacedText& text : primitives.texts)
                drawList->AddText(measurer.FontFor(text.weight), measurer.ImGuiSize(text.size), ImVec2(text.x, text.y),
                                  colourOf(text.ink, text.category), text.text.c_str(), nullptr, 0.0f, &clip);

            // A shaft and a filled head: every arrow alike, at the legend's weight, whatever the font.
            for (const PlacedArrow& arrow : primitives.arrows)
            {
                const ArrowShape shape  = ShapeOf(arrow);
                const ImU32      colour = colourOf(arrow.ink, CommandCategory::None);
                drawList->AddLine(ImVec2(shape.tailX, shape.tailY), ImVec2(shape.shaftEndX, shape.shaftEndY), colour,
                                  arrow.stroke);
                drawList->AddTriangleFilled(ImVec2(shape.tipX, shape.tipY), ImVec2(shape.baseAX, shape.baseAY),
                                            ImVec2(shape.baseBX, shape.baseBY), colour);
            }
        }

        // A transparent key's ▽, or a KC_NO key's ✕, on a badge: a rounded square in the face's
        // bottom right corner, solid, covering whatever legend is there -- faint anyway, and hover
        // names the key -- with the mark drawn in the face's colour (Rico, 2026-10-11). Before, the
        // marks stood alone, small and at half strength: hard to see, and the ✕ read as the × some
        // host layouts print in that corner; on a filled square it reads as a mark. No line across
        // a KC_NO key: it ran through the legends, at an angle that changed with the key's shape
        // (Rico, 2026-10-04).
        // Diagonal lines across the face, inside its contour -- an L-shaped key's too: each line kept
        // only between the contour's crossings, inside by the even-odd rule.
        void HatchContour(ImDrawList* drawList, const std::vector<ContourPoint>& contour, float spacing, float thickness,
                          ImU32 colour)
        {
            if (contour.size() < 3)
                return;
            float minX = contour[0].x, maxX = contour[0].x, minY = contour[0].y, maxY = contour[0].y;
            for (const ContourPoint& point : contour)
            {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }

            // Lines of slope -1 on screen: x + y = c, from the top-left corner to the bottom-right.
            std::vector<float> crossings;
            for (float c = minX + minY + spacing / 2; c < maxX + maxY; c += spacing)
            {
                crossings.clear();
                for (size_t index = 0; index < contour.size(); ++index)
                {
                    const ContourPoint& a = contour[index];
                    const ContourPoint& b = contour[(index + 1) % contour.size()];
                    const float         sa = a.x + a.y - c, sb = b.x + b.y - c;
                    if ((sa < 0.0f) == (sb < 0.0f))
                        continue;
                    const float t = sa / (sa - sb);
                    crossings.push_back(a.x + (b.x - a.x) * t);
                }
                std::sort(crossings.begin(), crossings.end());
                for (size_t pair = 0; pair + 1 < crossings.size(); pair += 2)
                    drawList->AddLine(ImVec2(crossings[pair], c - crossings[pair]),
                                      ImVec2(crossings[pair + 1], c - crossings[pair + 1]), colour, thickness);
            }
        }

        // The live test's "either was pressed": a ? in the face's top-right corner, in the warning's colour.
        void DrawAmbiguousMark(ImDrawList* drawList, ImVec2 p0, ImVec2 p1, float unit, bool dimmed)
        {
            ImFont*     font = ImGui::GetFont();
            const float size = std::max(10.0f * ImGui::GetStyle().FontScaleDpi, 0.28f * unit);
            const float inset = c_BadgeInset * unit;
            ImU32       ink  = BoardColours::Condition(KeyCondition::Ambiguous);
            if (dimmed)
                ink = Over(ink, BoardColours::Dimmed());
            const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "?");
            drawList->AddText(font, size, ImVec2(p1.x - inset - extent.x, p0.y + inset), ink, "?");
        }

        void DrawFallthroughMark(ImDrawList* drawList, Fallthrough fallthrough, KeyFill fill, float heat, ImVec2 p1,
                                 float unit, bool dimmed)
        {
            if (fallthrough == Fallthrough::None)
                return;

            const float scale = ImGui::GetStyle().FontScaleDpi;
            const float size  = std::max(11.0f * scale, c_BadgeSize * unit);
            const float inset = c_BadgeInset * unit;

            const BoardColours::Keycap keycap = BoardColours::Fill(fill, heat);
            ImU32 badge = Over(keycap.face, Faded(BoardColours::Legend(LegendInk::Legend, fill), c_BadgeStrength));
            ImU32 mark  = keycap.face;
            if (dimmed)
            {
                badge = Over(badge, BoardColours::Dimmed());
                mark  = Over(mark, BoardColours::Dimmed());
            }

            const ImVec2 max(p1.x - inset, p1.y - inset);
            const ImVec2 min(max.x - size, max.y - size);
            drawList->AddRectFilled(min, max, badge, 0.25f * size);

            const ImVec2 centre((min.x + max.x) / 2, (min.y + max.y) / 2);
            const float  glyph  = 0.46f * size;
            const float  stroke = std::max(1.2f * scale, 0.08f * size);
            if (fallthrough == Fallthrough::Transparent)
            {
                const float  height = glyph * 0.87f;
                const float  top    = centre.y - height / 2 + glyph * 0.02f;
                const ImVec2 left(centre.x - glyph / 2, top);
                const ImVec2 right(centre.x + glyph / 2, top);
                drawList->AddTriangle(left, right, ImVec2(centre.x, top + height), mark, stroke);
                return;
            }

            const float half = glyph * 0.42f;
            drawList->AddLine(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), mark,
                              stroke);
            drawList->AddLine(ImVec2(centre.x + half, centre.y - half), ImVec2(centre.x - half, centre.y + half), mark,
                              stroke);
        }

        // A Vial knob's direction key: ↺ or ↻ on a badge in the face's top right corner -- the
        // fallthrough badge's look, in the corner it leaves free (ui-design.md, "Knobs").
        void DrawTurnBadge(ImDrawList* drawList, bool clockwise, KeyFill fill, float heat, ImVec2 p0, ImVec2 p1,
                           float unit, bool dimmed)
        {
            const float scale = ImGui::GetStyle().FontScaleDpi;
            const float size  = std::max(11.0f * scale, c_BadgeSize * unit);
            const float inset = c_BadgeInset * unit;

            const BoardColours::Keycap keycap = BoardColours::Fill(fill, heat);
            ImU32 badge = Over(keycap.face, Faded(BoardColours::Legend(LegendInk::Legend, fill), c_BadgeStrength));
            ImU32 mark  = keycap.face;
            if (dimmed)
            {
                badge = Over(badge, BoardColours::Dimmed());
                mark  = Over(mark, BoardColours::Dimmed());
            }

            const ImVec2 min(p1.x - inset - size, p0.y + inset);
            const ImVec2 max(p1.x - inset, p0.y + inset + size);
            drawList->AddRectFilled(min, max, badge, 0.25f * size);
            DrawTurnArrow(drawList, ImVec2((min.x + max.x) / 2, (min.y + max.y) / 2), 0.28f * size, clockwise, mark,
                          std::max(1.2f * scale, 0.09f * size));
        }

        // A VIA knob's legends, stacked in its circle: the press on top, then a line per turn --
        // side by side they do not fit in 1u. The arrows share a column and the turns' keycodes
        // start at one x, so the two line up; the block is centred as one (Rico, 2026-10-13).
        void DrawKnobLegends(ImDrawList* drawList, const KnobLegend& knob, ImVec2 centre, float radius, float unit,
                             KeyFill fill, float heat, bool dimmed)
        {
            const ImGuiTextMeasurer measurer(g_LegendFonts, ImGui::GetFont());
            const float             size   = c_LetterShare * c_ModifierShare * unit;
            ImFont*                 font   = measurer.FontFor(LegendWeight::Regular);
            const float             imSize = measurer.ImGuiSize(size);
            const float             line   = size * 1.3f;

            const auto colourOf = [&](CommandCategory category)
            {
                ImU32 colour = BoardColours::Category(category, CategoryUse::Text, fill, heat)
                                   .value_or(BoardColours::Legend(LegendInk::Legend, fill));
                return dimmed ? Over(colour, BoardColours::Dimmed()) : colour;
            };
            const auto width = [&](const std::string& text) { return measurer.Width(text, size, LegendWeight::Regular); };

            const float arrowRadius = size * 0.32f;
            const float arrowWidth  = 2 * arrowRadius;
            const float gap         = size * 0.3f;
            const float block       = arrowWidth + gap + std::max(width(knob.counterClockwise.text), width(knob.clockwise.text));
            const float left        = centre.x - block / 2;

            const bool  hasPress = !knob.press.text.empty();
            const int   lines    = hasPress ? 3 : 2;
            const float top      = centre.y - lines * line / 2;

            const float  room = radius * 0.92f;
            const ImVec4 clip(centre.x - room, centre.y - room, centre.x + room, centre.y + room);
            const auto   text = [&](float x, int row, const KnobLine& legend)
            {
                const float y = top + row * line + (line - imSize) / 2;
                drawList->AddText(font, imSize, ImVec2(x, y), colourOf(legend.category), legend.text.c_str(), nullptr,
                                  0.0f, &clip);
            };

            int row = 0;
            if (hasPress)
                text(centre.x - width(knob.press.text) / 2, row++, knob.press);

            const ImU32 muted     = Faded(BoardColours::Legend(LegendInk::Legend, fill), 0.7f);
            const float thickness = std::max(1.0f, size * 0.1f);
            for (const auto& [legend, clockwise] : { std::pair{ &knob.counterClockwise, false },
                                                     std::pair{ &knob.clockwise, true } })
            {
                DrawTurnArrow(drawList, ImVec2(left + arrowRadius, top + row * line + line / 2), arrowRadius, clockwise,
                              dimmed ? Over(muted, BoardColours::Dimmed()) : muted, thickness);
                text(left + arrowWidth + gap, row++, *legend);
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
            const via::DefinitionKey& key      = described.geometry;
            const float               gap      = unit * c_KeyGap;
            const Rounding            rounding{ unit * c_KeyRounding, unit * c_InnerRounding };
            const float               scale    = ImGui::GetStyle().FontScaleDpi;

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

            // A VIA knob: a key, round -- the keycap's face, outline and states (ui-design.md, "Knobs").
            if (key.encoderIndex >= 0 && key.encoderDirection < 0)
            {
                const ImVec2 centre((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
                const float  radius = std::min(p1.x - p0.x, p1.y - p0.y) / 2;
                const bool   onKnob = inView && std::hypot(mouse.x - centre.x, mouse.y - centre.y) <= radius;

                if (pass == KeyPass::Legends)
                {
                    if (described.knob)
                        DrawKnobLegends(drawList, *described.knob, centre, radius, unit, described.fill, described.heat,
                                        isDimmed);
                    else
                        DrawLegends(drawList, described, p0, p1, unit, isDimmed);
                }
                else if (pass == KeyPass::Lip)
                {
                    drawList->AddCircleFilled(ImVec2(centre.x, centre.y + unit * c_Lip), radius,
                                              WithStates(BoardColours::Lip(), described.marks, onKnob));
                }
                else
                {
                    drawList->AddCircleFilled(centre, radius, WithStates(colours.face, described.marks, onKnob));
                    if (CurrentBoardStyle().keycaps == KeycapStyle::Outlined)
                    {
                        const float border = std::max(1.0f, scale);
                        ImU32       colour = BoardColours::Outline();
                        if (isDimmed)
                            colour = Over(colour, BoardColours::Dimmed());
                        drawList->AddCircle(centre, radius - border / 2, colour, 0, border);
                    }

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
                        drawList->AddCircle(centre, radius - inset - thickness / 2, colour, 0, thickness);
                        inset += thickness;
                    }
                }

                if (key.rotation != 0.0f)
                    for (int vertex = firstVertex; vertex < drawList->VtxBuffer.Size; ++vertex)
                        drawList->VtxBuffer[vertex].pos = rotation.Apply(drawList->VtxBuffer[vertex].pos);
                return onKnob;
            }

            if (pass == KeyPass::Legends)
            {
                DrawLegends(drawList, described, p0, p1, unit, isDimmed);
                DrawFallthroughMark(drawList, described.fallthrough, described.fill, described.heat, p1, unit, isDimmed);
                if (described.condition == KeyCondition::Ambiguous)
                    DrawAmbiguousMark(drawList, p0, p1, unit, isDimmed);
                if (key.encoderIndex >= 0)
                    DrawTurnBadge(drawList, key.encoderDirection == 1, described.fill, described.heat, p0, p1, unit, isDimmed);
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
                ImU32 faceColour = colours.face;
                if (described.condition != KeyCondition::None)
                    faceColour = Over(faceColour, BoardColours::ConditionTint(described.condition));
                FillContour(drawList, face, rounding, WithStates(faceColour, described.marks, hovered));
                if (described.hatched)
                {
                    const float hatch = std::max(1.0f, scale);
                    HatchContour(drawList, InsetContour(face, std::max(rounding.outer * 0.4f, 2 * hatch)),
                                 std::max(5.0f * scale, unit * 0.14f), hatch, Faded(BoardColours::Outline(), 0.7f));
                }

                if (CurrentBoardStyle().keycaps == KeycapStyle::Outlined)
                {
                    const float border = std::max(1.0f, scale);
                    ImU32       colour = BoardColours::Outline();
                    if (isDimmed)
                        colour = Over(colour, BoardColours::Dimmed());
                    StrokeContour(drawList, face, rounding, border / 2, border, colour);
                }

                // A command's band, with the face and under the marks, so a selected or highlighted
                // command keeps its whole outline (Rico, 2026-10-03). One per key: the hold's,
                // coloured by what the hold does, else the command's. Faint with the legends on a
                // transparent or KC_NO key.
                if (const KeycapLegend* legend = std::get_if<KeycapLegend>(&described.legends);
                    legend != nullptr && legend->Band() != CommandCategory::None)
                {
                    ImU32 band = BoardColours::Category(legend->Band(), CategoryUse::Band, described.fill, described.heat)
                                     .value_or(colours.legend);
                    if (isDimmed)
                        band = Over(band, BoardColours::Dimmed());
                    if (described.fallthrough != Fallthrough::None)
                        band = Faded(band, c_FaintStrength);
                    FillBand(drawList, p0, p1, rounding.outer, BandHeight(unit), band);
                }

                // Outlines nest along the contour, the first outermost, so several states read at
                // once -- a diagnosis outermost of all.
                const float thickness = std::max(2.0f, unit * 0.04f);
                float       inset     = 0.0f;
                if (described.condition != KeyCondition::None)
                {
                    StrokeContour(drawList, face, rounding, thickness / 2, thickness,
                                  BoardColours::Condition(described.condition));
                    inset += thickness;
                }
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

        // Each tag over the legends: its lines first, from the keys' centres to it, then the tag --
        // a rounded box in the picker's group-title colours, outlined in the highlight's.
        void DrawTags(ImDrawList* drawList, const BoardDescription& board, ImVec2 origin, float unit)
        {
            const float scale     = ImGui::GetStyle().FontScaleDpi;
            const float thickness = std::max(2.0f, unit * 0.05f);

            for (const BoardTag& tag : board.tags)
            {
                std::vector<ImVec2> centres;
                for (size_t key : tag.keys)
                    if (key < board.keys.size())
                        centres.push_back(CentreOnScreen(board, key, origin, unit));
                if (centres.empty())
                    continue;

                ImVec2 at(0.0f, 0.0f);
                for (const ImVec2& centre : centres)
                    at = ImVec2(at.x + centre.x / centres.size(), at.y + centre.y / centres.size());
                if (centres.size() == 1)
                    at.y += unit * 0.42f;   // a key alone: under its legend, not over it

                if (centres.size() > 1)
                    for (const ImVec2& centre : centres)
                        drawList->AddLine(centre, at, BoardColours::Line(Mark::Highlighted), thickness);

                const ImVec2 text = ImGui::CalcTextSize(tag.text.c_str());
                const ImVec2 half(text.x / 2 + 6 * scale, text.y / 2 + 2 * scale);
                const ImVec2 p0(at.x - half.x, at.y - half.y), p1(at.x + half.x, at.y + half.y);
                drawList->AddRectFilled(p0, p1, BoardColours::GroupLabel(), half.y);
                drawList->AddRect(p0, p1, BoardColours::Highlighted(), half.y, 0, std::max(1.0f, 1.5f * scale));
                drawList->AddText(ImVec2(at.x - text.x / 2, at.y - text.y / 2), BoardColours::GroupLabelText(),
                                  tag.text.c_str());
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

    void DrawTurnArrow(ImDrawList* drawList, ImVec2 centre, float radius, bool clockwise, ImU32 colour, float thickness)
    {
        constexpr float c_Pi    = 3.14159265358979f;
        constexpr int   c_Steps = 14;

        // Drawn clockwise -- the angle grows clockwise on screen, y pointing down -- from the upper
        // left round to the lower left; counter-clockwise is its mirror image.
        const float side = clockwise ? 1.0f : -1.0f;
        const float from = -0.85f * c_Pi;
        const float to   = 0.55f * c_Pi;
        const auto  at   = [&](float angle) { return ImVec2(centre.x + side * radius * std::cos(angle), centre.y + radius * std::sin(angle)); };

        for (int step = 0; step <= c_Steps; ++step)
            drawList->PathLineTo(at(from + (to - from) * step / c_Steps));
        drawList->PathStroke(colour, ImDrawFlags_None, thickness);

        // The head, along the arc's direction where it ends.
        const ImVec2 end     = at(to);
        const ImVec2 forward(side * -std::sin(to), std::cos(to));
        const ImVec2 outward(side * std::cos(to), std::sin(to));
        const float  head = std::max(thickness * 2.2f, radius * 0.75f);
        drawList->AddTriangleFilled(ImVec2(end.x + forward.x * head * 0.55f, end.y + forward.y * head * 0.55f),
                                    ImVec2(end.x - forward.x * head * 0.2f + outward.x * head * 0.5f,
                                           end.y - forward.y * head * 0.2f + outward.y * head * 0.5f),
                                    ImVec2(end.x - forward.x * head * 0.2f - outward.x * head * 0.5f,
                                           end.y - forward.y * head * 0.2f - outward.y * head * 0.5f),
                                    colour);
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
        // window does -- with room for the scrollbar under it, then; centred while it fits, the
        // room it leaves on both sides (Rico, 2026-10-05).
        const bool scrolls = size.x > avail;
        ImGui::BeginChild("##board", ImVec2(0.0f, size.y + (scrolls ? style.ScrollbarSize : 0.0f)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (!scrolls)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail - size.x) / 2.0f));

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
        DrawTags(drawList, board, origin, unit);
        DrawEdgeLabels(drawList, board, corner, origin, unit, leftMargin, canHover, events);

        // Claim the space drawn into, so the child scrolls around the board.
        ImGui::Dummy(size);
        ImGui::EndChild();

        events.width      = std::min(size.x, avail);
        events.fullWidth  = size.x;
        events.floorWidth = leftMargin + width * c_SmallestUnit * scale;
        events.height     = ImGui::GetItemRectSize().y;
        return events;
    }

    void DrawKeycaps(const BoardDescription& board, ImVec2 origin, float unit)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // The picker's items over the keys stay hovered while clicked: the key does too.
        const bool canHover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (CurrentBoardStyle().keycaps == KeycapStyle::BottomLip)
            for (const BoardKey& key : board.keys)
                if (!key.geometry.decal)
                    (void)DrawKey(drawList, origin, unit, key, canHover, KeyPass::Lip);

        for (const KeyPass pass : { KeyPass::Keycap, KeyPass::Legends })
            for (const BoardKey& key : board.keys)
                if (!key.geometry.decal)
                    (void)DrawKey(drawList, origin, unit, key, canHover, pass);
    }

    void DrawKeycodeTile(const KeycodeTile& tile, const FaceBox& box)
    {
        ImDrawList*  drawList = ImGui::GetWindowDrawList();
        const float  scale    = ImGui::GetStyle().FontScaleDpi * c_TileZoom;
        const ImVec2 p0(box.x0, box.y0);
        const ImVec2 p1(box.x1, box.y1);
        const float  rounding = 5.0f * scale;

        const BoardColours::Keycap colours = BoardColours::Fill(tile.fill, 0.0f);
        ImU32                      face    = colours.face;
        if (tile.hovered)
            face = Over(face, BoardColours::Hovered());
        drawList->AddRectFilled(p0, p1, face, rounding);

        const float border = std::max(1.0f, scale);
        drawList->AddRect(ImVec2(p0.x + border / 2, p0.y + border / 2), ImVec2(p1.x - border / 2, p1.y - border / 2),
                          BoardColours::Outline(), rounding, 0, border);

        const auto faint = [&](ImU32 colour) { return tile.isFaint ? Faded(colour, c_FaintStrength) : colour; };
        if (tile.band != CommandCategory::None)
            FillBand(drawList, p0, p1, rounding, c_TileBand * scale,
                     faint(BoardColours::Category(tile.band, CategoryUse::Band, tile.fill, 0.0f).value_or(colours.legend)));

        // The legends as the board draws them: a header in its category's colour, solved for this
        // face, the rest in the legend's.
        const ImGuiTextMeasurer measurer(g_LegendFonts, ImGui::GetFont());
        const KeycapPrimitives  primitives = LayOutTile(tile.legend, box, scale, measurer);
        const ImVec4            clip(p0.x, p0.y, p1.x, p1.y);
        const auto              colourOf   = [&](LegendInk ink, CommandCategory category)
        {
            return faint(BoardColours::Category(category, CategoryUse::Text, tile.fill, 0.0f)
                             .value_or(BoardColours::Legend(ink, tile.fill)));
        };
        for (const PlacedText& text : primitives.texts)
            drawList->AddText(measurer.FontFor(text.weight), measurer.ImGuiSize(text.size), ImVec2(text.x, text.y),
                              colourOf(text.ink, text.category), text.text.c_str(), nullptr, 0.0f, &clip);
        for (const PlacedArrow& arrow : primitives.arrows)
        {
            const ArrowShape shape  = ShapeOf(arrow);
            const ImU32      colour = colourOf(arrow.ink, CommandCategory::None);
            drawList->AddLine(ImVec2(shape.tailX, shape.tailY), ImVec2(shape.shaftEndX, shape.shaftEndY), colour,
                              arrow.stroke);
            drawList->AddTriangleFilled(ImVec2(shape.tipX, shape.tipY), ImVec2(shape.baseAX, shape.baseAY),
                                        ImVec2(shape.baseBX, shape.baseBY), colour);
        }

        DrawFallthroughMark(drawList, tile.mark, tile.fill, 0.0f, p1, c_SmallestUnit * scale, false);

        if (tile.selected)
        {
            const float thickness = 2.0f * scale;
            drawList->AddRect(ImVec2(p0.x + thickness / 2, p0.y + thickness / 2),
                              ImVec2(p1.x - thickness / 2, p1.y - thickness / 2), BoardColours::Selected(), rounding, 0,
                              thickness);
        }
    }

    void DrawDefinitionPreview(const via::KeyboardDefinition& definition, float width, float height)
    {
        // Choice 0 of every group a key names, whether or not the labels describe it.
        int groups = 0;
        for (const via::DefinitionKey& key : definition.keys)
            groups = std::max(groups, key.layoutIndex + 1);

        const std::vector<via::DefinitionKey> placed =
            PlaceKeys(definition, std::vector<uint8_t>(static_cast<size_t>(groups), 0));

        Bounds board;
        for (const via::DefinitionKey& key : placed)
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

        for (const via::DefinitionKey& key : placed)
        {
            if (key.decal)
                continue;

            // Rotated as the board is: drawn straight, then its vertices turned.
            const Rotation rotation(key, ImVec2(origin.x + key.rotationX * unit, origin.y + key.rotationY * unit));
            const int      firstVertex = drawList->VtxBuffer.Size;

            const ImVec2 p0(origin.x + key.x * unit + gap, origin.y + key.y * unit + gap);
            const ImVec2 p1(p0.x + key.width * unit - 2 * gap, p0.y + key.height * unit - 2 * gap);
            if (key.encoderIndex >= 0 && key.encoderDirection < 0)
                drawList->AddCircleFilled(ImVec2((p0.x + p1.x) / 2, (p0.y + p1.y) / 2),
                                          std::min(p1.x - p0.x, p1.y - p0.y) / 2, fill);   // a VIA knob, round
            else
                drawList->AddRectFilled(p0, p1, fill, rounding);

            if (key.HasSecondRectangle() && key.encoderIndex < 0)
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
