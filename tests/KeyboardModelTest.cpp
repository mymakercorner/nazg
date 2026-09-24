// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The keyboard model, which is deliberately pure: no transport, no coroutines, no
// device. Every case here is literals in and values out.
//
// The keymap decode is the part most likely to be subtly wrong -- layer-major,
// row-major, big-endian, two bytes per cell -- so it is pinned cell by cell rather
// than by a checksum over the whole buffer. It lives in adapters/via (the buffer is
// VIA's wire format), but it produces the model's Keymap, so it is checked here.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "model/NazgKeyboard.h"

#include "IsoMacroDefinition.h"
#include "ModelFDefinition.h"
#include "TestSupport.h"
#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/vial/NazgVialDefinition.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using nazg::BuildKeyboard;
using nazg::DecodeLayoutOptions;
using nazg::DecodeViaKeymap;
using nazg::DefinitionKey;
using nazg::FormatKeycode;
using nazg::Keyboard;
using nazg::KeyboardDefinition;
using nazg::Keycode;
using nazg::Keymap;
using nazg::LayoutOptionGroup;
using nazg::ParseDefinition;
using nazg::ParseLayoutGroups;
using nazg::PlaceKeys;
using nazg::QmkKeycodeVersion;
using nazg::ViaKeymapByteCount;

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

    constexpr QmkKeycodeVersion c_Version = QmkKeycodeVersion::V0_0_7;

    // A cell's keycode back as the raw value it was decoded from, -1 if it will not
    // encode. The codec round-trips every value exactly, so this recovers the bytes.
    int RawValue(const Keycode& keycode)
    {
        const std::optional<uint16_t> value = nazg::EncodeQmkKeycode(keycode, c_Version);
        return value ? *value : -1;
    }

    // A buffer whose every cell holds a value encoding its own position, so a mistake
    // in the index arithmetic produces an obviously wrong number rather than a
    // plausible one.
    std::vector<uint8_t> MakeKeymapBytes(uint8_t layers, uint8_t rows, uint8_t columns)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(ViaKeymapByteCount(layers, rows, columns));

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

        Check(ViaKeymapByteCount(3, 8, 18) == 864, "a 3 x 8 x 18 keymap is 864 bytes on the wire");

        const Keymap empty;
        Check(empty.IsEmpty(), "a default keymap holds nothing");
        Check(!empty.Contains(0, 0, 0), "and contains no cell");

        Keymap keymap(3, 8, 18);
        Check(keymap.Layers() == 3 && keymap.Rows() == 8 && keymap.Columns() == 18, "the extents are kept");
        Check(keymap.Contains(2, 7, 17), "the last cell is inside");
        Check(!keymap.Contains(3, 0, 0), "one layer past the end is outside");
        Check(!keymap.Contains(0, 8, 0), "one row past the end is outside");
        Check(!keymap.Contains(0, 0, 18), "one column past the end is outside");
        Check(FormatKeycode(keymap.At(0, 0, 0)) == "KC_NO", "a new keymap is all KC_NO");

        keymap.Set(1, 2, 3, nazg::LayerKey{ nazg::LayerOp::Momentary, 1 });
        Check(FormatKeycode(keymap.At(1, 2, 3)) == "MO(1)", "a written cell reads back");
        Check(FormatKeycode(keymap.At(1, 2, 4)) == "KC_NO", "and its neighbour is untouched");
    }

    void TestKeymapDecode()
    {
        std::printf("keymap decode\n");

        const std::vector<uint8_t> bytes  = MakeKeymapBytes(3, 8, 18);
        const Keymap               keymap = DecodeViaKeymap(bytes, 3, 8, 18, c_Version);

        Check(RawValue(keymap.At(0, 0, 0)) == 0x0000, "the first cell is the first two bytes");
        Check(RawValue(keymap.At(0, 0, 1)) == 0x0001, "columns advance fastest");
        Check(RawValue(keymap.At(0, 1, 0)) == 0x0100, "then rows");
        Check(RawValue(keymap.At(1, 0, 0)) == 0x1000, "then layers -- the buffer is layer-major");
        Check(RawValue(keymap.At(2, 7, 17)) == 0x2711, "the last cell lands at the end");

        // Big-endian, like every keycode in the protocol, and decoded on the way in.
        const std::vector<uint8_t> real = { 0x00, 0x04, 0x41, 0x04, 0x7C, 0x42 };
        const Keymap               row  = DecodeViaKeymap(real, 1, 1, 3, c_Version);
        Check(FormatKeycode(row.At(0, 0, 0)) == "KC_A" && FormatKeycode(row.At(0, 0, 1)) == "LT(1,KC_A)" &&
              FormatKeycode(row.At(0, 0, 2)) == "HF_TOGG",
              "cells arrive as keycodes, not values");

        Check(Throws([&] { (void)DecodeViaKeymap(bytes, 3, 8, 17, c_Version); }),
              "a buffer that does not match the shape is rejected");
        Check(Throws([&] { (void)DecodeViaKeymap({}, 1, 1, 1, c_Version); }),
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

        // The value a real board reported. A Model F with six one-bit groups had the
        // FIRST of them -- "Split Backspace" -- switched on in the Vial GUI and
        // answered 0x20. This is the case that pins the bit order: with the opposite
        // packing the same value would select the last group instead.
        std::vector<LayoutOptionGroup> modelF(6);
        Check(DecodeLayoutOptions(0x20, modelF) == std::vector<uint8_t>({ 1, 0, 0, 0, 0, 0 }),
              "0x20 selects the first of six groups, as the hardware reported");

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
        const Keymap             keymap     = DecodeViaKeymap(
            MakeKeymapBytes(3, definition.matrixRows, definition.matrixColumns), 3,
            definition.matrixRows, definition.matrixColumns, c_Version);

        const Keyboard keyboard = BuildKeyboard(definition, keymap, 0, c_Version);

        Check(keyboard.Name() == "leyden_jar/B104", "the definition came along");
        Check(keyboard.keymap.Layers() == 3, "three layers");
        Check(keyboard.keymap.Rows() == 8 && keyboard.keymap.Columns() == 18, "8 x 18 of cells");
        Check(keyboard.layoutSelection.size() == 6, "six layout groups were decoded");
        Check(keyboard.keycodeVersion == c_Version, "the keycode version is kept for writing back");

        // Every key the definition describes must address a cell that exists, or the
        // renderer would be asking for keycodes that are not there.
        bool allAddressable = true;
        for (const DefinitionKey& key : keyboard.definition.keys)
            if (!keyboard.keymap.Contains(0, key.row, key.column))
                allAddressable = false;

        Check(allAddressable, "every key addresses a real matrix cell");

        const DefinitionKey& first = keyboard.definition.keys.front();
        Check(RawValue(keyboard.KeycodeFor(first, 0)) == ((first.row << 8) | first.column),
              "a key reads the keycode of its own cell");
        Check(RawValue(keyboard.KeycodeFor(first, 2)) == (0x2000 | (first.row << 8) | first.column),
              "and a different layer reads a different one");

        Check(Throws([&] { (void)BuildKeyboard(definition, Keymap(3, 8, 17), 0, c_Version); }),
              "a keymap whose matrix disagrees with the definition is rejected");
    }

    const DefinitionKey* FindPlaced(const std::vector<DefinitionKey>& placed, int row, int column)
    {
        for (const DefinitionKey& key : placed)
            if (!key.decal && key.row == row && key.column == column)
                return &key;
        return nullptr;
    }

    // What is drawn, moved so its top-left corner is the origin, in a fixed order: two
    // placements are the same drawing exactly when these match.
    std::vector<std::tuple<bool, int, int, float, float, float, float>> Drawing(std::vector<DefinitionKey> placed)
    {
        float minX = placed.front().x;
        float minY = placed.front().y;
        for (const DefinitionKey& key : placed)
        {
            minX = std::min(minX, key.x);
            minY = std::min(minY, key.y);
        }

        std::vector<std::tuple<bool, int, int, float, float, float, float>> drawing;
        for (const DefinitionKey& key : placed)
            drawing.emplace_back(key.decal, key.decal ? -1 : key.row, key.decal ? -1 : key.column,
                                 key.x - minX, key.y - minY, key.width, key.height);

        std::sort(drawing.begin(), drawing.end());
        return drawing;
    }

    void TestPlaceKeys()
    {
        std::printf("placing layout options\n");

        const KeyboardDefinition source    = ParseDefinition(IsoMacroSource());
        const KeyboardDefinition converted = ParseDefinition(IsoMacroConverted());

        const std::vector<DefinitionKey> byDefault = PlaceKeys(source, { 0 });
        Check(byDefault.size() == 9, "choice 0: the 7 keys plus its 2");
        const DefinitionKey* top = FindPlaced(byDefault, 2, 1);
        Check(top != nullptr && top->x == 1.5f && top->y == 0.0f, "choice 0 stays where it is drawn");

        // Choice 1's topmost-leftmost key is its decal. Lining up without it would take
        // key (2,2), one row lower, as the pivot and pull it up into the wrong row.
        const std::vector<DefinitionKey> alternative = PlaceKeys(source, { 1 });
        Check(alternative.size() == 9, "choice 1: the 7 keys, its key and its decal");
        const DefinitionKey* bottom = FindPlaced(alternative, 2, 2);
        Check(bottom != nullptr && bottom->x == 1.5f && bottom->y == 1.0f,
              "choice 1 moves onto choice 0, its decal counting as the pivot");

        const std::vector<DefinitionKey> lined = PlaceKeys(converted, { 1 });
        const DefinitionKey*             unmoved = FindPlaced(lined, 2, 2);
        Check(unmoved != nullptr && unmoved->x == 0.0f && unmoved->y == 1.0f,
              "the converted form is already lined up, so nothing moves");

        Check(Drawing(PlaceKeys(source, { 0 })) == Drawing(PlaceKeys(converted, { 0 })),
              "both forms draw the same board with choice 0, up to a translation");
        Check(Drawing(PlaceKeys(source, { 1 })) == Drawing(PlaceKeys(converted, { 1 })),
              "and with choice 1");

        Check(PlaceKeys(source, {}).size() == 11, "with no selection every key is kept, unmoved");
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
    TestPlaceKeys();

    return TestResult();
}
