// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Vial's saved layout (.vil): keycodes spelled as vial-gui spells them, files as vial-gui writes
// them read into the model, and Nazg's own files read back. docs/research_material/vil-files.md.

#include <cstdio>
#include <stdexcept>
#include <string>

#include "adapters/vial/NazgVialKeycodeNames.h"
#include "adapters/vial/NazgVialSavedLayout.h"
#include "board/NazgSavedLayoutTransfer.h"
#include "TestSupport.h"

namespace vial = nazg::vial;
namespace qmk  = nazg::qmk;

using nazg::Keycode;
using nazg::LayerKey;
using nazg::LayerOp;
using nazg::LayerTapKey;
using nazg::MacroKey;
using nazg::ModifiedKey;
using nazg::NamedKey;
using nazg::TapDanceKey;

namespace
{
    using vial::NameTable;

    void TestNames()
    {
        std::printf("keycodes spelled as vial-gui spells them\n");

        Check(vial::SpellKeycode(0x0028, NameTable::V6) == "KC_ENTER", "a basic key by vial-gui's own name");
        Check(vial::ReadKeycodeName("KC_ENTER", NameTable::V6) == 0x0028, "and read back");
        Check(vial::SpellKeycode(0x0222, NameTable::V6) == "LSFT(KC_5)", "a shifted key as its mask: LSFT(KC_5)");
        Check(vial::ReadKeycodeName("LSFT(KC_5)", NameTable::V6) == 0x0222, "read back");
        Check(vial::ReadKeycodeName("KC_PERC", NameTable::V6) == 0x0222, "its own name read too");
        Check(vial::SpellKeycode(0x412C, NameTable::V6) == "LT1(KC_SPACE)", "a layer tap as LT1(KC_SPACE)");
        Check(vial::ReadKeycodeName("LT1(KC_SPACE)", NameTable::V6) == 0x412C, "read back");

        Check(vial::SpellKeycode(0x7703, NameTable::V6) == "M3", "a macro key, v6");
        Check(vial::SpellKeycode(0x5F15, NameTable::V5) == "M3", "a macro key, v5");
        Check(vial::ReadKeycodeName("M3", NameTable::V5) == 0x5F15, "read with the file's table");
        Check(vial::SpellKeycode(0x5702, NameTable::V6) == "TD(2)", "a tap dance key");
        Check(vial::ReadKeycodeName("TD(2)", NameTable::V6) == 0x5702, "read back");
        Check(vial::SpellKeycode(0x5221, NameTable::V6) == "MO(1)", "a layer key, v6");
        Check(vial::ReadKeycodeName("MO(1)", NameTable::V5) == 0x5101, "and v5's value");
        Check(vial::ReadKeycodeName("TO(1)", NameTable::V5) == 0x5011, "v5's TO with QMK's old ON_PRESS bit");
        Check(vial::ReadKeycodeName("TO(1)", NameTable::V6) == 0x5201, "v6's without");

        Check(vial::SpellKeycode(0x7E05, NameTable::V6) == "USER05", "a user key");
        Check(vial::SpellKeycode(0x7E14, NameTable::V6) == "0x7e14", "past USER15, vial-gui has no name");
        Check(vial::ReadKeycodeName("FN_MO13", NameTable::V6) == 0x7C77, "Vial's FN_MO13, QMK's tri-layer lower");
        Check(vial::ReadKeycodeName("RGB_TOG", NameTable::V5) == 0x5CC2, "an old name in the v5 table");
        Check(vial::SpellKeycode(0x7FFF, NameTable::V6) == "0x7fff", "a value with no name, in hex");
        Check(vial::ReadKeycodeName("0x7e05", NameTable::V6) == 0x7E05, "hex read");
        Check(vial::ReadKeycodeName("12", NameTable::V6) == 12, "a number read");
        Check(!vial::ReadKeycodeName("KC_WHATEVER", NameTable::V6), "an unknown name is not read");
        Check(!vial::ReadKeycodeName("LSFT(M3)", NameTable::V6), "nor a mask around more than a basic key");
    }

    // A file as vial-gui writes one: a Vial 6 board, 2 layers of 2 by 2, one knob.
    const char* c_VialFile = R"vil({"version": 1, "uid": 11254004745116475160,
        "layout": [[["KC_ESCAPE", "LT1(KC_SPACE)"], ["KC_ENTER", -1]], [["KC_TRNS", "M0"], ["TD(0)", "LSFT(KC_5)"]]],
        "encoder_layout": [[["KC_VOLD", "KC_VOLU"]], [["KC_TRNS", "KC_TRNS"]]],
        "layout_options": 1,
        "macro": [[["text", "hi"], ["tap", "KC_ENTER"], ["delay", 100]], []],
        "vial_protocol": 6, "via_protocol": 9,
        "tap_dance": [["KC_ESCAPE", "KC_CAPSLOCK", "KC_NO", "KC_NO", 200]],
        "combo": [["KC_J", "KC_K", "KC_NO", "KC_NO", "KC_ESCAPE"]],
        "key_override": [{"trigger": "KC_BSPACE", "replacement": "KC_DELETE", "layers": 65535, "trigger_mods": 2,
                          "negative_mod_mask": 0, "suppressed_mods": 2, "options": 135}],
        "alt_repeat_key": [{"keycode": "KC_N", "alt_keycode": "KC_P", "allowed_mods": 0, "options": 8}],
        "settings": {"7": 180, "22": 1}})vil";

    void TestVialFile()
    {
        std::printf("a file vial-gui wrote, for a Vial 6 board\n");

        const vial::SavedLayout layout = vial::ReadSavedLayout(c_VialFile, qmk::KeycodeVersion::V0_0_7);
        Check(layout.uid == 0x9C2E41B07D3A5F18ull, "the UID, past 2^63");
        Check(layout.vialProtocol == 6 && layout.viaProtocol == 9, "the protocols");
        Check(!layout.vendorId, "no VID:PID: vial-gui does not write it");
        Check(layout.keymap.size() == 2 && layout.Rows() == 2 && layout.Columns() == 2, "2 layers of 2 by 2");
        Check(layout.keymap[0][0][0] == Keycode{ NamedKey{ "KC_ESC" } }, "KC_ESCAPE is Nazg's KC_ESC");
        Check(layout.keymap[0][0][1] == Keycode{ LayerTapKey{ 1, "KC_SPC" } }, "LT1(KC_SPACE)");
        Check(!layout.keymap[0][1][1] && layout.unreadableKeys.empty(), "-1 is no value, not an unreadable one");
        Check(layout.keymap[1][0][1] == Keycode{ MacroKey{ 0 } }, "M0");
        Check(layout.keymap[1][1][0] == Keycode{ TapDanceKey{ 0 } }, "TD(0)");
        Check(layout.keymap[1][1][1] == Keycode{ ModifiedKey{ nazg::Mod::LeftShift, "KC_5" } }, "LSFT(KC_5)");

        Check(layout.knobs.size() == 2 && layout.knobs[0].size() == 1, "a knob on each layer");
        Check(layout.knobs[0][0].counterClockwise == Keycode{ NamedKey{ "KC_VOLD" } } &&
                  layout.knobs[0][0].clockwise == Keycode{ NamedKey{ "KC_VOLU" } },
              "counter-clockwise first");
        Check(layout.layoutOptions == 1u, "layout options");

        Check(layout.macros.size() == 2 && layout.macros[0] && layout.macros[0]->size() == 4, "a macro of four actions");
        const auto& macro = *layout.macros[0];
        Check(macro[0].kind == nazg::via::MacroAction::Kind::Character && macro[0].value == 'h', "its text, a character each");
        Check(macro[2].kind == nazg::via::MacroAction::Kind::Tap && macro[2].value == 0x28, "the key tapped, as the board numbers it");
        Check(macro[3].kind == nazg::via::MacroAction::Kind::Wait && macro[3].value == 100, "the wait");
        Check(layout.macros[1] && layout.macros[1]->empty(), "an empty macro");

        Check(layout.tapDances.size() == 1 && layout.tapDances[0], "a tap dance");
        Check(layout.tapDances[0]->actions[1] == Keycode{ NamedKey{ "KC_CAPS" } } && !layout.tapDances[0]->actions[2] &&
                  layout.tapDances[0]->tappingTerm == 200,
              "KC_NO is an empty action");
        Check(layout.combos.size() == 1 && layout.combos[0] && layout.combos[0]->output == Keycode{ NamedKey{ "KC_ESC" } },
              "a combo");
        Check(layout.keyOverrides.size() == 1 && layout.keyOverrides[0] && layout.keyOverrides[0]->held == 2 &&
                  layout.keyOverrides[0]->replacement == Keycode{ NamedKey{ "KC_DEL" } },
              "a key override");
        Check(layout.altRepeatKeys.size() == 1 && layout.altRepeatKeys[0] && layout.altRepeatKeys[0]->options == 8,
              "an alt repeat key");
        Check(layout.qmkSettings.size() == 2 && layout.qmkSettings.at(7) == 180, "the QMK settings");
        Check(layout.unreadable.empty(), "everything read");
    }

    void TestOldFirmware()
    {
        std::printf("a file saved before QMK's renumbering\n");

        const char* file = R"vil({"version": 1, "uid": 5, "vial_protocol": 5, "via_protocol": 9,
            "layout": [[["RGB_TOG", "0x5cc2"], ["MO(1)", "0x5f40"]]],
            "macro": [[["tap", "0x5f40"]]]})vil";
        const vial::SavedLayout layout = vial::ReadSavedLayout(file, qmk::KeycodeVersion::V0_0_7);

        Check(layout.keymap[0][0][0] == Keycode{ NamedKey{ "UG_TOGG" } }, "v5's RGB_TOG is the lighting toggle");
        Check(layout.keymap[0][0][1] == Keycode{ NamedKey{ "UG_TOGG" } }, "so is its old raw value");
        Check(layout.keymap[0][1][0] == Keycode{ LayerKey{ LayerOp::Momentary, 1 } }, "MO(1) at v5's value");
        Check(!layout.keymap[0][1][1] && layout.unreadableKeys.size() == 1 && layout.unreadableKeys[0].row == 1 &&
                  layout.unreadableKeys[0].column == 1,
              "an old value nothing names is unreadable, not replayed");
        Check(layout.macros.size() == 1 && !layout.macros[0] && layout.unreadable.size() == 1 &&
                  layout.unreadable[0] == "macro 0",
              "a macro holding one is left out, and said");
    }

    void TestViaFile()
    {
        std::printf("a VIA board's file\n");

        // vial-gui reads a VIA board with its v5 table, so its names are the board's raw values seen
        // through v5: KC_ENTER is 0x28 anywhere, and 0x7c16 is the board's own.
        const char* file = R"vil({"version": 1, "uid": -1, "vial_protocol": -1, "via_protocol": 12,
            "layout": [[["KC_ENTER", "0x7c16"]]], "layout_options": -1})vil";
        const vial::SavedLayout layout = vial::ReadSavedLayout(file, qmk::KeycodeVersion::V0_0_9);

        Check(!layout.uid, "uid -1: no UID");
        Check(layout.keymap[0][0][0] == Keycode{ NamedKey{ "KC_ENT" } }, "a name");
        Check(layout.keymap[0][0][1] == Keycode{ NamedKey{ "QK_GESC" } }, "a raw value, read as the board numbers it");
        Check(!layout.layoutOptions, "layout options -1: none");
    }

    void TestRoundTrip()
    {
        std::printf("Nazg's own files read back\n");

        vial::SavedLayout vialBoard = vial::ReadSavedLayout(c_VialFile, qmk::KeycodeVersion::V0_0_7);
        vialBoard.vendorId  = 0x1209;
        vialBoard.productId = 0x4704;
        const std::string text = vial::WriteSavedLayout(vialBoard);
        Check(text.find("\"LT1(KC_SPACE)\"") != std::string::npos && text.find("\"KC_ENTER\"") != std::string::npos,
              "written with vial-gui's names");
        Check(text.find("\"nazg\"") != std::string::npos, "and Nazg's key");

        const vial::SavedLayout back = vial::ReadSavedLayout(text, qmk::KeycodeVersion::V0_0_7);
        Check(back.uid == vialBoard.uid && back.vendorId == 0x1209 && back.productId == 0x4704, "the board said again");
        Check(back.keymap == vialBoard.keymap, "the keymap");
        Check(back.knobs[0][0].clockwise == vialBoard.knobs[0][0].clockwise &&
                  back.knobs[0][0].counterClockwise == vialBoard.knobs[0][0].counterClockwise,
              "the knobs");
        Check(back.layoutOptions == vialBoard.layoutOptions, "layout options");
        Check(back.macros == vialBoard.macros, "the macros");
        Check(back.tapDances == vialBoard.tapDances && back.combos == vialBoard.combos, "tap dances and combos");
        Check(back.keyOverrides == vialBoard.keyOverrides && back.altRepeatKeys == vialBoard.altRepeatKeys,
              "key overrides and alt repeat keys");
        Check(back.qmkSettings == vialBoard.qmkSettings, "the QMK settings");

        vial::SavedLayout viaBoard;
        viaBoard.viaProtocol    = 12;
        viaBoard.keycodeVersion = qmk::KeycodeVersion::V0_0_9;
        viaBoard.keymap         = { { { Keycode{ NamedKey{ "KC_ENT" } }, Keycode{ NamedKey{ "QK_GESC" } } } } };
        const std::string viaText = vial::WriteSavedLayout(viaBoard);
        Check(viaText.find("KC_") == std::string::npos && viaText.find("\"0x28\"") != std::string::npos,
              "a VIA board's keys as raw values: vial-gui would read names with the wrong table");
        Check(viaText.find("\"uid\":-1") != std::string::npos, "and uid -1");
        const vial::SavedLayout viaBack = vial::ReadSavedLayout(viaText, qmk::KeycodeVersion::V0_0_9);
        Check(viaBack.keymap == viaBoard.keymap, "read back");
    }

    void TestNotALayout()
    {
        std::printf("what is not a saved layout\n");

        bool threw = false;
        try
        {
            (void)vial::ReadSavedLayout("not json", qmk::KeycodeVersion::V0_0_7);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        Check(threw, "text that is not JSON");

        threw = false;
        try
        {
            (void)vial::ReadSavedLayout(R"vil({"name": "a VIA definition"})vil",qmk::KeycodeVersion::V0_0_7);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        Check(threw, "JSON with no keymap");
    }
}

namespace
{
    // A board of 2 by 2 cells, two layers, a key on every cell; the file read as it holds it.
    struct Fixture
    {
        nazg::Keyboard      keyboard;
        nazg::BoardContents board;
    };

    Fixture MakeBoard()
    {
        nazg::via::KeyboardDefinition definition;
        definition.matrixRows    = 2;
        definition.matrixColumns = 2;
        for (uint8_t row = 0; row < 2; ++row)
            for (uint8_t column = 0; column < 2; ++column)
            {
                nazg::via::DefinitionKey key;
                key.row    = row;
                key.column = column;
                key.x      = column;
                key.y      = row;
                definition.keys.push_back(key);
            }

        nazg::Keymap keymap(2, 2, 2);
        keymap.Set(0, 0, 0, NamedKey{ "KC_A" });
        Fixture fixture{ nazg::BuildKeyboard(definition, keymap, 0, qmk::KeycodeVersion::V0_0_7), {} };
        fixture.keyboard.report.isVial        = true;
        fixture.keyboard.report.vialProtocol  = 6;
        fixture.keyboard.report.macroCount    = 2;
        fixture.keyboard.report.tapDanceCount = 1;

        vial::SavedLayout& layout = fixture.board.layout;
        layout.vialProtocol       = 6;
        layout.keycodeVersion     = qmk::KeycodeVersion::V0_0_7;
        layout.keymap.assign(2, std::vector<std::vector<std::optional<Keycode>>>(2, std::vector<std::optional<Keycode>>(2)));
        for (uint8_t layer = 0; layer < 2; ++layer)
            for (uint8_t row = 0; row < 2; ++row)
                for (uint8_t column = 0; column < 2; ++column)
                    layout.keymap[layer][row][column] = keymap.At(layer, row, column);
        layout.macros    = { nazg::via::MacroActions{}, nazg::via::MacroActions{} };
        layout.tapDances = { nazg::TapDance{} };
        fixture.board.macroBufferSize = 8;
        return fixture;
    }

    void TestCompare()
    {
        std::printf("what a file changes on the board\n");

        const Fixture fixture = MakeBoard();
        Check(nazg::CompareWithBoard(fixture.board, fixture.board.layout, fixture.keyboard).IsEmpty(),
              "the board's own contents change nothing");

        vial::SavedLayout file = fixture.board.layout;
        file.keymap[1][0][1]   = Keycode{ NamedKey{ "KC_B" } };
        file.keymap.push_back(file.keymap[0]);
        nazg::LayoutChanges changes = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(changes.keys.size() == 1 && changes.keys[0].layer == 1 && changes.keys[0].column == 1 &&
                  changes.keys[0].then == Keycode{ NamedKey{ "KC_B" } },
              "a key that differs");
        Check(changes.leftOut.size() == 1 && changes.leftOut[0] == "1 layer past the 2 this keyboard has",
              "a layer past the board's, left out and said");
        Check(!changes.NeedsUnlock(file), "a plain key needs no unlock");

        file                 = fixture.board.layout;
        file.keymap[0][0][0] = Keycode{ NamedKey{ "QK_BOOT" } };
        Check(nazg::CompareWithBoard(fixture.board, file, fixture.keyboard).NeedsUnlock(file),
              "Boot does: a locked board stores it as nothing");

        file = fixture.board.layout;
        for (auto& rows : file.keymap)
            for (auto& row : rows)
                row.push_back(Keycode{ NamedKey{ "KC_C" } });
        changes = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(!changes.keymapFits && changes.keys.empty() && changes.leftOut.size() == 1,
              "another matrix: no key loaded, and why said");

        file           = fixture.board.layout;
        file.macros[1] = nazg::via::MacroActions{ { nazg::via::MacroAction::Kind::Character, 'a' } };
        changes        = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(changes.macros.size() == 1 && changes.macros[0] == 1 && changes.NeedsUnlock(file),
              "a macro that differs, which needs the board unlocked");

        file.macros[1] = nazg::via::MacroActions(9, { nazg::via::MacroAction::Kind::Character, 'a' });
        changes        = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(changes.macros.empty() && changes.leftOut.size() == 1 &&
                  changes.leftOut[0] == "the macros: they take 11 bytes, this keyboard holds 8",
              "macros too long for the buffer are left out");

        file = fixture.board.layout;
        nazg::TapDance dance;
        dance.actions[0] = Keycode{ NamedKey{ "KC_ESC" } };
        file.tapDances   = { dance, dance };
        changes          = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(changes.tapDances.size() == 1 && changes.leftOut.size() == 1 &&
                  changes.leftOut[0] == "1 tap dance past the 1 this keyboard has",
              "a slot past the board's count is left out");

        file             = fixture.board.layout;
        file.qmkSettings = { { 7, 180 } };
        changes          = nazg::CompareWithBoard(fixture.board, file, fixture.keyboard);
        Check(changes.qmkSettings.empty() && changes.leftOut.size() == 1, "a setting the firmware lacks is left out");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestNames();
    TestVialFile();
    TestOldFirmware();
    TestViaFile();
    TestRoundTrip();
    TestNotALayout();
    TestCompare();
    return TestResult();
}
