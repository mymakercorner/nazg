// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What Vial's QMK Settings are, as vial-qmk's qmk_settings.c has them: each setting's width on the
// wire -- seen on the Model F 2026-10-11, where a 1-byte setting read as four bytes carries the
// request's own -- the tap-hold choice across both generations, Magic's swaps as keycode_config()
// makes them, and QMK's guess of each key's hand.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include <cstdio>

#include "model/NazgQmkSettings.h"
#include "TestSupport.h"

namespace via = nazg::via;

using nazg::HoldDecision;
using nazg::QmkSettings;

namespace QmkSetting = nazg::QmkSetting;
namespace MagicBit   = nazg::MagicBit;

namespace
{
    void TestWidths()
    {
        std::printf("each setting's width\n");

        Check(nazg::QmkSettingWidth(QmkSetting::GraveEscape) == 1 && nazg::QmkSettingWidth(QmkSetting::Tapping) == 1 &&
                  nazg::QmkSettingWidth(QmkSetting::ChordalHold) == 1,
              "bits and bit settings: one byte");
        Check(nazg::QmkSettingWidth(QmkSetting::TappingTerm) == 2 && nazg::QmkSettingWidth(QmkSetting::FlowTap) == 2 &&
                  nazg::QmkSettingWidth(QmkSetting::OneShotTimeout) == 2,
              "terms and delays: two");
        Check(nazg::QmkSettingWidth(QmkSetting::Magic) == 4, "Magic: four");
        Check(nazg::QmkSettingWidth(0) == 0 && nazg::QmkSettingWidth(28) == 0, "an id Nazg does not know: none");
    }

    void TestHoldDecision()
    {
        std::printf("the tap-hold choice\n");

        // The Model F's firmware (2023), as read 2026-10-11: qsid 8 holds 0.
        const QmkSettings old{ { QmkSetting::Tapping, 0 } };
        Check(nazg::HoldDecisionOf(old) == HoldDecision::OtherKeyPress,
              "qsid 8 cleared: Hold On Other Key Press, stored turned round");
        Check(nazg::HoldDecisionOf({ { QmkSetting::Tapping, 0x03 } }) == HoldDecision::Permissive, "bits 0 and 1: Permissive Hold");
        Check(nazg::HoldDecisionOf({ { QmkSetting::Tapping, 0x02 } }) == HoldDecision::WaitForTerm, "bit 1 alone: neither");

        const QmkSettings writes = nazg::HoldDecisionWrites({ { QmkSetting::Tapping, 0x08 } }, HoldDecision::WaitForTerm);
        Check(writes.size() == 1 && writes.at(QmkSetting::Tapping) == 0x0A, "old: one byte, Retro Tapping kept");

        const QmkSettings now{ { QmkSetting::PermissiveHold, 1 }, { QmkSetting::HoldOnOtherKey, 1 } };
        Check(nazg::HoldDecisionOf(now) == HoldDecision::OtherKeyPress, "both on: Hold On Other Key Press decides");
        const QmkSettings both = nazg::HoldDecisionWrites(now, HoldDecision::Permissive);
        Check(both.size() == 2 && both.at(QmkSetting::PermissiveHold) == 1 && both.at(QmkSetting::HoldOnOtherKey) == 0,
              "today's: two settings");
    }

    void TestMagic()
    {
        std::printf("Magic's swaps\n");

        const uint32_t caps = 1u << MagicBit::SwapControlCapsLock;
        Check(nazg::MagicKeycode("KC_CAPS", caps) == "KC_LCTL" && nazg::MagicKeycode("KC_LCTL", caps) == "KC_CAPS",
              "Caps Lock and Left Control swapped");
        Check(!nazg::MagicKeycode("KC_A", caps) && !nazg::MagicKeycode("KC_LCTL", 0), "anything else, or nothing on: itself");
        const uint32_t altGui = (1u << MagicBit::SwapLeftAltGui) | (1u << MagicBit::NoGui);
        Check(nazg::MagicKeycode("KC_LALT", altGui) == "KC_NO" && nazg::MagicKeycode("KC_LGUI", altGui) == "KC_LALT",
              "GUI off after the swap: Alt sends nothing, GUI sends Alt");
        Check(nazg::MagicKeycode("KC_RGUI", 1u << MagicBit::NoGui) == "KC_NO", "GUI off");
    }

    via::DefinitionKey Key(float x, float y, float width = 1.0f)
    {
        via::DefinitionKey key;
        key.x     = x;
        key.y     = y;
        key.width = width;
        return key;
    }

    void TestHands()
    {
        std::printf("QMK's guess of each key's hand\n");

        // A split: mirrored about its middle.
        const std::vector<char> split = nazg::GuessHands({ Key(0, 0), Key(1, 0), Key(5, 0), Key(6, 0) });
        Check(split == std::vector<char>{ 'L', 'L', 'R', 'R' }, "a split: by its middle");

        // A row and a 6.25u space bar under it: the line through the space bar's centre.
        const std::vector<char> board =
            nazg::GuessHands({ Key(0, 0), Key(1, 0), Key(5, 0), Key(9, 0), Key(10, 0), Key(3, 1, 6.25f) });
        Check(board[0] == 'L' && board[1] == 'L' && board[3] == 'R' && board[4] == 'R', "a board: either side of its space bar");
        Check(board[5] == '*', "the space bar itself: either hand");

        // No space bar, not mirrored: the widest gap.
        const std::vector<char> gap = nazg::GuessHands({ Key(0, 0), Key(1, 0), Key(2, 0), Key(6, 0), Key(7, 0) });
        Check(gap == std::vector<char>{ 'L', 'L', 'L', 'R', 'R' }, "otherwise: at the widest gap");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestWidths();
    TestHoldDecision();
    TestMagic();
    TestHands();

    return TestResult();
}
