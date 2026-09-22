// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The keyboard model, which is deliberately pure: no transport, no coroutines, no
// device. Every case here is literals in and values out.
//
// The keymap decode is the part most likely to be subtly wrong -- layer-major,
// row-major, big-endian, two bytes per cell -- so it is pinned cell by cell rather
// than by a checksum over the whole buffer.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "model/NazgKeyboard.h"

#include "ModelFDefinition.h"
#include "TestSupport.h"
#include "adapters/vial/NazgVialDefinition.h"

#include <stdexcept>
#include <string>
#include <vector>

using nazg::BuildKeyboard;
using nazg::DecodeLayoutOptions;
using nazg::DefinitionKey;
using nazg::Keyboard;
using nazg::KeyboardDefinition;
using nazg::Keymap;
using nazg::LayoutOptionGroup;
using nazg::ParseLayoutGroups;

namespace
{
    template <typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    // A buffer whose every cell holds a value encoding its own position, so a mistake
    // in the index arithmetic produces an obviously wrong number rather than a
    // plausible one.
    std::vector<uint8_t> MakeKeymapBytes(uint8_t layers, uint8_t rows, uint8_t columns)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(Keymap::ByteCount(layers, rows, columns));

        for (uint8_t layer = 0; layer < layers; ++layer)
            for (uint8_t row = 0; row < rows; ++row)
                for (uint8_t column = 0; column < columns; ++column)
                {
                    const uint16_t keycode = static_cast<uint16_t>((layer << 12) | (row << 8) | column);
                    bytes.push_back(static_cast<uint8_t>(keycode >> 8));
                    bytes.push_back(static_cast<uint8_t>(keycode & 0xFF));
                }

        return bytes;
    }

    void TestKeymapShape()
    {
        std::printf("keymap shape\n");

        Check(Keymap::ByteCount(3, 8, 18) == 864, "a 3 x 8 x 18 keymap is 864 bytes on the wire");

        const Keymap empty;
        Check(empty.IsEmpty(), "a default keymap holds nothing");
        Check(!empty.Contains(0, 0, 0), "and contains no cell");

        Keymap keymap(3, 8, 18);
        Check(keymap.Layers() == 3 && keymap.Rows() == 8 && keymap.Columns() == 18, "the extents are kept");
        Check(keymap.Contains(2, 7, 17), "the last cell is inside");
        Check(!keymap.Contains(3, 0, 0), "one layer past the end is outside");
        Check(!keymap.Contains(0, 8, 0), "one row past the end is outside");
        Check(!keymap.Contains(0, 0, 18), "one column past the end is outside");

        keymap.Set(1, 2, 3, 0xABCD);
        Check(keymap.At(1, 2, 3) == 0xABCD, "a written cell reads back");
        Check(keymap.At(1, 2, 4) == 0, "and its neighbour is untouched");
    }

    void TestKeymapDecode()
    {
        std::printf("keymap decode\n");

        const std::vector<uint8_t> bytes = MakeKeymapBytes(3, 8, 18);
        const Keymap               keymap = Keymap::FromBuffer(bytes, 3, 8, 18);

        Check(keymap.At(0, 0, 0) == 0x0000, "the first cell is the first two bytes");
        Check(keymap.At(0, 0, 1) == 0x0001, "columns advance fastest");
        Check(keymap.At(0, 1, 0) == 0x0100, "then rows");
        Check(keymap.At(1, 0, 0) == 0x1000, "then layers -- the buffer is layer-major");
        Check(keymap.At(2, 7, 17) == 0x2711, "the last cell lands at the end");

        // Big-endian, like every keycode in the protocol.
        const std::vector<uint8_t> single = { 0x7C, 0x0A };
        Check(Keymap::FromBuffer(single, 1, 1, 1).At(0, 0, 0) == 0x7C0A,
              "keycodes are decoded big-endian");

        Check(Throws([&] { (void)Keymap::FromBuffer(bytes, 3, 8, 17); }),
              "a buffer that does not match the shape is rejected");
        Check(Throws([&] { (void)Keymap::FromBuffer({}, 1, 1, 1); }),
              "an empty buffer is rejected");
    }

    void TestLayoutGroups()
    {
        std::printf("layout groups\n");

        // Exactly the six the Model F declares: five toggles and one named choice.
        const std::vector<std::string> labels = {
            "Split Backspace", "Full Left Shift", "Split Right Shift",
            R"(["Enter","ISO Enter","ANSI Enter"])", "Split Numpad Zero", "Full Nav Cluster"
        };

        const std::vector<LayoutOptionGroup> groups = ParseLayoutGroups(labels);

        Check(groups.size() == 6, "six groups");
        Check(groups[0].name == "Split Backspace" && groups[0].options.empty(),
              "a plain string is a toggle");
        Check(groups[0].Count() == 2, "and a toggle has two states");
        Check(groups[3].name == "Enter", "an array's first element names the group");
        Check(groups[3].options.size() == 2 && groups[3].options[0] == "ISO Enter",
              "and the rest are its options");
        Check(groups[3].Count() == 2, "this choice has two options");
    }

    void TestLayoutOptionDecoding()
    {
        std::printf("layout option decoding\n");

        std::vector<LayoutOptionGroup> groups(3);   // three toggles, one bit each

        Check(DecodeLayoutOptions(0, groups) == std::vector<uint8_t>({ 0, 0, 0 }),
              "zero selects the first option everywhere");

        // The last group takes the lowest bits.
        Check(DecodeLayoutOptions(0b001, groups) == std::vector<uint8_t>({ 0, 0, 1 }),
              "bit 0 belongs to the last group");
        Check(DecodeLayoutOptions(0b100, groups) == std::vector<uint8_t>({ 1, 0, 0 }),
              "and the highest bit to the first");

        // A group with more than two options needs more than one bit.
        std::vector<LayoutOptionGroup> wide(2);
        wide[0].options = { "a", "b", "c", "d", "e" };   // 5 options -> 3 bits
        const std::vector<uint8_t> selection = DecodeLayoutOptions(0b1001, wide);
        Check(selection.size() == 2 && selection[0] == 4 && selection[1] == 1,
              "a five-option group takes three bits, leaving one for the toggle");
    }

    void TestKeyVisibility()
    {
        std::printf("key visibility\n");

        Keyboard keyboard;
        keyboard.layoutSelection = { 0, 1 };

        DefinitionKey always;
        Check(keyboard.IsKeyVisible(always), "a key with no layout option is always shown");

        DefinitionKey selected;
        selected.layoutIndex  = 1;
        selected.layoutOption = 1;
        Check(keyboard.IsKeyVisible(selected), "a key matching the selection is shown");

        DefinitionKey alternative;
        alternative.layoutIndex  = 1;
        alternative.layoutOption = 0;
        Check(!keyboard.IsKeyVisible(alternative), "its alternative is hidden");

        DefinitionKey unknownGroup;
        unknownGroup.layoutIndex  = 9;
        unknownGroup.layoutOption = 0;
        Check(keyboard.IsKeyVisible(unknownGroup), "a key in an undeclared group is shown, not dropped");
    }

    // The whole model assembled from the real definition, so the pieces are checked
    // against a board that exists rather than against invented dimensions.
    void TestBuildFromRealDefinition()
    {
        std::printf("build from the real definition\n");

        const KeyboardDefinition definition = nazg::DecodeDefinition(ModelFDefinition());
        const std::vector<uint8_t> bytes =
            MakeKeymapBytes(3, definition.matrixRows, definition.matrixColumns);

        const Keyboard keyboard = BuildKeyboard(definition, bytes, 3, 0);

        Check(keyboard.Name() == "leyden_jar/B104", "the definition came along");
        Check(keyboard.keymap.Layers() == 3, "three layers");
        Check(keyboard.keymap.Rows() == 8 && keyboard.keymap.Columns() == 18, "8 x 18 of cells");
        Check(keyboard.layoutSelection.size() == 6, "six layout groups were decoded");

        // Every key the definition describes must address a cell that exists, or the
        // renderer would be asking for keycodes that are not there.
        bool allAddressable = true;
        for (const DefinitionKey& key : keyboard.definition.keys)
            if (!keyboard.keymap.Contains(0, key.row, key.column))
                allAddressable = false;

        Check(allAddressable, "every key addresses a real matrix cell");

        const DefinitionKey& first = keyboard.definition.keys.front();
        Check(keyboard.KeycodeFor(first, 0) == ((first.row << 8) | first.column),
              "a key reads the keycode of its own cell");
        Check(keyboard.KeycodeFor(first, 2) == (0x2000 | (first.row << 8) | first.column),
              "and a different layer reads a different one");

        Check(Throws([&] { (void)BuildKeyboard(definition, bytes, 4, 0); }),
              "a layer count that disagrees with the buffer is rejected");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestKeymapShape();
    TestKeymapDecode();
    TestLayoutGroups();
    TestLayoutOptionDecoding();
    TestKeyVisibility();
    TestBuildFromRealDefinition();

    return TestResult();
}
