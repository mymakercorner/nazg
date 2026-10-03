// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycapLayout.h"

#include <algorithm>
#include <cmath>

namespace nazg
{
    namespace
    {
        constexpr float c_PairLine   = 1.12f;   // line height of a letter pair, in em
        constexpr float c_WordLine   = 1.0f;    // of a wrapped name, tighter
        constexpr float c_AltGrShare = 0.8f;    // the AltGr character and the fourth level, of the letter size
        constexpr float c_BandShare  = 0.055f;  // a command's band along the top: a header sits under it
        constexpr float c_HeaderGap  = 0.02f;   // between the band and the header
        constexpr float c_Clearance  = 0.04f;   // kept between a legend and a header beside it
        constexpr float c_AltGrGap   = 0.7f;    // between a spherical pair and its AltGr character, of its size

        constexpr char c_Ellipsis[] = "…";

        // One line of a legend: text, or an arrow drawn in its place.
        struct Line
        {
            std::string    text;
            float          size       = 0.0f;
            LegendWeight   weight     = LegendWeight::Regular;
            float          lineHeight = c_PairLine;
            ArrowDirection arrow      = ArrowDirection::None;
            bool           cut        = false;

            float Height() const { return size * lineHeight; }
        };

        void PopCodePoint(std::string& text)
        {
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
                text.pop_back();
            if (!text.empty())
                text.pop_back();
        }

        size_t CodePoints(const std::string& text)
        {
            return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c)
                                                     { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        // A name as lines, never scaled (ui-design.md, "Rico's answers"): on one line, else on two
        // split at the space that makes the longer line shortest, else its short form likewise,
        // else the short form cut with "...". `width` and `height` are the room on the key.
        std::vector<Line> TextChain(const Words& words, float size, LegendWeight weight, float width, float height,
                                    const TextMeasurer& measurer)
        {
            const auto measure = [&](std::string_view text) { return measurer.Width(text, size, weight); };
            const auto line    = [&](std::string text) { return Line{ std::move(text), size, weight, c_WordLine }; };

            const auto place = [&](const std::string& text) -> std::vector<Line>
            {
                if (measure(text) <= width)
                    return { line(text) };
                if (height < 2 * size * c_WordLine)
                    return {};

                std::vector<Line> best;
                float             bestWidth = 0.0f;
                for (size_t space = text.find(' '); space != std::string::npos; space = text.find(' ', space + 1))
                {
                    const std::string first  = text.substr(0, space);
                    const std::string second = text.substr(space + 1);
                    const float       wider  = std::max(measure(first), measure(second));
                    if (wider <= width && (best.empty() || wider < bestWidth))
                    {
                        best      = { line(first), line(second) };
                        bestWidth = wider;
                    }
                }
                return best;
            };

            if (words.full.empty())
                return {};

            std::vector<Line> lines = place(words.full);
            if (lines.empty() && !words.shortForm.empty())
                lines = place(words.shortForm);
            if (!lines.empty())
                return lines;

            std::string cut = words.shortForm.empty() ? words.full : words.shortForm;
            while (CodePoints(cut) > 1 && measure(cut + c_Ellipsis) > width)
                PopCodePoint(cut);

            Line ending = line(cut + c_Ellipsis);
            ending.cut  = true;
            return { ending };
        }

        // Some glyphs look misplaced where the font puts them, so they are placed by their ink
        // against a capital's (ui-design.md, "Some glyphs are placed by their ink"): "-" sits at
        // mid x-height and "_" below the baseline, so "_ over -" looked low beside "! over 1";
        // "`" sat high. "~" rests on the baseline instead, level with the bottom of "!".
        float InkShift(const TextMeasurer& measurer, std::string_view text, float size, LegendWeight weight,
                       const InkExtent& capital)
        {
            const bool centre = text == "-" || text == "_" || text == "`";
            const bool low    = text == "~";
            if (!centre && !low)
                return 0.0f;

            const InkExtent glyph = measurer.Ink(text, size, weight);
            if (low)
                return capital.bottom - glyph.bottom;
            return (capital.top + capital.bottom) / 2.0f - (glyph.top + glyph.bottom) / 2.0f;
        }

        // The keycap's text, measured once and placed. Lines are placed by a capital's height:
        // centred in the room a line takes, whatever the font's own ascent and descent.
        class Placer
        {
        public:
            Placer(const TextMeasurer& measurer, KeycapPrimitives& out) : m_Measurer(measurer), m_Out(out) {}

            float Width(std::string_view text, float size, LegendWeight weight) const
            {
                return m_Measurer.Width(text, size, weight);
            }

            InkExtent Capital(float size, LegendWeight weight) const { return m_Measurer.Ink("H", size, weight); }

            // `text` with its capital's top at `capTop`, its left at `x`; returns the capital's
            // bottom -- the baseline, near enough.
            float AtCapTop(const std::string& text, float x, float capTop, float size, LegendWeight weight,
                           LegendInk ink, bool cut = false)
            {
                const InkExtent capital = Capital(size, weight);
                const float     top     = capTop - capital.top + InkShift(m_Measurer, text, size, weight, capital);

                m_Out.texts.push_back({ text, x, top, size, Width(text, size, weight), weight, ink, cut });
                return capTop + (capital.bottom - capital.top);
            }

            // In a line's room from `top`, `height` tall.
            float InRoom(const std::string& text, float x, float top, float height, float size, LegendWeight weight,
                         LegendInk ink, bool cut = false)
            {
                const InkExtent capital = Capital(size, weight);
                const float     capTop  = top + (height - (capital.bottom - capital.top)) / 2.0f;
                return AtCapTop(text, x, capTop, size, weight, ink, cut);
            }

            // With its capital's bottom at `baseline`.
            void AtBaseline(const std::string& text, float x, float baseline, float size, LegendWeight weight,
                            LegendInk ink)
            {
                const InkExtent capital = Capital(size, weight);
                AtCapTop(text, x, baseline - (capital.bottom - capital.top), size, weight, ink);
            }

            void Arrow(float x, float y, float size, float strokeShare, ArrowDirection direction)
            {
                m_Out.arrows.push_back({ x, y, size, std::max(1.0f, size * strokeShare), direction, LegendInk::Legend });
            }

        private:
            const TextMeasurer& m_Measurer;
            KeycapPrimitives&   m_Out;
        };
    }

    KeycapPrimitives LayOutKeycap(const KeycapLegend& legend, LegendFamily family, const FaceBox& face, float unit,
                                  bool oneUnit, const TextMeasurer& measurer)
    {
        KeycapPrimitives out;
        Placer           placer(measurer, out);

        const float   pad = c_LegendPad * unit;
        const FaceBox box{ face.x0 + pad, face.y0 + pad, face.x1 - pad, face.y1 - pad };
        const float   width  = box.x1 - box.x0;
        const float   height = box.y1 - box.y0;
        if (width <= 0.0f || height <= 0.0f)
            return out;

        const bool  spherical = family == LegendFamily::Spherical;
        const bool  command   = legend.placement == PlacementClass::Command;
        const float letter    = c_LetterShare * unit;
        const float modifier  = c_ModifierShare * letter;
        const float header    = c_HeaderShare * letter;
        const float altgr     = c_AltGrShare * letter;

        // Letters heavier than modifier text on cylindrical sets, as GMK prints them; the same
        // weight on spherical ones, as SA's single stroke (Rico, 2026-10-03).
        const LegendWeight letterWeight   = LegendWeight::Bold;
        const LegendWeight modifierWeight = spherical ? LegendWeight::Bold : LegendWeight::Regular;
        const LegendInk    altgrInk       = spherical ? LegendInk::Muted : LegendInk::Legend;

        // The header -- a hold, a modified key's modifiers, a command's -- top right, under where
        // a command's band runs: one line, its words or cut.
        const float headerTop = face.y0 + std::max(2.0f, c_BandShare * unit) + c_HeaderGap * unit;
        float       headerWidth = 0.0f;
        if (!legend.header.empty())
        {
            const Line line = TextChain({ legend.header, "" }, header, LegendWeight::Bold, width,
                                        header * c_WordLine, measurer)
                                  .front();
            headerWidth = placer.Width(line.text, header, LegendWeight::Bold);
            placer.InRoom(line.text, box.x1 - headerWidth, headerTop, line.Height(), header, LegendWeight::Bold,
                          LegendInk::Legend, line.cut);
        }

        const Words& words = spherical ? legend.spherical : legend.cylindrical;

        std::vector<Line> lines;
        switch (legend.placement)
        {
        case PlacementClass::Blank:
            break;

        // One legend or a pair, by what the host layout gives the position: on Greek the Q
        // position types ; and :. The Shift character on top.
        case PlacementClass::Character:
            if (!legend.shifted.empty())
                lines.push_back({ legend.shifted, letter, letterWeight });
            if (!legend.plain.empty())
                lines.push_back({ legend.plain, letter, letterWeight });
            break;

        case PlacementClass::Numpad:
            if (!words.full.empty())
                lines.push_back({ words.full, letter, letterWeight });
            break;

        case PlacementClass::Arrow:
            lines.push_back({ {}, letter, letterWeight, c_PairLine, legend.arrow });
            break;

        // A command's main legend has the room its header leaves.
        case PlacementClass::FunctionRow:
        case PlacementClass::Modifier:
        case PlacementClass::Command:
            lines = TextChain(words, modifier, modifierWeight, width,
                              command && !legend.header.empty() ? height - header * c_WordLine : height, measurer);
            break;
        }

        // Cylindrical: a 1u modifier centred both ways, as GMK's Delete, End and Pg Dn; wider
        // modifiers and the function row at the left, centred vertically; letters, pairs, arrows
        // and numpad digits top left -- unless the legend set says otherwise. Spherical: centred.
        // A command's main legend is centred on both, as any key's.
        const bool centred = command || spherical || legend.place == Placement::Centre ||
                             (legend.placement == PlacementClass::Modifier && oneUnit &&
                              legend.place == Placement::ByClass);
        const bool middle = !centred && (legend.placement == PlacementClass::FunctionRow ||
                                         legend.placement == PlacementClass::Modifier ||
                                         legend.place == Placement::MiddleLeft);

        float total = 0.0f;
        float widest = 0.0f;
        for (const Line& line : lines)
        {
            total += line.Height();
            if (line.arrow == ArrowDirection::None)
                widest = std::max(widest, placer.Width(line.text, line.size, line.weight));
        }

        // Legends centred on the whole key, as every key's, not in the room a header leaves.
        float y       = centred || middle ? std::max((box.y0 + box.y1 - total) / 2.0f, box.y0) : box.y0;
        float centreX = (box.x0 + box.x1) / 2.0f;

        // A command's main legend goes below its header only where it would run into it.
        if (command && !legend.header.empty())
        {
            const bool clear = (width - widest) / 2.0f >= headerWidth + c_Clearance * unit;
            if (y < headerTop + header * c_WordLine && !clear)
                y = headerTop + header * c_WordLine;
        }

        // Spherical sets with an AltGr character, as KAT Napoleonic's AZERTY and Bépo kits print
        // it: the pair moves left of centre and the AltGr character takes the right, level with
        // the lower legend. A lone legend keeps the centre, its AltGr character bottom right.
        const bool altgrBeside = spherical && !legend.altgr.empty() && lines.size() == 2;
        float      altgrX      = 0.0f;
        if (altgrBeside)
        {
            const float gap   = c_AltGrGap * altgr;
            const float left  = (box.x0 + box.x1 - (widest + gap + placer.Width(legend.altgr, altgr, LegendWeight::Regular))) / 2.0f;
            centreX           = left + widest / 2.0f;
            altgrX            = left + widest + gap;
        }

        // A centred legend that would reach the header in the corner -- a wide "@" under "L2" --
        // moves left just enough to clear it, as a pair does for an AltGr character. One lower
        // down, clear of the header's line, stays.
        if (centred && !command && !legend.header.empty() && !lines.empty() &&
            lines.front().arrow == ArrowDirection::None && y < headerTop + header * c_WordLine)
        {
            const float limit = box.x1 - headerWidth - c_Clearance * unit;
            const float half  = placer.Width(lines.front().text, lines.front().size, lines.front().weight) / 2.0f;
            if (centreX + half > limit)
                centreX = std::max(box.x0 + half, limit - half);
        }

        // Likewise a centred legend that would reach the AltGr character bottom right -- a wide Q
        // beside German's @ on the smallest board; the mockup checked US, French and Bépo only.
        const float altgrTop = box.y1 - 1.06f * altgr;
        if (centred && !altgrBeside && !legend.altgr.empty() && !lines.empty() && y + total > altgrTop)
        {
            const float limit = box.x1 - placer.Width(legend.altgr, altgr, LegendWeight::Regular) - c_Clearance * unit;
            if (centreX + widest / 2.0f > limit)
                centreX = std::max(box.x0 + widest / 2.0f, limit - widest / 2.0f);
        }

        float lastBaseline = 0.0f;
        for (const Line& line : lines)
        {
            if (line.arrow != ArrowDirection::None)
            {
                placer.Arrow(centred ? centreX : box.x0 + line.size / 2.0f, y + line.Height() / 2.0f, line.size, 0.085f,
                             line.arrow);
            }
            else
            {
                const float lineWidth = placer.Width(line.text, line.size, line.weight);
                lastBaseline = placer.InRoom(line.text, centred ? centreX - lineWidth / 2.0f : box.x0, y, line.Height(),
                                             line.size, line.weight, LegendInk::Legend, line.cut);
            }
            y += line.Height();
        }

        // The AltGr character, printed always where the host layout has one: what the key types
        // (Rico, 2026-10-03). Bottom right, as ISO keycaps print it, a little smaller than the
        // letters; beside a spherical pair, level with its lower legend.
        if (!legend.altgr.empty())
        {
            if (altgrBeside)
                placer.AtBaseline(legend.altgr, altgrX, lastBaseline, altgr, LegendWeight::Regular, altgrInk);
            else
                placer.InRoom(legend.altgr, box.x1 - placer.Width(legend.altgr, altgr, LegendWeight::Regular),
                              altgrTop, altgr * c_WordLine, altgr, LegendWeight::Regular, altgrInk);
        }

        // The fourth level, Shift+AltGr, top right -- on the host layouts that print it, and on
        // cylindrical sets only: spherical caps have no room for it. Under a header, if any, so
        // the right column reads header, fourth level, AltGr.
        if (!legend.shiftAltgr.empty() && !spherical)
        {
            const float top = legend.header.empty() ? box.y0 : headerTop + header * c_WordLine;
            placer.InRoom(legend.shiftAltgr, box.x1 - placer.Width(legend.shiftAltgr, altgr, LegendWeight::Regular), top,
                          altgr * c_WordLine, altgr, LegendWeight::Regular, LegendInk::Legend);
        }

        // The numpad's second legends, cylindrical only, as GMK prints them: text bottom left at
        // the modifier text's size; an arrow bottom right at the letters' and heavier.
        if (!spherical)
        {
            if (legend.secondArrow != ArrowDirection::None)
                placer.Arrow(box.x1 - letter / 2.0f, box.y1 - letter / 2.0f, letter, 0.11f, legend.secondArrow);
            else if (!legend.second.empty())
                placer.InRoom(legend.second, box.x0, box.y1 - modifier, modifier * c_WordLine, modifier,
                              LegendWeight::Regular, LegendInk::Legend);
        }

        return out;
    }

    KeycapPrimitives LayOutSlots(const SlotLegends& slots, const FaceBox& face, float unit, const TextMeasurer& measurer)
    {
        KeycapPrimitives out;
        Placer           placer(measurer, out);

        const float   pad = c_LegendPad * unit;
        const FaceBox box{ face.x0 + pad, face.y0 + pad, face.x1 - pad, face.y1 - pad };
        const float   size   = c_ModifierShare * c_LetterShare * unit;
        const float   line   = size * c_PairLine;
        const float   width  = box.x1 - box.x0;
        if (width <= 0.0f)
            return out;

        bool used[4] = {};
        for (size_t slot = 0; slot < c_LegendSlotCount; ++slot)
            used[slot / 3] |= !slots[slot].text.empty();

        // Top and front rows on the edges, the bottom row above the front one, the middle row
        // centred between whichever rows the key fills.
        const float top    = box.y0;
        const float front  = box.y1 - line;
        const float bottom = used[3] ? front - line : front;
        float       middle = (box.y0 + box.y1 - line) / 2.0f;
        if (used[0])
            middle = std::max(middle, top + line);
        if (used[2])
            middle = std::min(middle, bottom - line);
        const float rowTop[4] = { top, middle, bottom, front };

        for (size_t slot = 0; slot < c_LegendSlotCount; ++slot)
        {
            const Legend& legend = slots[slot];
            if (legend.text.empty())
                continue;

            const Line  fitted = TextChain({ legend.text, "" }, size, LegendWeight::Regular, width, line, measurer).front();
            const float w      = placer.Width(fitted.text, size, LegendWeight::Regular);
            const size_t column = slot % 3;
            const float  x      = column == 0 ? box.x0 : column == 1 ? (box.x0 + box.x1 - w) / 2.0f : box.x1 - w;

            placer.InRoom(fitted.text, x, rowTop[slot / 3], line, size, LegendWeight::Regular,
                          legend.role == LegendRole::Value ? LegendInk::Value : LegendInk::Legend, fitted.cut);
        }

        return out;
    }

    ArrowShape ShapeOf(const PlacedArrow& arrow)
    {
        // Along +x, then turned: right, down, left, up -- clockwise on screen, y growing down.
        const float length = arrow.size * 0.9f;
        const float head   = arrow.size * 0.36f;
        const float half   = arrow.size * 0.2f;

        float cosine = 1.0f, sine = 0.0f;
        switch (arrow.direction)
        {
        case ArrowDirection::Down:  cosine = 0.0f;  sine = 1.0f;  break;
        case ArrowDirection::Left:  cosine = -1.0f; sine = 0.0f;  break;
        case ArrowDirection::Up:    cosine = 0.0f;  sine = -1.0f; break;
        default: break;
        }

        const auto turn = [&](float x, float y, float& outX, float& outY)
        {
            outX = arrow.x + x * cosine - y * sine;
            outY = arrow.y + x * sine + y * cosine;
        };

        ArrowShape shape{};
        turn(-length / 2.0f, 0.0f, shape.tailX, shape.tailY);
        turn(length / 2.0f - head * 0.8f, 0.0f, shape.shaftEndX, shape.shaftEndY);
        turn(length / 2.0f, 0.0f, shape.tipX, shape.tipY);
        turn(length / 2.0f - head, -half, shape.baseAX, shape.baseAY);
        turn(length / 2.0f - head, half, shape.baseBX, shape.baseBY);
        return shape;
    }
}
