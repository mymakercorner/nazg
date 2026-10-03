// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Keycap layout: where legends go on a face, by the rules of ui-design.md, "The board's look"
// and after -- with a fake measurer whose sizes are simple, so each rule shows as a number.
// Whether real legends fit their keys, in the real font, is LegendFontTest's.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgKeycapLayout.h"

#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <string>

using nazg::ArrowDirection;
using nazg::FaceBox;
using nazg::InkExtent;
using nazg::KeycapLegend;
using nazg::KeycapPrimitives;
using nazg::LayOutKeycap;
using nazg::LegendFamily;
using nazg::LegendWeight;
using nazg::PlacedText;
using nazg::PlacementClass;

using Category = nazg::CommandCategory;

namespace
{
    // Every character half an em wide -- a little more in Bold -- and a capital from 0.2 to
    // 0.9 em below the line's top; "-" and "~" sit where fonts put them, too low and too high.
    class FakeMeasurer : public nazg::TextMeasurer
    {
    public:
        float Width(std::string_view text, float size, LegendWeight weight) const override
        {
            const auto points = std::count_if(text.begin(), text.end(), [](char c)
                                              { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
            return static_cast<float>(points) * size * (weight == LegendWeight::Bold ? 0.55f : 0.5f);
        }

        InkExtent Ink(std::string_view text, float size, LegendWeight) const override
        {
            if (text == "-")
                return { 0.55f * size, 0.65f * size };
            if (text == "~")
                return { 0.45f * size, 0.6f * size };
            return { 0.2f * size, 0.9f * size };
        }
    };

    const FakeMeasurer c_Measurer;

    constexpr float c_Unit = 100.0f;

    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 0.01f;
    }

    // A key `width` units wide at the origin, its face inset by the gap.
    FaceBox Face(float width = 1.0f, float height = 1.0f)
    {
        const float gap = nazg::c_KeyGap * c_Unit;
        return { gap, gap, width * c_Unit - gap, height * c_Unit - gap };
    }

    KeycapPrimitives Lay(const KeycapLegend& legend, LegendFamily family, float width = 1.0f)
    {
        return LayOutKeycap(legend, family, Face(width), c_Unit, width <= 1.0f, c_Measurer);
    }

    const PlacedText* Find(const KeycapPrimitives& primitives, std::string_view text)
    {
        for (const PlacedText& placed : primitives.texts)
            if (placed.text == text)
                return &placed;
        return nullptr;
    }

    float CapTop(const PlacedText& text) { return text.y + 0.2f * text.size; }
    float Baseline(const PlacedText& text) { return text.y + 0.9f * text.size; }
    float Right(const PlacedText& text) { return text.x + text.width; }

    KeycapLegend Character(const char* plain, const char* shifted = "", const char* altgr = "")
    {
        KeycapLegend legend;
        legend.placement = PlacementClass::Character;
        legend.plain     = plain;
        legend.shifted   = shifted;
        legend.altgr     = altgr;
        return legend;
    }

    nazg::Header Hold(const char* words)
    {
        return { { words, "" }, Category::Behaviour };
    }

    KeycapLegend Named(PlacementClass placement, const char* full, const char* shortForm = "",
                       nazg::Placement place = nazg::Placement::ByClass)
    {
        KeycapLegend legend;
        legend.placement   = placement;
        legend.cylindrical = { full, shortForm };
        legend.spherical   = { full, shortForm };
        legend.place       = place;
        return legend;
    }

    const float c_Box0    = Face().x0 + nazg::c_LegendPad * c_Unit;   // the legend box's left and top
    const float c_Box1    = Face().x1 - nazg::c_LegendPad * c_Unit;   // its right and bottom
    const float c_Letter  = nazg::c_LetterShare * c_Unit;
    const float c_Small   = nazg::c_ModifierShare * c_Letter;

    void TestCylindrical()
    {
        std::printf("cylindrical\n");

        const KeycapPrimitives pair = Lay(Character("1", "!"), LegendFamily::Cylindrical);
        const PlacedText*      bang = Find(pair, "!");
        const PlacedText*      one  = Find(pair, "1");
        Check(bang != nullptr && one != nullptr, "a pair is two legends");
        Check(bang && one && Near(bang->x, c_Box0) && Near(one->x, c_Box0), "both at the left, as GMK prints them");
        Check(bang && one && bang->y < one->y, "the Shift character on top");
        Check(bang && Near(bang->size, c_Letter) && bang->weight == LegendWeight::Bold, "letters at 0.30 unit, Bold");
        // The first line's room starts at the box's top; its capital is centred in it.
        Check(bang && Near(CapTop(*bang), c_Box0 + (1.12f - 0.7f) * c_Letter / 2.0f), "placed by the capital's height");

        const KeycapPrimitives del  = Lay(Named(PlacementClass::Modifier, "Del"), LegendFamily::Cylindrical);
        const PlacedText*      text = Find(del, "Del");
        Check(text && Near(text->x + text->width / 2.0f, 50.0f), "a 1u modifier is centred");
        Check(text && Near(text->size, c_Small) && text->weight == LegendWeight::Regular,
              "modifier text at 3/5 of the letters, lighter");

        const KeycapPrimitives shift = Lay(Named(PlacementClass::Modifier, "Shift"), LegendFamily::Cylindrical, 2.25f);
        const PlacedText*      left  = Find(shift, "Shift");
        Check(left && Near(left->x, c_Box0), "a wider modifier sits at the left");
        Check(left && Near(left->y + left->size * 0.55f, 50.0f), "centred vertically, by its capital");

        const KeycapPrimitives escape =
            Lay(Named(PlacementClass::Modifier, "Esc", "", nazg::Placement::MiddleLeft), LegendFamily::Cylindrical);
        Check(Find(escape, "Esc") && Near(Find(escape, "Esc")->x, c_Box0),
              "Esc breaks its class's rule: at the left, as the function row");

        KeycapLegend up;
        up.placement = PlacementClass::Arrow;
        up.arrow     = ArrowDirection::Up;
        const KeycapPrimitives arrow = Lay(up, LegendFamily::Cylindrical);
        Check(arrow.texts.empty() && arrow.arrows.size() == 1, "an arrow is drawn, not set");
        Check(arrow.arrows.size() == 1 && Near(arrow.arrows[0].x, c_Box0 + c_Letter / 2.0f),
              "arrow keys keep the top left");
    }

    void TestSpherical()
    {
        std::printf("spherical\n");

        const KeycapPrimitives pair = Lay(Character("1", "!"), LegendFamily::Spherical);
        const PlacedText*      one  = Find(pair, "1");
        Check(one && Near(one->x + one->width / 2.0f, 50.0f), "centred");

        const KeycapPrimitives shift = Lay(Named(PlacementClass::Modifier, "SHIFT"), LegendFamily::Spherical, 2.25f);
        const PlacedText*      word  = Find(shift, "SHIFT");
        Check(word && Near(word->x + word->width / 2.0f, 112.5f), "a wide modifier centred too");
        Check(word && word->weight == LegendWeight::Bold, "modifier text at the letters' weight");

        // KAT Napoleonic: the pair left of centre, the AltGr character to its right, level
        // with the lower legend.
        const KeycapPrimitives french = Lay(Character("é", "2", "~"), LegendFamily::Spherical);
        const PlacedText*      e      = Find(french, "é");
        const PlacedText*      tilde  = Find(french, "~");
        Check(e && tilde && e->x + e->width / 2.0f < 50.0f && tilde->x > Right(*e), "the pair moves left for it");
        Check(e && tilde && tilde->ink == nazg::LegendInk::Muted, "set apart by lightness");
        Check(e && tilde && Near(tilde->y + 0.6f * tilde->size, Baseline(*e)),
              "the ~ rests on the lower legend's baseline -- by its ink, not its font position");
    }

    void TestNeverScaled()
    {
        std::printf("never scaled\n");

        // 1u at 100 px: 64 px of room; modifier text 18 px, 9 px a character.
        const KeycapPrimitives two = Lay(Named(PlacementClass::Modifier, "Page Down", "Pg Dn"), LegendFamily::Cylindrical);
        Check(Find(two, "Page") && Find(two, "Down"), "too long for one line: two");

        const KeycapPrimitives shortForm =
            Lay(Named(PlacementClass::Modifier, "Backspacing Wonderfully", "Bksp"), LegendFamily::Cylindrical);
        Check(shortForm.texts.size() == 1 && Find(shortForm, "Bksp"), "too long on two: the short form");

        const KeycapPrimitives cut = Lay(Named(PlacementClass::Modifier, "Supercalifragilistic"), LegendFamily::Cylindrical);
        Check(cut.texts.size() == 1 && cut.texts[0].cut && cut.texts[0].text.size() > 3 &&
                  cut.texts[0].text.substr(cut.texts[0].text.size() - 3) == "…",
              "nothing fits: cut with ...");
        Check(cut.texts.size() == 1 && cut.texts[0].width <= c_Box1 - c_Box0, "and the cut fits");

        bool sizes = true;
        for (const KeycapPrimitives& primitives : { two, shortForm, cut })
            for (const PlacedText& text : primitives.texts)
                sizes &= Near(text.size, c_Small);
        Check(sizes, "every legend at its size: none shrunk");
    }

    void TestSecondLegends()
    {
        std::printf("second legends\n");

        const KeycapPrimitives uk   = Lay(Character("4", "$", "€"), LegendFamily::Cylindrical);
        const PlacedText*      euro = Find(uk, "€");
        Check(euro && Near(Right(*euro), c_Box1), "the AltGr character bottom right");
        Check(euro && Near(euro->size, 0.8f * c_Letter) && euro->weight == LegendWeight::Regular,
              "a little smaller than the letters");

        KeycapLegend bepo = Character("(", "4", "[");
        bepo.shiftAltgr   = "≤";
        const KeycapPrimitives bepoKey = Lay(bepo, LegendFamily::Cylindrical);
        const PlacedText*      fourth  = Find(bepoKey, "≤");
        Check(fourth && Near(Right(*fourth), c_Box1) && fourth->y < 50.0f, "the fourth level top right");
        Check(!Find(Lay(bepo, LegendFamily::Spherical), "≤"), "never on spherical sets");

        KeycapLegend seven = Named(PlacementClass::Numpad, "7");
        seven.second       = "Home";
        const KeycapPrimitives sevenKey = Lay(seven, LegendFamily::Cylindrical);
        const PlacedText*      home     = Find(sevenKey, "Home");
        Check(home && Near(home->x, c_Box0) && home->y > 50.0f, "the numpad's second legend bottom left");
        Check(!Find(Lay(seven, LegendFamily::Spherical), "Home"), "none on spherical sets");

        KeycapLegend eight = Named(PlacementClass::Numpad, "8");
        eight.secondArrow  = ArrowDirection::Up;
        const KeycapPrimitives arrow = Lay(eight, LegendFamily::Cylindrical);
        Check(arrow.arrows.size() == 1 && Near(arrow.arrows[0].x, c_Box1 - c_Letter / 2.0f), "an arrow bottom right");
        Check(arrow.arrows.size() == 1 && Near(arrow.arrows[0].size, c_Letter), "at the letters' size");
    }

    void TestHeaders()
    {
        std::printf("headers\n");

        KeycapLegend escape = Named(PlacementClass::Modifier, "Esc");
        escape.hold         = Hold("Ctrl");
        const KeycapPrimitives primitives = Lay(escape, LegendFamily::Cylindrical);
        const PlacedText*      hold       = Find(primitives, "Ctrl");
        Check(hold && Near(Right(*hold), c_Box1), "a hold top right");
        Check(hold && Near(hold->size, nazg::c_HeaderShare * c_Letter), "at half the letter size");
        Check(hold && hold->category == Category::Behaviour, "in its category's colour");
        Check(hold && hold->y < Find(primitives, "Esc")->y, "under the band, above the legends");
        const KeycapPrimitives alone   = Lay(Named(PlacementClass::Modifier, "Esc"), LegendFamily::Cylindrical);
        const PlacedText*      tap     = Find(primitives, "Esc");
        const PlacedText*      without = Find(alone, "Esc");
        Check(tap && without && tap->y == without->y && tap->x == without->x,
              "the tap's legend stays where it is without one");

        // A wide centred legend on top -- "@" under "L2" -- moves left just enough to clear the
        // header; one in the middle of the key is clear of it and stays.
        KeycapLegend wide = Character("2", "WW");
        wide.hold         = Hold("LL");
        const KeycapPrimitives moved  = Lay(wide, LegendFamily::Spherical);
        const PlacedText*      header = Find(moved, "LL");
        const PlacedText*      ww     = Find(moved, "WW");
        Check(header && ww && ww->x + ww->width / 2.0f < 50.0f, "a centred legend that would reach the header moves left");
        Check(header && ww && Near(Right(*ww), header->x - 0.04f * c_Unit), "just enough to clear it");

        KeycapLegend lone = Character("WW");
        lone.hold         = Hold("LL");
        const KeycapPrimitives middle = Lay(lone, LegendFamily::Spherical);
        Check(Find(middle, "WW") && Near(Find(middle, "WW")->x + Find(middle, "WW")->width / 2.0f, 50.0f),
              "a lone legend in the middle stays centred");

        // A long hold over a letter on 1u -- Alt Gr over W -- would reach it: the letter goes below
        // the hold. Here "Alt Gr" is 49.5 px wide, so a 16.5 px letter at the left meets it.
        KeycapLegend altGrW = Character("W");
        altGrW.hold         = Hold("Alt Gr");
        const KeycapPrimitives under   = Lay(altGrW, LegendFamily::Cylindrical);
        const PlacedText*      altGr   = Find(under, "Alt Gr");
        const PlacedText*      w       = Find(under, "W");
        Check(altGr && w && CapTop(*w) > altGr->y + altGr->size, "a letter a long hold would reach goes below it");
        Check(w && Near(w->x, c_Box0), "and keeps its column");

        // A pair under it no longer fits: its Shift character goes, rather than anything shrinking.
        KeycapLegend altGrTwo = Character("2", "@");
        altGrTwo.hold         = Hold("Alt Gr");
        const KeycapPrimitives dropped = Lay(altGrTwo, LegendFamily::Cylindrical);
        Check(Find(dropped, "2") && !Find(dropped, "@"), "a pair that no longer fits drops its Shift character");
        Check(Find(dropped, "2") && Near(Find(dropped, "2")->size, c_Letter), "the other keeps its size");
    }

    void TestCommands()
    {
        std::printf("commands\n");

        // A tap-hold whose tap is a command: two headers stacked top right, each in its colour.
        KeycapLegend play = Named(PlacementClass::Command, "Play");
        play.hold         = Hold("L1");
        play.header       = { { "Media", "" }, Category::Host };
        const KeycapPrimitives stacked = Lay(play, LegendFamily::Cylindrical);
        const PlacedText*      layer   = Find(stacked, "L1");
        const PlacedText*      media   = Find(stacked, "Media");
        const PlacedText*      action  = Find(stacked, "Play");
        Check(layer && media && Near(Right(*layer), c_Box1) && Near(Right(*media), c_Box1), "both headers right-aligned");
        Check(layer && media && Near(media->y - layer->y, nazg::c_HeaderShare * c_Letter), "the hold's on top, one line apart");
        Check(layer && media && layer->category == Category::Behaviour && media->category == Category::Host,
              "each in its own category's colour");
        Check(action && action->category == Category::None && Near(action->size, c_Small),
              "the main legend in the legend colour, at the modifier text's size");
        Check(action && media && CapTop(*action) > media->y + media->size,
              "a main legend the headers would reach goes below them");
        Check(action && Near(action->x + action->width / 2.0f, 50.0f), "centred across the key");

        // One clear of a short header stays centred on the whole key, as any key's legend.
        KeycapLegend clear = Named(PlacementClass::Command, "Play");
        clear.header       = { { "M", "" }, Category::Host };
        const KeycapPrimitives centred = Lay(clear, LegendFamily::Cylindrical);
        const KeycapPrimitives bare    = Lay(Named(PlacementClass::Command, "Play"), LegendFamily::Cylindrical);
        Check(Find(centred, "Play") && Find(bare, "Play") && Near(Find(centred, "Play")->y, Find(bare, "Play")->y),
              "a main legend clear of its header stays centred on the whole key");

        // A header too long for its line takes its short form; it never wraps.
        KeycapLegend capsWord = Named(PlacementClass::Command, "On/Off");
        capsWord.header       = { { "Caps Word Wrapping", "Caps Wd" }, Category::Behaviour };
        Check(Find(Lay(capsWord, LegendFamily::Cylindrical), "Caps Wd"), "a header's short form where its name is too long");

        // QMK's own name splits after its prefix's "_".
        const KeycapPrimitives steno = Lay(Named(PlacementClass::Command, "STN_RES1"), LegendFamily::Cylindrical);
        Check(Find(steno, "STN_") && Find(steno, "RES1"), "a QMK name splits after its prefix");
    }

    void TestInk()
    {
        std::printf("ink\n");

        const KeycapPrimitives pair  = Lay(Character("-", "_"), LegendFamily::Cylindrical);
        const PlacedText*      minus = Find(pair, "-");
        const KeycapPrimitives plain = Lay(Character("x", "_"), LegendFamily::Cylindrical);
        const PlacedText*      x     = Find(plain, "x");
        Check(minus && x && Near(minus->y + 0.6f * minus->size, x->y + 0.55f * x->size),
              "a - is centred on the capital's height, not where the font puts it");
    }

    void TestArrowShape()
    {
        std::printf("arrow shape\n");

        nazg::PlacedArrow arrow;
        arrow.x         = 50;
        arrow.y         = 50;
        arrow.size      = 20;
        arrow.direction = ArrowDirection::Up;
        const nazg::ArrowShape up = nazg::ShapeOf(arrow);
        Check(Near(up.tipX, 50) && up.tipY < 50 && up.tailY > 50, "up points up, y growing down");

        arrow.direction             = ArrowDirection::Left;
        const nazg::ArrowShape left = nazg::ShapeOf(arrow);
        Check(left.tipX < 50 && Near(left.tipY, 50), "left points left");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestCylindrical();
    TestSpherical();
    TestNeverScaled();
    TestSecondLegends();
    TestHeaders();
    TestCommands();
    TestInk();
    TestArrowShape();

    return TestResult();
}
