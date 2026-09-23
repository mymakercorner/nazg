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

using nazg::KeycapLegend;
using nazg::LegendFor;
using nazg::UsHostLayout;

namespace Mod = nazg::Mod;

namespace
{
    bool Legend(const nazg::Keycode& keycode, const char* primary, const char* secondary)
    {
        return LegendFor(keycode, UsHostLayout()) == KeycapLegend{ primary, secondary };
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

    TestNamedKeys();
    TestComposedKeys();

    return TestResult();
}
