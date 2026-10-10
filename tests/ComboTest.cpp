// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What a combo does, as QMK's process_combo.c has it: the inputs it matches -- up to the first
// empty one -- how two combos sharing keys get along, and where the keys sending an input are.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include <cstdio>

#include "model/NazgCombo.h"
#include "TestSupport.h"

using nazg::Combo;
using nazg::ComboRelation;
using nazg::DefinitionKey;
using nazg::Keycode;
using nazg::LayerKey;
using nazg::LayerOp;
using nazg::ModTapKey;
using nazg::NamedKey;

namespace
{
    const Keycode c_J   = NamedKey{ "KC_J" };
    const Keycode c_K   = NamedKey{ "KC_K" };
    const Keycode c_L   = NamedKey{ "KC_L" };
    const Keycode c_A   = NamedKey{ "KC_A" };
    const Keycode c_Esc = NamedKey{ "KC_ESC" };

    Combo Chord(std::optional<Keycode> a, std::optional<Keycode> b, std::optional<Keycode> c = std::nullopt,
                std::optional<Keycode> output = c_Esc)
    {
        Combo combo;
        combo.inputs = { a, b, c, std::nullopt };
        combo.output = output;
        return combo;
    }

    void TestInputs()
    {
        std::printf("the inputs a combo matches\n");

        Check(Combo{}.IsEmpty(), "nothing set: an unused slot");
        Check(!Chord(std::nullopt, std::nullopt, std::nullopt, c_Esc).IsEmpty(), "an output alone is not empty");

        const Combo jk = Chord(c_J, c_K);
        Check(jk.MatchedInputs() == std::vector<Keycode>{ c_J, c_K }, "both inputs matched");
        Check(!jk.HasGap(), "no gap");

        Combo gap = Chord(c_J, std::nullopt, c_K);
        Check(gap.MatchedInputs() == std::vector<Keycode>{ c_J }, "the firmware stops at the first empty input");
        Check(gap.HasGap(), "an input after an empty one is a gap");
        gap.CloseGaps();
        Check(gap.MatchedInputs() == std::vector<Keycode>{ c_J, c_K } && !gap.HasGap(), "closed, both are matched");

        Check(Chord(std::nullopt, c_J).MatchedInputs().empty(), "the first input empty: nothing matched");
    }

    void TestRelations()
    {
        std::printf("combos sharing keys\n");

        const Combo jk  = Chord(c_J, c_K);
        const Combo kj  = Chord(c_K, c_J, std::nullopt, NamedKey{ "KC_DEL" });
        const Combo jkl = Chord(c_J, c_K, c_L);
        const Combo kl  = Chord(c_K, c_L);

        Check(RelationOf(jk, kj) == ComboRelation::SameKeys, "the same keys, in any order");
        Check(RelationOf(jk, jkl) == ComboRelation::Inside, "J+K is inside J+K+L, which wins");
        Check(RelationOf(jkl, jk) == ComboRelation::Holds, "J+K+L holds J+K");
        Check(RelationOf(jk, kl) == ComboRelation::None, "sharing K only: neither inside the other");
        Check(RelationOf(jk, Combo{}) == ComboRelation::None, "an empty slot shares nothing");
        Check(RelationOf(Chord(c_J, std::nullopt, c_K), jk) == ComboRelation::Inside,
              "a gap counts as the firmware reads it: J alone, inside J+K");
    }

    DefinitionKey Key(float x, uint8_t column)
    {
        DefinitionKey key;
        key.x      = x;
        key.column = column;
        return key;
    }

    // Three keys and two layers: J, K and a Gui-tap A on layer 0; an arrow over J on layer 1.
    void TestWhere()
    {
        std::printf("where the keys sending an input are\n");

        nazg::Keyboard keyboard;
        keyboard.definition.keys = { Key(0, 0), Key(1, 1), Key(2, 2) };
        keyboard.keymap          = nazg::Keymap(2, 1, 3);
        keyboard.keymap.Set(0, 0, 0, c_J);
        keyboard.keymap.Set(0, 0, 1, c_K);
        const Keycode guiA = ModTapKey{ nazg::Mod::LeftGui, "KC_A" };
        keyboard.keymap.Set(0, 0, 2, guiA);
        keyboard.keymap.Set(1, 0, 0, NamedKey{ "KC_LEFT" });
        keyboard.keymap.Set(1, 0, 1, NamedKey{ "KC_TRNS" });
        keyboard.keymap.Set(1, 0, 2, NamedKey{ "KC_TRNS" });

        Check(LayerSending(keyboard, c_K) == std::optional<uint8_t>(0), "K is sent on layer 0");
        Check(LayerSending(keyboard, NamedKey{ "KC_LEFT" }) == std::optional<uint8_t>(1), "Left only on layer 1");
        Check(!LayerSending(keyboard, c_A), "no key sends A itself");
        Check(TapHoldSending(keyboard, c_A) == std::optional<Keycode>(guiA),
              "but the Gui-tap key types it: the combo needs LGUI_T(KC_A)");
        Check(!TapHoldSending(keyboard, c_K), "K is sent as it is: no tap-hold to suggest");
        Check(!TapHoldSending(keyboard, LayerKey{ LayerOp::Momentary, 1 }), "only a plain key can be a tap");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestInputs();
    TestRelations();
    TestWhere();

    return TestResult();
}
