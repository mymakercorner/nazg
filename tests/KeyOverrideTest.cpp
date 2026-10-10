// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What a key override is, as QMK's process_key_override.c and vial-qmk's vial.c have it: an
// unused slot, the options read with none set as all three, what can be sent, two slots the
// firmware tells apart only by their order -- and where the key is, on the override's layers.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include <cstdio>

#include "model/NazgCombo.h"
#include "model/NazgKeyOverride.h"
#include "TestSupport.h"

using nazg::DefinitionKey;
using nazg::KeyOverride;
using nazg::Keycode;
using nazg::NamedKey;
namespace Option = nazg::KeyOverrideOption;

namespace
{
    const Keycode c_Bksp = NamedKey{ "KC_BSPC" };
    const Keycode c_Del  = NamedKey{ "KC_DEL" };
    const Keycode c_Ins  = NamedKey{ "KC_INS" };

    // Shift + Backspace for Delete, Shift hidden: the classic, as Nazg starts one.
    KeyOverride ShiftBackspace()
    {
        KeyOverride keyOverride = nazg::NewKeyOverride();
        keyOverride.trigger     = c_Bksp;
        keyOverride.replacement = c_Del;
        keyOverride.held        = nazg::Mod::LeftShift | nazg::Mod::RightShift;
        keyOverride.hidden      = keyOverride.held;
        return keyOverride;
    }

    void TestSlot()
    {
        std::printf("a key override slot\n");

        Check(KeyOverride{}.IsEmpty() && !KeyOverride{}.IsOn(), "Vial's reset slot, all zeros: empty, off");
        KeyOverride layersOnly;
        layersOnly.layers  = 0xFFFF;
        layersOnly.options = Option::Enabled;
        Check(layersOnly.IsEmpty(), "layers and options alone: still nobody's override");

        const KeyOverride fresh = nazg::NewKeyOverride();
        Check(fresh.IsOn() && fresh.layers == nazg::c_AllKeyOverrideLayers && fresh.Activations() == Option::Activations,
              "a new one is on, on every layer, with QMK's usual options");

        KeyOverride none;
        Check(none.Activations() == Option::Activations, "no activation set reads as all three");
        none.options = Option::TriggerDown;
        Check(none.Activations() == Option::TriggerDown, "one set is that one alone");

        Check(ShiftBackspace().IsOn() && !ShiftBackspace().IsEmpty(), "Shift + Backspace is in use");
    }

    void TestCanBeSent()
    {
        std::printf("what can be sent\n");

        Check(nazg::CanBeSent(0x004C), "Delete, a basic key");
        Check(nazg::CanBeSent(0x00A9), "Volume Up, a media key below 0x100");
        Check(nazg::CanBeSent(0x011C), "Ctrl+Y: the modifiers go as weak mods");
        Check(!nazg::CanBeSent(0x0000) && !nazg::CanBeSent(0x0001), "nothing, and transparent, are no key");
        Check(!nazg::CanBeSent(0x0100), "Ctrl with no key: nothing to register");
        Check(!nazg::CanBeSent(0x5221), "MO(1): register_code() takes 8 bits");
        Check(!nazg::CanBeSent(0x7C00), "Boot neither");
    }

    void TestSameRule()
    {
        std::printf("two slots the firmware tells apart only by order\n");

        const KeyOverride first = ShiftBackspace();
        KeyOverride       second = first;
        second.replacement        = c_Ins;
        Check(nazg::SameRule(first, second, 0x000F), "same key, same modifiers held: only the first plays");

        KeyOverride elsewhere = second;
        elsewhere.layers      = 0x0010;
        Check(!nazg::SameRule(first, elsewhere, 0x000F) && nazg::SameRule(first, elsewhere, 0x001F),
              "no layer in common on the board: both play");

        KeyOverride alt = second;
        alt.held        = nazg::Mod::LeftAlt | nazg::Mod::RightAlt;
        Check(!nazg::SameRule(first, alt, 0x000F), "other modifiers: both play");

        KeyOverride off = second;
        off.options &= static_cast<uint8_t>(~Option::Enabled);
        Check(!nazg::SameRule(first, off, 0x000F), "one off: nothing shadowed");
    }

    DefinitionKey Key(float x, uint8_t column)
    {
        DefinitionKey key;
        key.x      = x;
        key.column = column;
        return key;
    }

    // Backspace on layer 0, Insert on layer 1 over it.
    void TestWhere()
    {
        std::printf("where the key is, on the override's layers\n");

        nazg::Keyboard keyboard;
        keyboard.definition.keys = { Key(0, 0) };
        keyboard.keymap          = nazg::Keymap(2, 1, 1);
        keyboard.keymap.Set(0, 0, 0, c_Bksp);
        keyboard.keymap.Set(1, 0, 0, c_Ins);

        Check(LayerSending(keyboard, c_Ins) == std::optional<uint8_t>(1), "every layer: Insert on layer 1");
        Check(!LayerSending(keyboard, c_Ins, 0x0001), "layer 0 only: no key sends Insert");
        Check(LayerSending(keyboard, c_Bksp, 0x0002) == std::nullopt, "layer 1 only: Backspace is not there");
        Check(LayerSending(keyboard, c_Bksp, 0x0001) == std::optional<uint8_t>(0), "layer 0: Backspace");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestSlot();
    TestCanBeSent();
    TestSameRule();
    TestWhere();

    return TestResult();
}
