// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Keycap legends: a Keycode seen through a host layout. The drawing that uses them is a
// first draft; the rules checked here -- positions labelled by what they type on the
// host, Shift legends on symbol keys, LSFT(kc) shown as the character it types -- are
// decided, and outlive it.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgKeycapLegend.h"

#include "TestSupport.h"
#include "adapters/qmk/NazgQmkKeycodes.h"

#include <set>
#include <string>

using nazg::HostLayout;
using nazg::KeycapLegend;
using nazg::LegendFor;
using nazg::UsHostLayout;

namespace Mod = nazg::Mod;

namespace
{
    bool Legend(const nazg::Keycode& keycode, const char* primary, const char* secondary,
                const HostLayout& layout = UsHostLayout())
    {
        return LegendFor(keycode, layout) == KeycapLegend{ primary, secondary };
    }

    // The host layout table is committed data produced by hand, so its shape is checked
    // the way the keycode table's is.
    void TestLayoutTable()
    {
        std::printf("host layout table\n");

        const auto layouts = nazg::HostLayouts();
        Check(layouts.size() == 69, "69 layouts: QMK's 72 minus plover, plover_dvorak and nordic");

        bool sorted     = true;
        bool nonEmpty   = true;
        bool uniqueKeys = true;
        bool basicKeys  = true;
        for (size_t i = 0; i < layouts.size(); ++i)
        {
            if (i > 0 && !(layouts[i - 1].id < layouts[i].id))
                sorted = false;
            if (layouts[i].legends.empty() || layouts[i].name.empty())
                nonEmpty = false;

            std::set<std::string_view> keys;
            for (const nazg::HostLegend& legend : layouts[i].legends)
            {
                uniqueKeys &= keys.insert(legend.key).second;

                // Every position must be a basic keycode, or LegendFor would never ask.
                const nazg::QmkKeycode* keycode =
                    nazg::FindQmkKeycodeByName(legend.key, nazg::c_LatestQmkKeycodeVersion);
                basicKeys &= keycode != nullptr && keycode->value <= 0xFF;
            }
        }
        Check(sorted, "sorted by id, so a list of them reads alphabetically");
        Check(nonEmpty, "every layout has a name and legends");
        Check(uniqueKeys, "no position appears twice in a layout");
        Check(basicKeys, "every position is a basic keycode");

        Check(nazg::FindHostLayout("french") != nullptr && nazg::FindHostLayout("french")->name == "French",
              "a layout is found by its QMK id");
        Check(nazg::FindHostLayout("french_mac_iso")->name == "French (Mac ISO)", "variants read naturally");
        Check(nazg::FindHostLayout("plover") == nullptr, "steno layouts are not host layouts");
        Check(nazg::FindHostLayout("klingon") == nullptr, "an unknown id finds nothing");
        Check(UsHostLayout().id == "us", "US is the default");
    }

    // AZERTY, which is where this started: positions are QWERTY's, legends are not.
    void TestFrench()
    {
        std::printf("French AZERTY\n");

        const HostLayout& french = *nazg::FindHostLayout("french");

        Check(Legend(nazg::NamedKey{ "KC_Q" }, "A", "", french), "KC_Q types A");
        Check(Legend(nazg::NamedKey{ "KC_SCLN" }, "M", "", french), "KC_SCLN types M");
        Check(Legend(nazg::NamedKey{ "KC_1" }, "&", "1", french), "the number row: & with 1 on Shift");
        Check(Legend(nazg::NamedKey{ "KC_2" }, "é", "2", french), "é, in UTF-8");
        Check(Legend(nazg::NamedKey{ "KC_LBRC" }, "^", "¨", french), "a dead key shows its accent, not a tag");
        Check(Legend(nazg::NamedKey{ "KC_NUHS" }, "*", "µ", french), "the ISO hash key");
        Check(Legend(nazg::NamedKey{ "KC_BSLS" }, "*", "µ", french), "and backslash, which the OS treats as the same key");
        Check(Legend(nazg::NamedKey{ "KC_NUBS" }, "<", ">", french), "the extra ISO key");
        Check(Legend(nazg::NamedKey{ "KC_ENT" }, "Enter", "", french), "layout-independent keys are unchanged");
        Check(Legend(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }, "1", "", french),
              "LSFT(KC_1) types 1 on AZERTY");
    }

    // The third and fourth levels are in the table, though no legend shows them yet.
    void TestAltGr()
    {
        std::printf("AltGr\n");

        const nazg::HostLegend* ukFour = nazg::FindHostLayout("uk")->Find("KC_4");
        Check(ukFour != nullptr && ukFour->altgr == "€", "UK: AltGr+4 is the euro");

        const nazg::HostLegend* frenchZero = nazg::FindHostLayout("french")->Find("KC_0");
        Check(frenchZero != nullptr && frenchZero->altgr == "@", "AZERTY: AltGr+à is @");

        const nazg::HostLegend* intlFour = nazg::FindHostLayout("us_international")->Find("KC_4");
        Check(intlFour != nullptr && intlFour->altgr == "¤" && intlFour->shiftAltgr == "£",
              "US International: Shift+AltGr is the fourth level");

        const nazg::HostLegend* macE = nazg::FindHostLayout("french_mac_iso")->Find("KC_E");
        Check(macE != nullptr && !macE->altgr.empty(), "the Mac layouts fill it from Option");

        Check(UsHostLayout().Find("KC_4")->altgr.empty(), "plain US has no AltGr level");
    }

    void TestNamedKeys()
    {
        std::printf("named keys\n");

        Check(Legend(nazg::NamedKey{ "KC_A" }, "A", ""), "a letter is its capital, with no Shift legend");
        Check(Legend(nazg::NamedKey{ "KC_1" }, "1", "!"), "a symbol key carries its Shift character too");
        Check(Legend(nazg::NamedKey{ "KC_BSLS" }, "\\", "|"), "backslash and bar");
        Check(Legend(nazg::NamedKey{ "KC_NUBS" }, "\\", "|"), "the extra ISO key types the same pair on US");
        Check(Legend(nazg::NamedKey{ "KC_ENT" }, "Enter", ""), "a layout-independent key uses QMK's label");
        Check(Legend(nazg::NamedKey{ "HF_TOGG" }, "Toggle Haptic", ""), "and so does a QMK feature key");
        Check(Legend(nazg::NamedKey{ "KC_NO" }, "", ""), "KC_NO is a blank key");
        Check(Legend(nazg::NamedKey{ "KC_TRNS" }, "Trans", ""), "KC_TRNS says so");
        Check(Legend(nazg::NamedKey{ "QK_STENO_BOLT" }, "QK_STENO_BOLT", ""),
              "a keycode QMK has since removed falls back to its name");
    }

    void TestComposedKeys()
    {
        std::printf("composed keys\n");

        Check(Legend(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }, "!", ""),
              "LSFT(KC_1) is shown as the character it types");
        Check(Legend(nazg::ModifiedKey{ Mod::RightShift, "KC_SLSH" }, "?", ""), "on either Shift");
        Check(Legend(nazg::ModifiedKey{ Mod::LeftShift, "KC_A" }, "A", "LSFT"),
              "a shifted letter has no separate character, so the modifier is shown");
        Check(Legend(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_C" }, "C", "LCTL+LSFT"),
              "other modifiers are named above the key");
        Check(Legend(nazg::LayerTapKey{ 1, "KC_SPC" }, "Spacebar", "LT 1"), "LT: the tap below, the layer above");
        Check(Legend(nazg::ModTapKey{ Mod::LeftCtrl, "KC_ESC" }, "Esc", "MT LCTL"), "MT likewise");
        Check(Legend(nazg::LayerKey{ nazg::LayerOp::Momentary, 2 }, "MO(2)", ""),
              "a layer key is shown in QMK notation");
        Check(Legend(nazg::UnknownKey{ 0x8123 }, "0x8123", ""), "an unknown value is shown as hex");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestLayoutTable();
    TestNamedKeys();
    TestComposedKeys();
    TestFrench();
    TestAltGr();

    return TestResult();
}
