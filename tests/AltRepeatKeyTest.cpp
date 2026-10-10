// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What an alt repeat key is, as vial-qmk's vial.c and QMK's repeat_key.c have it: an unused slot,
// the last key as the firmware matches it, the keys it never remembers, what is sent wrong, two
// entries the firmware tells apart only by their order, and the pairs QMK makes itself.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include <cstdio>

#include "model/NazgAltRepeatKey.h"
#include "TestSupport.h"

using nazg::AltRepeatKey;
using nazg::Keycode;
using nazg::ModifiedKey;
using nazg::NamedKey;
namespace Mod    = nazg::Mod;
namespace Option = nazg::AltRepeatOption;

namespace
{
    const Keycode c_Z     = NamedKey{ "KC_Z" };
    const Keycode c_CtrlZ = ModifiedKey{ Mod::LeftCtrl, "KC_Z" };
    const Keycode c_CtrlY = ModifiedKey{ Mod::LeftCtrl, "KC_Y" };

    // After Ctrl+Z, Ctrl+Y, as Nazg starts one.
    AltRepeatKey UndoRedo()
    {
        AltRepeatKey entry = nazg::NewAltRepeatKey();
        entry.lastKey      = c_CtrlZ;
        entry.altKey       = c_CtrlY;
        return entry;
    }

    void TestSlot()
    {
        std::printf("an alt repeat key slot\n");

        Check(AltRepeatKey{}.IsEmpty() && !AltRepeatKey{}.IsOn(), "Vial's reset slot, all zeros: empty, off");
        AltRepeatKey optionsOnly;
        optionsOnly.options = Option::Enabled | Option::Bidirectional;
        Check(optionsOnly.IsEmpty(), "options alone: still nobody's entry");

        const AltRepeatKey fresh = nazg::NewAltRepeatKey();
        Check(fresh.IsOn() && fresh.Has(Option::IgnoreHandedness) && !fresh.Has(Option::Bidirectional) &&
                  !fresh.Has(Option::DefaultToThis),
              "a new one is on, left and right alike, one way, no default");
        Check(!UndoRedo().IsEmpty(), "Ctrl+Z then Ctrl+Y is in use");
    }

    void TestBase()
    {
        std::printf("the last key as the firmware matches it\n");

        const nazg::BaseKey modified = nazg::BaseOf(c_CtrlZ);
        Check(modified.key == c_Z && modified.mods == Mod::LeftCtrl, "Ctrl+Z: Z, Ctrl required");
        const nazg::BaseKey modTap = nazg::BaseOf(nazg::ModTapKey{ Mod::LeftGui, "KC_A" });
        Check(modTap.key == Keycode{ NamedKey{ "KC_A" } } && modTap.mods == 0, "a home-row Gui: its letter, no modifier");
        const nazg::BaseKey layerTap = nazg::BaseOf(nazg::LayerTapKey{ 1, "KC_SPC" });
        Check(layerTap.key == Keycode{ NamedKey{ "KC_SPC" } }, "a layer-tap: its tap key");
        const Keycode macro = nazg::MacroKey{ 2 };
        Check(nazg::BaseOf(macro).key == macro && nazg::BaseOf(macro).mods == 0, "anything else: itself");
    }

    void TestNeverRemembered()
    {
        std::printf("keys QMK never remembers\n");

        Check(nazg::NeverRemembered(NamedKey{ "KC_LSFT" }) && nazg::NeverRemembered(NamedKey{ "KC_RGUI" }), "modifiers");
        const uint8_t meh = Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt;
        Check(nazg::NeverRemembered(ModifiedKey{ meh, "KC_NO" }) &&
                  nazg::NeverRemembered(ModifiedKey{ static_cast<uint8_t>(meh | Mod::LeftGui), "KC_NO" }),
              "Meh and Hyper");
        Check(!nazg::NeverRemembered(ModifiedKey{ Mod::LeftCtrl, "KC_NO" }), "Ctrl with no key is not in QMK's list");
        Check(nazg::NeverRemembered(nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }) &&
                  nazg::NeverRemembered(nazg::LayerKey{ nazg::LayerOp::OneShot, 2 }),
              "MO and OSL");
        Check(!nazg::NeverRemembered(nazg::LayerKey{ nazg::LayerOp::Default, 1 }), "DF is remembered");
        Check(nazg::NeverRemembered(nazg::OneShotModKey{ Mod::LeftShift }).has_value(), "a one-shot modifier");
        Check(nazg::NeverRemembered(NamedKey{ "QK_REP" }) && nazg::NeverRemembered(NamedKey{ "QK_AREP" }) &&
                  nazg::NeverRemembered(NamedKey{ "QK_LLCK" }) && nazg::NeverRemembered(NamedKey{ "TL_LOWR" }),
              "the Repeat keys, Layer Lock, Tri Layer");
        Check(!nazg::NeverRemembered(c_Z) && !nazg::NeverRemembered(c_CtrlZ) &&
                  !nazg::NeverRemembered(nazg::ModTapKey{ Mod::LeftGui, "KC_A" }),
              "a letter, Ctrl+Z, a home-row mod");
        Check(nazg::IsRepeatKey(NamedKey{ "QK_AREP" }) && !nazg::IsRepeatKey(c_Z), "Alt Repeat cannot send a Repeat key");
    }

    void TestSentWrong()
    {
        std::printf("right-hand modifiers, sent wrong\n");

        const Keycode rightCtrlY = ModifiedKey{ Mod::RightCtrl, "KC_Y" };
        Check(nazg::SentWrong(rightCtrlY) && !nazg::SentWrong(c_CtrlY) && !nazg::SentWrong(c_Z), "only a right one");
        Check(nazg::WithLeftModifiers(rightCtrlY) == c_CtrlY, "moved to the left");
        Check(nazg::WithLeftModifiers(ModifiedKey{ Mod::RightShift | Mod::RightAlt, "KC_Y" }) ==
                  Keycode{ ModifiedKey{ Mod::LeftShift | Mod::LeftAlt, "KC_Y" } },
              "all of them");
    }

    void TestSameLastKey()
    {
        std::printf("two entries the firmware tells apart only by order\n");

        const AltRepeatKey first  = UndoRedo();
        AltRepeatKey       second = first;
        second.altKey             = ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_Z" };
        Check(nazg::SameLastKey(first, second), "Ctrl+Z twice: the first wins");

        AltRepeatKey shifted = second;
        shifted.lastKey      = ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_Z" };
        Check(!nazg::SameLastKey(first, shifted), "Ctrl+Shift+Z: more modifiers, its own");

        AltRepeatKey right = second;
        right.lastKey      = ModifiedKey{ Mod::RightCtrl, "KC_Z" };
        right.options      = Option::Enabled;
        Check(nazg::SameLastKey(first, right), "Right Ctrl+Z against Ctrl either side: both match Right Ctrl");
        AltRepeatKey left = first;
        left.options      = Option::Enabled;
        Check(!nazg::SameLastKey(left, right), "Left Ctrl against Right Ctrl, sides told apart: no overlap");

        AltRepeatKey off = second;
        off.options &= static_cast<uint8_t>(~Option::Enabled);
        Check(!nazg::SameLastKey(first, off), "one off: nothing shadowed");
    }

    void TestQmkPairs()
    {
        std::printf("the pairs QMK makes itself\n");

        Check(nazg::QmkPairs(NamedKey{ "KC_LEFT" }, NamedKey{ "KC_RGHT" }) &&
                  nazg::QmkPairs(NamedKey{ "KC_DEL" }, NamedKey{ "KC_BSPC" }),
              "Left and Right, Delete and Backspace, either order");
        Check(!nazg::QmkPairs(NamedKey{ "KC_LEFT" }, NamedKey{ "KC_UP" }) && !nazg::QmkPairs(c_Z, c_Z), "not across pairs");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestSlot();
    TestBase();
    TestNeverRemembered();
    TestSentWrong();
    TestSameLastKey();
    TestQmkPairs();

    return TestResult();
}
