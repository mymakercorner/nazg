// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Typing text with a host layout (ui/NazgHostTyping.h): the keystrokes of a character -- on a key,
// through a dead key, a dead key alone -- and what keystrokes type back. Known cases on the layouts
// of ui-design/macros-section.html, then every character of every layout typed and read back.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgHostTyping.h"

#include "TestSupport.h"

#include <set>
#include <string>
#include <vector>

using nazg::HostStroke;
using nazg::HostTypist;

namespace
{
    const nazg::HostLayout& Layout(const char* id)
    {
        const nazg::HostLayout* layout = nazg::FindHostLayout(id);
        if (!layout)
            std::printf("  no layout %s\n", id);
        return layout ? *layout : nazg::UsHostLayout();
    }

    std::vector<HostStroke> Strokes(const char* utf8, const nazg::HostLayout& layout)
    {
        std::vector<HostStroke> all;
        for (char32_t c : nazg::FromUtf8(utf8))
        {
            const auto strokes = nazg::StrokesOf(c, layout);
            if (!strokes)
                return {};
            all.insert(all.end(), strokes->begin(), strokes->end());
        }
        return all;
    }

    std::string Typed(const std::vector<HostStroke>& strokes, const nazg::HostLayout& layout)
    {
        HostTypist typist(layout);
        for (const HostStroke& stroke : strokes)
            typist.Type(stroke);
        return nazg::ToUtf8(typist.Finish());
    }

    bool Is(const std::vector<HostStroke>& strokes, std::initializer_list<HostStroke> wanted)
    {
        return strokes == std::vector<HostStroke>(wanted);
    }

    void TestKnownCharacters()
    {
        std::printf("known characters\n");

        const nazg::HostLayout& intl = Layout("us_international");
        Check(Is(Strokes("é", intl), { { "KC_E", 2 } }), "US International: é is AltGr+E, the capital's level");
        Check(Is(Strokes("É", intl), { { "KC_E", 3 } }), "É is Shift+AltGr+E");
        Check(Is(Strokes("ê", intl), { { "KC_6", 1 }, { "KC_E", 0 } }), "ê: ^ (dead, Shift+6) then E");
        Check(Is(Strokes("'", intl), { { "KC_QUOT", 0 }, { "KC_SPC", 0 } }), "': the dead ' then Space");
        Check(Is(Strokes("\"", intl), { { "KC_QUOT", 1 }, { "KC_SPC", 0 } }), "\": the dead \" then Space");

        const nazg::HostLayout& french = Layout("french");
        Check(Is(Strokes("a", french), { { "KC_Q", 0 } }), "French: a is where US has Q");
        Check(Is(Strokes("é", french), { { "KC_2", 0 } }), "é is a key of its own");
        Check(Is(Strokes("ê", french), { { "KC_LBRC", 0 }, { "KC_E", 0 } }), "ê: ^ then E");
        Check(Is(Strokes("Ê", french), { { "KC_LBRC", 0 }, { "KC_E", 1 } }), "Ê: ^ then Shift+E");

        const nazg::HostLayout& bepo = Layout("bepo");
        Check(Is(Strokes("é", bepo), { { "KC_W", 0 } }), "Bépo: é plain on KC_W");
        Check(Is(Strokes("É", bepo), { { "KC_W", 1 } }), "É with Shift");
        Check(!Strokes("Café crème, l'été même", bepo).empty(), "the mockup's sentence types on Bépo");

        const nazg::HostLayout& german = Layout("german");
        Check(Strokes("é", german).size() == 2, "German: é through the dead ´");
        Check(Is(Strokes("ß", german), { { "KC_MINS", 0 } }), "ß is a key");

        Check(Strokes("é", nazg::UsHostLayout()).empty(), "US: é cannot be typed");
        Check(Is(Strokes("A", nazg::UsHostLayout()), { { "KC_A", 1 } }), "US: A is Shift+A, though US lists no letters");
    }

    void TestReading()
    {
        std::printf("keystrokes read back\n");

        // What VIA's "l'eau" types: the US keys of l ' e a u.
        const std::vector<HostStroke> leau = { { "KC_L", 0 }, { "KC_QUOT", 0 }, { "KC_E", 0 }, { "KC_A", 0 }, { "KC_U", 0 } };
        Check(Typed(leau, nazg::UsHostLayout()) == "l'eau", "on US: l'eau");
        Check(Typed(leau, Layout("us_international")) == "léau", "on US International: léau -- ' is dead");
        Check(Typed(leau, Layout("french")) == "lùeqi" || Typed(leau, Layout("french")) == "lùequ",
              "on French: lùequ");
        Check(Typed({ { "KC_6", 1 } }, Layout("us_international")) == "^", "a dead key left pending types itself");
        Check(Typed({ { "KC_6", 1 }, { "KC_Q", 0 } }, Layout("us_international")) == "^q",
              "a dead key and a letter that do not compose: both");
    }

    void TestCase()
    {
        std::printf("letters' case\n");
        Check(nazg::LowerOf(U'É') == U'é' && nazg::LowerOf(U'Ç') == U'ç', "Latin-1");
        Check(nazg::LowerOf(U'Ł') == U'ł' && nazg::LowerOf(U'Ž') == U'ž' && nazg::LowerOf(U'Ő') == U'ő', "Latin Extended-A");
        Check(nazg::LowerOf(U'Ж') == U'ж' && nazg::LowerOf(U'Є') == U'є' && nazg::LowerOf(U'Ω') == U'ω',
              "Cyrillic and Greek");
        Check(nazg::LowerOf(U'×') == U'×' && nazg::LowerOf(U'é') == U'é', "neither a capital: unchanged");
        Check(nazg::ToUtf8(nazg::FromUtf8("l'été ‘€’")) == "l'été ‘€’", "UTF-8 there and back");
    }

    // Every character every layout types -- on a key, or alone from a dead key, or composed from a
    // dead key and a letter -- found, typed, and read back the same.
    void TestEveryLayout()
    {
        std::printf("every layout\n");

        size_t characters = 0, composed = 0, failures = 0;
        for (const nazg::HostLayout& layout : nazg::HostLayouts())
        {
            std::set<char32_t> wanted;
            for (const nazg::HostLegend& legend : layout.legends)
                for (uint8_t level = 0; level < 4; ++level)
                    if (const auto typed = nazg::TypedBy({ legend.key, level }, layout))
                    {
                        if (!typed->dead)
                            wanted.insert(typed->character);
                        else if (const char32_t alone = nazg::AloneOf(typed->character, layout))
                            wanted.insert(alone);
                    }
            for (const nazg::Composition& composition : nazg::Compositions())
                if (nazg::StrokesOf(composition.composed, layout))
                {
                    wanted.insert(composition.composed);
                    ++composed;
                }

            for (char32_t c : wanted)
            {
                if (c == U' ')
                    continue;
                const auto strokes = nazg::StrokesOf(c, layout);
                const std::u32string one(1, c);
                if (!strokes || Typed(*strokes, layout) != nazg::ToUtf8(one))
                {
                    if (++failures <= 12)
                        std::printf("  %.*s: U+%04X %s\n", static_cast<int>(layout.id.size()), layout.id.data(),
                                    static_cast<unsigned>(c), strokes ? "reads back otherwise" : "has no keystrokes");
                }
                ++characters;
            }
        }
        std::printf("  %zu characters on %zu layouts, %zu of them composed through dead keys\n", characters,
                    nazg::HostLayouts().size(), composed);
        Check(failures == 0, "every character types and reads back the same");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestKnownCharacters();
    TestReading();
    TestCase();
    TestEveryLayout();
    return TestResult();
}
