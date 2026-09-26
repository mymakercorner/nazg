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
            blank &= key.marks == 0 && key.fill == nazg::KeyFill::Neutral;
            for (const nazg::Legend& legend : key.legends)
                blank &= legend.text.empty();
        }
        Check(blank, "no legend, no mark, no line, no label");
    }

    void TestLegendSlots()
    {
        std::printf("legend slots\n");

        BoardKey key;
        key[LegendSlot::TopLeft].text    = "!";
        key[LegendSlot::MiddleLeft].text = "1";
        key[LegendSlot::FrontRight].text = "F";

        Check(key.legends[0].text == "!", "top left is KLE's position 0");
        Check(key.legends[3].text == "1", "middle left is KLE's position 3");
        Check(key.legends[11].text == "F", "front right is KLE's last, 11");
        Check(key[LegendSlot::MiddleLeft].role == nazg::LegendRole::Label, "a legend is a label unless it says otherwise");
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
    TestLegendSlots();
    TestKeyCentre();
    TestSpreadApart();

    return TestResult();
}
