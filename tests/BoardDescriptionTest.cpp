// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The board description a section fills: what it starts from, and the placing rules the
// renderer takes from it -- a key's centre, labels spread along an edge. How it looks is
// the renderer's and is not tested; what it means is.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgBoardDescription.h"

#include "TestSupport.h"

#include <cmath>
#include <variant>
#include <vector>

using nazg::BoardKey;
using nazg::DefinitionKey;
using nazg::DescribeKeyboard;
using nazg::KeyCentre;
using nazg::LegendSlot;
using nazg::SpreadApart;

namespace
{
    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }

    DefinitionKey Key(float x, float y, uint8_t row, uint8_t column)
    {
        DefinitionKey key;
        key.x      = x;
        key.y      = y;
        key.row    = row;
        key.column = column;
        return key;
    }

    // A section starts from the board as the definition draws it at its layout choice.
    void TestDescribeKeyboard()
    {
        std::printf("describe a keyboard\n");

        nazg::Keyboard keyboard;
        keyboard.definition.keys.push_back(Key(0, 0, 0, 0));

        DefinitionKey decal = Key(1, 0, 0, 0);
        decal.decal         = true;
        keyboard.definition.keys.push_back(decal);

        // One layout option: choice 0 on the board, choice 1 drawn below it, as a source
        // definition has it.
        DefinitionKey choice0 = Key(2, 0, 0, 1);
        choice0.layoutIndex   = 0;
        choice0.layoutOption  = 0;
        DefinitionKey choice1 = Key(2, 3, 0, 2);
        choice1.layoutIndex   = 0;
        choice1.layoutOption  = 1;
        keyboard.definition.keys.push_back(choice0);
        keyboard.definition.keys.push_back(choice1);

        keyboard.layoutSelection = { 1 };

        const nazg::BoardDescription board = DescribeKeyboard(keyboard);

        Check(board.keys.size() == 3, "the always-present key, the decal and the chosen option");
        Check(board.keys.size() == 3 && board.keys[1].geometry.decal, "the decal is kept, for the space it takes");

        bool chosenInPlace = false;
        for (const BoardKey& key : board.keys)
            if (key.geometry.column == 2)
                chosenInPlace = Near(key.geometry.x, 2) && Near(key.geometry.y, 0);
        Check(chosenInPlace, "the chosen option is where choice 0 is drawn");

        bool blank = board.lines.empty() && board.labels.empty();
        for (const BoardKey& key : board.keys)
        {
            blank &= key.marks == 0;
            blank &= std::holds_alternative<nazg::KeycapLegend>(key.legends) &&
                     std::get<nazg::KeycapLegend>(key.legends) == nazg::KeycapLegend{};
        }
        Check(blank, "no legend, no mark, no line, no label");

        // No keymap here: every key is KC_NO, which leaves a 1u key an alpha.
        bool classed = true;
        for (const BoardKey& key : board.keys)
            classed &= key.fill == nazg::KeyFill::Alpha;
        Check(classed, "each key filled with its keycap class");
    }

    // The class comes from what the key does on the base layer (ui-design.md, "Keycap colour
    // classes").
    void TestKeycapClasses()
    {
        std::printf("keycap classes\n");

        using nazg::KeyFill;
        using nazg::KeycapClassOf;
        using nazg::NamedKey;

        const DefinitionKey unit = Key(0, 0, 0, 0);
        DefinitionKey       wide = unit;
        wide.width               = 2.25f;

        Check(KeycapClassOf(NamedKey{ "KC_A" }, unit) == KeyFill::Alpha, "a letter is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_1" }, unit) == KeyFill::Alpha, "a digit is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_SCLN" }, unit) == KeyFill::Alpha, "punctuation is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_SPC" }, wide) == KeyFill::Alpha, "the space bar is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_P7" }, unit) == KeyFill::Alpha, "a numpad digit is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_PDOT" }, unit) == KeyFill::Alpha, "the numpad's dot is an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_ESC" }, unit) == KeyFill::Accent, "Esc is an accent");
        Check(KeycapClassOf(NamedKey{ "KC_ENT" }, wide) == KeyFill::Accent, "Enter is an accent");
        Check(KeycapClassOf(NamedKey{ "KC_PENT" }, unit) == KeyFill::Accent, "the numpad's Enter is an accent");
        Check(KeycapClassOf(NamedKey{ "KC_F1" }, unit) == KeyFill::Modifier, "an F-key is a modifier");
        Check(KeycapClassOf(NamedKey{ "KC_LSFT" }, wide) == KeyFill::Modifier, "Shift is a modifier");
        Check(KeycapClassOf(NamedKey{ "KC_PSLS" }, unit) == KeyFill::Modifier, "a numpad operator is a modifier");
        Check(KeycapClassOf(NamedKey{ "KC_UP" }, unit) == KeyFill::Modifier, "an arrow is a modifier");
        Check(KeycapClassOf(NamedKey{ "UG_TOGG" }, unit) == KeyFill::Modifier, "a command is a modifier");
        Check(KeycapClassOf(nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }, unit) == KeyFill::Modifier,
              "a layer key is a modifier");

        Check(KeycapClassOf(nazg::ModTapKey{ nazg::Mod::LeftCtrl, "KC_ESC" }, unit) == KeyFill::Accent,
              "a tap-hold takes its tap's class: Ctrl when held, Esc when tapped is an accent");
        Check(KeycapClassOf(nazg::LayerTapKey{ 1, "KC_SPC" }, wide) == KeyFill::Alpha,
              "a space bar holding a layer stays an alpha");
        Check(KeycapClassOf(nazg::ModifiedKey{ nazg::Mod::LeftShift, "KC_1" }, unit) == KeyFill::Alpha,
              "Shift with a character key still prints a character: an alpha");
        Check(KeycapClassOf(nazg::ModifiedKey{ nazg::Mod::LeftCtrl, "KC_C" }, unit) == KeyFill::Modifier,
              "Ctrl+C is a modifier");

        Check(KeycapClassOf(NamedKey{ "KC_NO" }, unit) == KeyFill::Alpha, "KC_NO on 1u: an alpha");
        Check(KeycapClassOf(NamedKey{ "KC_NO" }, wide) == KeyFill::Modifier, "KC_NO past 1.25u: a modifier");
        DefinitionKey quarter = unit;
        quarter.width         = 1.25f;
        Check(KeycapClassOf(NamedKey{ "KC_TRNS" }, quarter) == KeyFill::Alpha, "KC_TRNS on 1.25u: still an alpha");
    }

    void TestLegendSlots()
    {
        std::printf("legend slots\n");

        BoardKey key;
        Check(std::holds_alternative<nazg::KeycapLegend>(key.legends), "a key starts with keycap legends, blank");

        key[LegendSlot::TopLeft].text    = "!";
        key[LegendSlot::MiddleLeft].text = "1";
        key[LegendSlot::FrontRight].text = "F";

        const auto* slots = std::get_if<nazg::SlotLegends>(&key.legends);
        Check(slots != nullptr, "naming a slot turns them into slot legends");
        Check(slots != nullptr && (*slots)[0].text == "!", "top left is KLE's position 0");
        Check(slots != nullptr && (*slots)[3].text == "1", "middle left is KLE's position 3");
        Check(slots != nullptr && (*slots)[11].text == "F", "front right is KLE's last, 11");
        Check(key[LegendSlot::MiddleLeft].role == nazg::LegendRole::Label, "a legend is a label unless it says otherwise");
    }

    BoardKey Placed(float x, float y, float width)
    {
        BoardKey key;
        key.geometry       = Key(x, y, 0, 0);
        key.geometry.width = width;
        return key;
    }

    // A modifier names its side only where its keycode's side is not where the key sits,
    // measured from the space bar (ui-design.md, "Names of the modifiers").
    void TestSides()
    {
        std::printf("sides of the board\n");

        using nazg::KeySide;
        using nazg::SideLine;
        using nazg::SideOf;

        // A full-size bottom row: Ctrl, Win, Alt, the space bar, Alt, Win, Menu, Ctrl, then the
        // arrows and a numpad, which put the board's own centre far right of the space bar's.
        std::vector<BoardKey> keys = { Placed(0, 5, 1.25f),     Placed(1.25f, 5, 1.25f), Placed(2.5f, 5, 1.25f),
                                       Placed(3.75f, 5, 6.25f), Placed(10, 5, 1.25f),    Placed(11.25f, 5, 1.25f),
                                       Placed(12.5f, 5, 1.25f), Placed(13.75f, 5, 1.25f), Placed(15.25f, 5, 1),
                                       Placed(18.5f, 5, 2),     Placed(20.5f, 5, 1),      Placed(21.5f, 0, 1) };

        const float line = SideLine(keys);
        Check(Near(line, 6.875f), "the line is the space bar's centre, not the board's");
        Check(SideOf(keys[0].geometry, line) == KeySide::Left, "left Ctrl is on the left");
        Check(SideOf(keys[4].geometry, line) == KeySide::Right, "right Alt is on the right, though the board's centre is further");
        Check(SideOf(keys[3].geometry, line) == KeySide::Neither, "the space bar straddles the line");

        // No space bar -- an ortho -- and the board's own centre.
        std::vector<BoardKey> ortho;
        for (int column = 0; column < 12; ++column)
            ortho.push_back(Placed(static_cast<float>(column), 0, 1));
        Check(Near(SideLine(ortho), 6.0f), "without a space bar, the board's centre");
        Check(SideOf(ortho[5].geometry, 6.0f) == KeySide::Left && SideOf(ortho[6].geometry, 6.0f) == KeySide::Right,
              "an ortho's halves");
    }

    // Keymap's legends: the keycode at each key, seen through the settings and the key's side.
    void TestDescribeLegends()
    {
        std::printf("describe legends\n");

        nazg::Keyboard keyboard;
        keyboard.definition.matrixRows    = 1;
        keyboard.definition.matrixColumns = 3;
        DefinitionKey left  = Key(0, 0, 0, 0);
        DefinitionKey space = Key(1, 0, 0, 1);
        space.width         = 6.25f;
        DefinitionKey right = Key(7.25f, 0, 0, 2);
        keyboard.definition.keys = { left, space, right };
        keyboard.keymap          = nazg::Keymap(1, 1, 3);
        keyboard.keymap.Set(0, 0, 0, nazg::NamedKey{ "KC_RCTL" });
        keyboard.keymap.Set(0, 0, 1, nazg::NamedKey{ "KC_SPC" });
        keyboard.keymap.Set(0, 0, 2, nazg::NamedKey{ "KC_RCTL" });

        nazg::BoardDescription board = DescribeKeyboard(keyboard);
        nazg::DescribeLegends(board, keyboard, 0, nazg::LegendSettings{});

        const auto legendOf = [&](size_t index) { return std::get<nazg::KeycapLegend>(board.keys[index].legends); };
        Check(legendOf(0).cylindrical.full == "Right Control", "Right Ctrl on the left half names its side");
        Check(legendOf(2).cylindrical.full == "Control", "on the right half, the plain keycap word");
        Check(legendOf(1).placement == nazg::PlacementClass::Blank, "the space bar is blank");
    }

    void TestKeyCentre()
    {
        std::printf("key centre\n");

        DefinitionKey wide = Key(1, 2, 0, 0);
        wide.width         = 2;
        const auto [x, y]  = KeyCentre(wide);
        Check(Near(x, 2.0f) && Near(y, 2.5f), "the middle of an unrotated key");

        // Turned a quarter clockwise about the origin: what was to its right is now below.
        DefinitionKey turned = Key(0, 0, 0, 0);
        turned.rotation      = 90;
        const auto [tx, ty]  = KeyCentre(turned);
        Check(Near(tx, -0.5f) && Near(ty, 0.5f), "a rotated key's centre turns with it, clockwise on screen");
    }

    void TestSpreadApart()
    {
        std::printf("spread apart\n");

        Check(SpreadApart({ 0, 5, 10 }, { 2, 2, 2 }, 1) == std::vector<float>{ 0, 5, 10 },
              "labels with room stay where they are wanted");

        Check(SpreadApart({ 5, 5 }, { 2, 4 }, 0) == std::vector<float>{ 5, 8 },
              "the second of two on one spot moves on by half of each");

        Check(SpreadApart({ 10, 0, 1 }, { 2, 2, 2 }, 1) == std::vector<float>{ 10, 0, 3 },
              "order by wanted position, results by input index");

        Check(SpreadApart({ 0, 0, 0 }, { 2, 2, 2 }, 1) == std::vector<float>{ 0, 3, 6 },
              "a pile of labels becomes an ordered list");

        Check(SpreadApart({}, {}, 1).empty(), "no labels, nothing to place");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestDescribeKeyboard();
    TestKeycapClasses();
    TestLegendSlots();
    TestSides();
    TestDescribeLegends();
    TestKeyCentre();
    TestSpreadApart();

    return TestResult();
}
