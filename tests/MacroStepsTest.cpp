// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A macro's steps to a board's actions and back (ui/NazgMacroSteps.h): text through the host
// layout, keys sent with modifiers, presses and releases, waits -- in each of the four formats, the
// bytes included; text other apps wrote; and every character of every layout, through every format.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgHostTyping.h"
#include "ui/NazgMacroSteps.h"

#include "TestSupport.h"

#include <string>
#include <vector>

using nazg::Macro;
using nazg::MacroAction;
using nazg::MacroActions;
using nazg::MacroFormat;
using nazg::MacroStep;
using Kind = nazg::MacroAction::Kind;

namespace
{
    const nazg::QmkKeycodeVersion c_Version = nazg::c_LatestQmkKeycodeVersion;

    const nazg::HostLayout& Layout(const char* id)
    {
        const nazg::HostLayout* layout = nazg::FindHostLayout(id);
        return layout ? *layout : nazg::UsHostLayout();
    }

    MacroStep Text(const char* text) { return { MacroStep::Kind::Text, text }; }
    MacroStep Key(nazg::Keycode key) { return { MacroStep::Kind::Key, {}, key }; }
    MacroStep Press(const char* key) { return { MacroStep::Kind::Press, {}, nazg::NamedKey{ key } }; }
    MacroStep Release(const char* key) { return { MacroStep::Kind::Release, {}, nazg::NamedKey{ key } }; }
    MacroStep Wait(uint32_t ms) { return { MacroStep::Kind::Wait, {}, nazg::NamedKey{ "KC_NO" }, ms }; }

    // Steps to actions to bytes, and back.
    Macro ThroughBoard(const Macro& macro, const nazg::MacroContext& context, std::vector<nazg::MacroProblem>* problems = nullptr)
    {
        const nazg::MacroWriting writing = nazg::ActionsOf(macro, context);
        if (problems)
            *problems = writing.problems;
        const std::vector<uint8_t> bytes = nazg::EncodeMacros({ writing.actions }, context.format);
        const std::vector<MacroActions> read = nazg::DecodeMacros(bytes, 1, context.format);
        return nazg::StepsOf(read.empty() ? MacroActions{} : read[0], context);
    }

    void TestRoundTrips()
    {
        std::printf("round trips\n");

        const Macro withWaits = { Key(nazg::ModifiedKey{ nazg::Mod::LeftGui, "KC_R" }), Wait(300), Text("cmd"),
                                  Key(nazg::NamedKey{ "KC_ENT" }), Press("KC_LALT"), Key(nazg::NamedKey{ "KC_TAB" }),
                                  Key(nazg::NamedKey{ "KC_TAB" }), Release("KC_LALT"), Text("Café crème, l'été même") };
        const Macro noWaits(withWaits.begin() + 2, withWaits.end());

        for (const char* host : { "us_international", "french", "bepo", "german" })
            for (MacroFormat format : { MacroFormat::VialExtended, MacroFormat::VialBasic, MacroFormat::Via, MacroFormat::Unprefixed })
            {
                const nazg::MacroContext context{ format, c_Version, Layout(host) };
                const Macro& macro = nazg::HasWaits(format) ? withWaits : noWaits;
                std::vector<nazg::MacroProblem> problems;
                const bool same = ThroughBoard(macro, context, &problems) == macro && problems.empty();
                const std::string what = std::string(host) + ", format " + std::to_string(static_cast<int>(format)) +
                                         ": the macro reads back as written";
                Check(same, what.c_str());
            }
    }

    void TestStoredForm()
    {
        std::printf("what is stored\n");

        const nazg::MacroContext french{ MacroFormat::Via, c_Version, Layout("french") };
        Check(nazg::ActionsOf({ Text("a") }, french).actions == MacroActions{ { Kind::Character, 'q' } },
              "French a: the byte q, the US character on that key");

        const nazg::MacroContext intlVial{ MacroFormat::VialExtended, c_Version, Layout("us_international") };
        Check(nazg::ActionsOf({ Text("é") }, intlVial).actions == MacroActions{ { Kind::Tap, 0x1408 } },
              "US International é on Vial: one tap of RALT(KC_E), 0x1408");

        const nazg::MacroContext intlVia{ MacroFormat::Via, c_Version, Layout("us_international") };
        Check(nazg::ActionsOf({ Text("é") }, intlVia).actions ==
                  MacroActions{ { Kind::Press, 0xE6 }, { Kind::Tap, 0x08 }, { Kind::Release, 0xE6 } },
              "on VIA: Right Alt pressed around E");
        Check(nazg::ActionsOf({ Text("'") }, intlVia).actions ==
                  MacroActions{ { Kind::Character, '\'' }, { Kind::Character, ' ' } },
              "' on US International: the dead ' then Space");

        Check(nazg::ActionsOf({ Key(nazg::ModifiedKey{ nazg::Mod::LeftGui, "KC_R" }) }, intlVia).actions ==
                  MacroActions{ { Kind::Press, 0xE3 }, { Kind::Tap, 0x15 }, { Kind::Release, 0xE3 } },
              "Win+R on VIA: the chord");
        Check(nazg::ActionsOf({ Key(nazg::ModifiedKey{ nazg::Mod::LeftGui, "KC_R" }) }, intlVial).actions ==
                  MacroActions{ { Kind::Tap, 0x0815 } },
              "on Vial: one tap of LGUI(KC_R)");

        const Macro best = { Text("Best regards,") };
        Check(nazg::BytesOf(nazg::ActionsOf(best, french).actions, MacroFormat::Via) == 14,
              "Best regards, on French: 13 bytes and the end, as plain characters would");
    }

    void TestReading()
    {
        std::printf("what other apps wrote\n");

        MacroActions leau;
        for (char c : std::string("l'eau, cmd"))
            leau.push_back({ Kind::Character, static_cast<uint8_t>(c) });
        leau.push_back({ Kind::Character, '\n' });

        const nazg::MacroContext intl{ MacroFormat::Via, c_Version, Layout("us_international") };
        Check(nazg::StepsOf(leau, intl) == Macro{ Text("léau, cmd"), Key(nazg::NamedKey{ "KC_ENT" }) },
              "VIA's l'eau, cmd reads léau, cmd on US International; a newline is Enter");
        const nazg::MacroContext french{ MacroFormat::Via, c_Version, Layout("french") };
        Check(nazg::StepsOf(leau, french)[0] == Text("lùequ; c,d"), "and lùequ; c,d on French");

        // VIA's chord {KC_LCTL,KC_C}: press, press, release, release -- one key sent with Ctrl.
        const MacroActions chord = { { Kind::Press, 0xE0 }, { Kind::Press, 0x06 }, { Kind::Release, 0x06 }, { Kind::Release, 0xE0 } };
        Check(nazg::StepsOf(chord, intl) == Macro{ Key(nazg::ModifiedKey{ nazg::Mod::LeftCtrl, "KC_C" }) },
              "VIA's chord Ctrl+C is one key");

        // Presses that cross stay presses and releases.
        const MacroActions crossed = { { Kind::Press, 0xE0 }, { Kind::Press, 0xE1 }, { Kind::Tap, 0x29 },
                                       { Kind::Release, 0xE0 }, { Kind::Release, 0xE1 } };
        const Macro steps = nazg::StepsOf(crossed, intl);
        Check(steps.size() == 5 && steps[0].kind == MacroStep::Kind::Press && steps[3].kind == MacroStep::Kind::Release,
              "crossed presses and releases are read as they are");
        const std::vector<size_t> partner = nazg::PairPressesAndReleases(steps);
        Check(partner[0] == 3 && partner[1] == 4 && partner[2] == 2, "Ctrl pairs with its release, Shift with its own");
        const std::vector<size_t> lone = nazg::PairPressesAndReleases({ Press("KC_LSFT"), Text("abc") });
        Check(lone[0] == 0, "a press never released pairs with nothing");
    }

    void TestProblems()
    {
        std::printf("what a board cannot store\n");

        const nazg::MacroContext us{ MacroFormat::Via, c_Version, nazg::UsHostLayout() };
        const nazg::MacroWriting accents = nazg::ActionsOf({ Text("Café crème") }, us);
        Check(accents.problems.size() == 1 && accents.problems[0].what.rfind("éè cannot be typed", 0) == 0,
              "é and è on US: named once each");

        const nazg::MacroWriting layer = nazg::ActionsOf({ Key(nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }) }, us);
        Check(layer.problems.size() == 1, "MO(1) in a VIA macro: basic keys only");
        const nazg::MacroContext vial{ MacroFormat::VialExtended, c_Version, nazg::UsHostLayout() };
        Check(nazg::ActionsOf({ Key(nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }) }, vial).problems.empty(),
              "on Vial 5 and later, any keycode");

        const nazg::MacroContext old{ MacroFormat::Unprefixed, c_Version, nazg::UsHostLayout() };
        Check(nazg::ActionsOf({ Wait(100) }, old).problems.size() == 1, "no waits before VIA 11");
        Check(nazg::ActionsOf({ Wait(10000) }, us).problems.size() == 1, "VIA waits to 9 999 ms");
    }

    void TestEveryLayout()
    {
        std::printf("every layout, every format\n");

        size_t failures = 0, checked = 0;
        for (const nazg::HostLayout& layout : nazg::HostLayouts())
        {
            std::u32string all;
            for (const nazg::HostLegend& legend : layout.legends)
                for (uint8_t level = 0; level < 4; ++level)
                    if (const auto typed = nazg::TypedBy({ legend.key, level }, layout); typed && !typed->dead &&
                        typed->character != U' ' && all.find(typed->character) == std::u32string::npos)
                        all += typed->character;
            const Macro macro = { Text(nazg::ToUtf8(all).c_str()) };

            for (MacroFormat format : { MacroFormat::VialExtended, MacroFormat::Via, MacroFormat::Unprefixed })
            {
                const nazg::MacroContext context{ format, c_Version, layout };
                std::vector<nazg::MacroProblem> problems;
                if (ThroughBoard(macro, context, &problems) != macro || !problems.empty())
                {
                    if (++failures <= 6)
                        std::printf("  %.*s, format %d: differs\n", static_cast<int>(layout.id.size()), layout.id.data(),
                                    static_cast<int>(format));
                }
                ++checked;
            }
        }
        std::printf("  %zu layouts and formats\n", checked);
        Check(failures == 0, "every layout's characters, written in every format, read back the same");
    }
}

namespace
{
    void TestScript()
    {
        std::printf("VIA's script\n");
        const Macro runCmd = { Key(nazg::ModifiedKey{ nazg::Mod::LeftGui, "KC_R" }), Wait(300), Text("cmd{x}"),
                               Key(nazg::NamedKey{ "KC_ENT" }) };
        Check(nazg::ViaScriptOf(runCmd) == "{KC_LGUI,KC_R}{300}cmd\\{x}{KC_ENT}", "Win+R, a wait, text, Enter");
        Check(nazg::ViaScriptOf({ Press("KC_LALT"), Key(nazg::NamedKey{ "KC_TAB" }), Release("KC_LALT") }) ==
                  "{+KC_LALT}{KC_TAB}{-KC_LALT}",
              "a press and a release");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestScript();
    TestRoundTrips();
    TestStoredForm();
    TestReading();
    TestProblems();
    TestEveryLayout();
    return TestResult();
}
