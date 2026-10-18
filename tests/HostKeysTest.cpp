// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The keys the computer receives, matched to the board's (model/NazgHostKeys.h): which keycodes a
// HID usage comes from, and which keys can be tested by it at all.

#include <algorithm>
#include <cstdio>

#include "model/NazgHostKeys.h"
#include "TestSupport.h"

namespace qmk = nazg::qmk;

using nazg::c_ConsumerPage;
using nazg::c_KeyboardPage;
using nazg::HidUsage;
using nazg::Keycode;

namespace
{
    constexpr qmk::KeycodeVersion c_Version = qmk::KeycodeVersion::V0_0_7;

    bool Sends(uint32_t usage, std::string_view name, qmk::KeycodeVersion version = c_Version)
    {
        const auto names = nazg::KeycodesSending(usage, version);
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    void TestUsages()
    {
        std::printf("what the computer reports, as keycodes\n");

        Check(Sends(HidUsage(c_KeyboardPage, 0x04), "KC_A"), "the keyboard page's A is KC_A");
        Check(Sends(HidUsage(c_KeyboardPage, 0xE1), "KC_LSFT"), "a modifier");
        Check(Sends(HidUsage(c_KeyboardPage, 0x04), "KC_A", qmk::KeycodeVersion::Legacy), "the same before the renumbering");
        Check(Sends(HidUsage(c_KeyboardPage, 0x80), "KC_VOLU") && Sends(HidUsage(c_KeyboardPage, 0x80), "KC_KB_VOLUME_UP"),
              "Volume Up as the computer reports it: KC_VOLU or the keyboard page's own");
        Check(Sends(HidUsage(c_ConsumerPage, 0xCD), "KC_MPLY"), "Play/Pause on the consumer page");
        Check(nazg::KeycodesSending(HidUsage(c_KeyboardPage, 0xA5), c_Version).empty(), "past KC_EXSEL: nothing tested");
        Check(nazg::KeycodesSending(HidUsage(c_ConsumerPage, 0x04), c_Version).empty(), "an unknown consumer usage: nothing");
    }

    void TestTestedNames()
    {
        std::printf("which keys can be tested by the computer's keys\n");

        Check(nazg::TestedName(nazg::NamedKey{ "KC_A" }, c_Version) == "KC_A", "a basic key, by its own name");
        Check(nazg::TestedName(nazg::ModifiedKey{ 0x02, "KC_1" }, c_Version) == "KC_1", "a shifted key, by its key");
        Check(nazg::TestedName(nazg::ModTapKey{ 0x01, "KC_A" }, c_Version) == "KC_A", "a mod-tap, by its tap");
        Check(nazg::TestedName(nazg::LayerTapKey{ 1, "KC_SPC" }, c_Version) == "KC_SPC", "a layer-tap, by its tap");
        Check(nazg::TestedName(nazg::NamedKey{ "KC_VOLU" }, c_Version) == "KC_VOLU", "a media key");

        Check(!nazg::TestedName(nazg::NamedKey{ "KC_NO" }, c_Version), "KC_NO sends nothing");
        Check(!nazg::TestedName(nazg::NamedKey{ "KC_TRNS" }, c_Version), "nor does KC_TRNS on the base layer");
        Check(!nazg::TestedName(nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }, c_Version), "nor a layer key");
        Check(!nazg::TestedName(nazg::NamedKey{ "QK_BOOT" }, c_Version), "nor Boot");
        Check(!nazg::TestedName(nazg::MacroKey{ 0 }, c_Version), "nor a macro: what it types is not one key");
        Check(!nazg::TestedName(nazg::NamedKey{ "KC_PWR" }, c_Version), "nor a system key");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestUsages();
    TestTestedNames();
    return TestResult();
}
