// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VIA's saved layout (.layout.json): keycodes spelled with VIA's own tables, macros as VIA's
// expressions, files as the VIA app writes them read into the model.
// docs/research_material/via-layout-files.md.

#include <cstdio>
#include <stdexcept>
#include <string>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeycodeNames.h"
#include "adapters/via/NazgViaSavedLayout.h"
#include "TestSupport.h"

namespace via = nazg::via;
namespace qmk = nazg::qmk;

namespace
{
    using via::MacroAction;
    using via::NameTable;

    // What a keycode of the model is on a board of `version`.
    std::optional<uint16_t> Value(const std::optional<nazg::Keycode>& key,
                                  qmk::KeycodeVersion version = qmk::KeycodeVersion::V0_0_7)
    {
        return key ? qmk::EncodeKeycode(*key, version) : std::nullopt;
    }

    void TestTables()
    {
        std::printf("the table VIA uses, by protocol or keycode version\n");

        Check(via::NameTableFor(uint16_t{ 9 }) == NameTable::V9, "protocol 9: default.ts");
        Check(via::NameTableFor(uint16_t{ 10 }) == NameTable::V10, "protocol 10");
        Check(via::NameTableFor(uint16_t{ 11 }) == NameTable::V11, "protocol 11");
        Check(via::NameTableFor(uint16_t{ 13 }) == NameTable::V12, "protocol 13 reads with v12 and v13's names");
        Check(via::NameTableFor(qmk::KeycodeVersion::Legacy) == NameTable::V9, "a Vial board before the renumbering");
        Check(via::NameTableFor(qmk::KeycodeVersion::V0_0_7) == NameTable::V12, "a Vial board after it");
    }

    void TestNames()
    {
        std::printf("keycodes spelled as the VIA app spells them\n");

        Check(via::ReadKeycodeName("KC_A", NameTable::V12) == 0x0004, "a name of the table");
        Check(via::ReadKeycodeName("KC_SLCK", NameTable::V12) == 0x0047, "VIA's own spelling, not QMK's KC_SCRL");
        Check(via::ReadKeycodeName("MI_C", NameTable::V12) == 0x7110, "VIA's MIDI value, not QMK's 0x7103");
        Check(!via::ReadKeycodeName("MI_C", NameTable::V9), "a name the table lacks");
        Check(via::ReadKeycodeName("UG_TOGG", NameTable::V12) == 0x7820 && via::ReadKeycodeName("RGB_TOG", NameTable::V12) == 0x7820,
              "v13's names and v12's, one value");

        Check(via::ReadKeycodeName("MO(1)", NameTable::V9) == 0x5101, "a layer key before the renumbering");
        Check(via::ReadKeycodeName("MO(1)", NameTable::V12) == 0x5221, "and after");
        Check(via::ReadKeycodeName("TO(1)", NameTable::V9) == 0x5011, "default.ts's TO, with QMK's old ON_PRESS bit");
        Check(via::ReadKeycodeName("TO(1)", NameTable::V10) == 0x5001, "v10's without");
        Check(via::ReadKeycodeName("mo(1)", NameTable::V12) == 0x5221, "lower case read");
        Check(!via::ReadKeycodeName("MO(99)", NameTable::V12), "a layer past the range");
        Check(via::ReadKeycodeName("MACRO(3)", NameTable::V12) == 0x7703, "a macro key, v12");
        Check(via::ReadKeycodeName("MACRO(3)", NameTable::V11) == 0x7705, "v11's VIA-specific numbering");
        Check(via::ReadKeycodeName("CUSTOM(0)", NameTable::V12) == 0x7E00, "a custom keycode");
        Check(via::ReadKeycodeName("CUSTOM(0)", NameTable::V11) == 0x7F00, "v11's");

        Check(via::ReadKeycodeName("MACRO03", NameTable::V12) == 0x7703, "a renamed macro key");
        Check(via::ReadKeycodeName("USER01", NameTable::V12) == 0x7E01, "a renamed user key");
        Check(via::ReadKeycodeName("SPC_FN1", NameTable::V12) == 0x412C, "SPC_FN1, LT(1,KC_SPC)");
        Check(via::ReadKeycodeName("KC_TILD", NameTable::V12) == 0x0235, "KC_TILD, S(KC_GRV)");

        Check(via::ReadKeycodeName("S(KC_1)", NameTable::V12) == 0x021E, "a shifted key");
        Check(via::ReadKeycodeName("LCTL(LSFT(KC_A))", NameTable::V12) == 0x0304, "modifiers nested");
        Check(via::ReadKeycodeName("HYPR(KC_A)", NameTable::V12) == 0x0F04, "a combined modifier name");
        Check(via::ReadKeycodeName("RCS(KC_A)", NameTable::V12) == 0x1304, "right-hand ones");
        Check(via::ReadKeycodeName("LT(1,KC_SPC)", NameTable::V12) == 0x412C, "a layer tap");
        Check(via::ReadKeycodeName("MT(MOD_LCTL | MOD_LSFT,KC_A)", NameTable::V12) == 0x2304, "a mod-tap, v12");
        Check(via::ReadKeycodeName("MT(MOD_LCTL | MOD_LSFT,KC_A)", NameTable::V9) == 0x6304, "and before the renumbering");
        Check(via::ReadKeycodeName("OSM(MOD_LSFT)", NameTable::V12) == 0x52A2, "a one-shot modifier");
        Check(via::ReadKeycodeName("LM(1,MOD_LCTL)", NameTable::V12) == 0x5021, "a layer-mod, the layer above 5 bits");
        Check(via::ReadKeycodeName("LM(1,MOD_LCTL)", NameTable::V9) == 0x5911, "above 4 before the renumbering");

        Check(via::ReadKeycodeName("0x7c40", NameTable::V12) == 0x7C40, "VIA's hex, which VIA itself loses");
        Check(!via::ReadKeycodeName("LT(1,undefined)", NameTable::V12), "what VIA writes for a key it cannot name");
        Check(!via::ReadKeycodeName("LT(16,KC_A)", NameTable::V12), "a layer tap past layer 15");
        Check(!via::ReadKeycodeName("", NameTable::V12), "the empty string");
        Check(!via::ReadKeycodeName("KC_WHATEVER", NameTable::V12), "an unknown name");
        Check(!via::ReadKeycodeName("S(MO(1))", NameTable::V12), "a modifier around more than a basic key");
    }

    // A file as the VIA app writes one: 2 layers of 2 by 3, one knob, three macros.
    const char* c_ViaFile = R"via({"name": "Test Board", "vendorProductId": 1380974593,
        "macros": ["Hi{KC_ENT}{100}{KC_LCTL,KC_C}{+KC_LSFT}a{-KC_LSFT}\\{x", "", "{KC_NOPE}"],
        "layers": [["KC_ESC", "LT(1,KC_SPC)", "MACRO(0)", "S(KC_1)", "0x7c40", "KC_WHATEVER"],
                   ["KC_TRNS", "MO(1)", "KC_NO", "MT(MOD_LCTL | MOD_LSFT,KC_A)", "CUSTOM(0)", "KC_SLCK"]],
        "encoders": [[["KC_VOLD", "KC_VOLU"], ["KC_TRNS", "KC_TRNS"]]]})via";

    void TestFile()
    {
        std::printf("a file the VIA app wrote, for a protocol 12 board\n");

        Check(via::IsSavedLayout(c_ViaFile), "told from a .vil by its content");
        Check(!via::IsSavedLayout(R"({"version": 1, "uid": 5, "layout": []})"), "a .vil is not one");
        Check(!via::IsSavedLayout("not JSON"), "nor is anything else");

        const nazg::SavedLayout layout = via::ReadSavedLayout(c_ViaFile, NameTable::V12, qmk::KeycodeVersion::V0_0_7, 2, 3);
        Check(layout.format == nazg::SavedLayoutFormat::Via && !layout.uid, "VIA's, with no UID");
        Check(layout.vendorId == 0x5250 && layout.productId == 0x0001, "vendorProductId split into VID:PID");
        Check(layout.keymap.size() == 2 && layout.Rows() == 2 && layout.Columns() == 3, "2 layers of 2 by 3, from a flat list");
        Check(Value(layout.keymap[0][0][0]) == 0x0029, "KC_ESC, row 0");
        Check(Value(layout.keymap[0][0][1]) == 0x412C, "a layer tap");
        Check(Value(layout.keymap[0][1][0]) == 0x021E, "S(KC_1), row 1: index 3 of 3 columns");
        Check(Value(layout.keymap[0][1][1]) == 0x7C40, "VIA's hex read");
        Check(!layout.keymap[0][1][2] && layout.unreadableKeys.size() == 1 && layout.unreadableKeys[0].layer == 0 &&
                  layout.unreadableKeys[0].row == 1 && layout.unreadableKeys[0].column == 2 &&
                  layout.unreadableKeys[0].text == "KC_WHATEVER",
              "an unknown name left out and said, where VIA loads KC_NO");
        Check(Value(layout.keymap[1][1][0]) == 0x2304, "a mod-tap");
        Check(Value(layout.keymap[1][1][2]) == 0x0047, "KC_SLCK");

        Check(layout.knobs.size() == 2 && layout.knobs[0].size() == 1, "[encoder][layer] turned into [layer][knob]");
        Check(Value(layout.knobs[0][0].counterClockwise) == 0x00AA && Value(layout.knobs[0][0].clockwise) == 0x00A9,
              "counter-clockwise first");
        Check(Value(layout.knobs[1][0].clockwise) == 0x0001, "the second layer's");

        Check(layout.macros.size() == 3, "a slot per macro");
        Check(layout.macros[0] && layout.macros[0]->size() == 13, "the first, thirteen actions");
        if (layout.macros[0] && layout.macros[0]->size() == 13)
        {
            const auto& macro = *layout.macros[0];
            Check(macro[0] == MacroAction{ MacroAction::Kind::Character, 'H' }, "text, a character each");
            Check(macro[2] == MacroAction{ MacroAction::Kind::Tap, 0x28 }, "{KC_ENT} tapped");
            Check(macro[3] == MacroAction{ MacroAction::Kind::Wait, 100 }, "{100} a wait");
            Check(macro[4] == MacroAction{ MacroAction::Kind::Press, 0xE0 } &&
                      macro[5] == MacroAction{ MacroAction::Kind::Press, 0x06 } &&
                      macro[6] == MacroAction{ MacroAction::Kind::Release, 0x06 } &&
                      macro[7] == MacroAction{ MacroAction::Kind::Release, 0xE0 },
                  "a chord pressed in order, released in reverse");
            Check(macro[8] == MacroAction{ MacroAction::Kind::Press, 0xE1 } &&
                      macro[10] == MacroAction{ MacroAction::Kind::Release, 0xE1 },
                  "{+KC_LSFT} and {-KC_LSFT}");
            Check(macro[11] == MacroAction{ MacroAction::Kind::Character, '{' }, "\\{ a brace typed");
        }
        Check(layout.macros[1] && layout.macros[1]->empty(), "an empty macro");
        Check(!layout.macros[2] && layout.unreadable.size() == 1 && layout.unreadable[0] == "macro 2",
              "a macro with an unknown key left out, said");
    }

    void TestOtherMatrix()
    {
        std::printf("a file whose layers do not fit the board's matrix\n");

        const nazg::SavedLayout layout = via::ReadSavedLayout(c_ViaFile, NameTable::V12, qmk::KeycodeVersion::V0_0_7, 2, 4);
        Check(layout.keymap.empty() && layout.unreadableKeys.empty(), "the keymap left out: its keys cannot be placed");
        Check(!layout.unreadable.empty() && layout.unreadable[0].rfind("the keymap", 0) == 0, "and said");
        Check(layout.knobs.size() == 2 && layout.macros.size() == 3, "the knobs and macros still read");
    }

    void TestNotALayout()
    {
        std::printf("text that is not a VIA saved layout\n");

        bool refused = false;
        try
        {
            (void)via::ReadSavedLayout(R"({"layers": []})", NameTable::V12, qmk::KeycodeVersion::V0_0_7, 2, 3);
        }
        catch (const std::runtime_error&)
        {
            refused = true;
        }
        Check(refused, "no vendorProductId: refused");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestTables();
    TestNames();
    TestFile();
    TestOtherMatrix();
    TestNotALayout();
    return TestResult();
}
