// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Decodes a real keyboard's definition: XZ through minlzma (Vial's sourcing), then
// the SHARED VIA definition parser -- the same JSON a VIA registry serves.
//
// The fixture in ModelFDefinition.h is the genuine article, pulled off the board, so
// this covers the awkward parts no invented sample would have: USB ids written as
// strings, layout options in the fourth label, keys up to 6.25u wide, and rows that
// start with a vertical offset.
//
// Positions are compared exactly on purpose. Every offset in this format is a dyadic
// fraction -- 0.25, 0.5, 1.0 -- so the arithmetic is exact in binary floating point,
// and an epsilon would only hide a real mistake.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaProtocol.h"
#include "adapters/vial/NazgVialDefinition.h"

#include "IsoMacroDefinition.h"
#include "ModelFDefinition.h"
#include "TestSupport.h"

#include <string>
#include <vector>

using nazg::DecodeDefinition;
using nazg::DecompressDefinition;
using nazg::DefinitionKey;
using nazg::ParseDefinition;
using nazg::ProtocolError;
using nazg::KeyboardDefinition;

namespace
{
    template <typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const ProtocolError&)
        {
            return true;
        }
        return false;
    }

    void TestDecompression()
    {
        std::printf("decompression\n");

        const std::vector<uint8_t> compressed = ModelFDefinition();

        Check(compressed.size() == 704, "the fixture is the 704 bytes the board sent");
        Check(compressed[0] == 0xFD && compressed[1] == '7' && compressed[2] == 'z',
              "it starts with the XZ magic");

        const std::vector<uint8_t> json = DecompressDefinition(compressed);

        Check(json.size() == 2078, "it inflates to 2078 bytes");
        Check(json.front() == '{' && json.back() == '}', "and the result is a JSON object");
    }

    void TestDecompressionRejectsRubbish()
    {
        std::printf("decompression failures\n");

        Check(Throws([] { (void)DecompressDefinition({}); }), "an empty definition is rejected");

        Check(Throws([] { (void)DecompressDefinition(std::vector<uint8_t>(64, 0xAB)); }),
              "bytes that are not an XZ stream are rejected");

        // A truncated stream has a valid header, so it gets past the size query and
        // fails in the decode pass -- a different path through the wrapper.
        std::vector<uint8_t> truncated = ModelFDefinition();
        truncated.resize(truncated.size() / 2);
        Check(Throws([&] { (void)DecompressDefinition(truncated); }), "a truncated stream is rejected");
    }

    void TestDefinitionFields()
    {
        std::printf("definition fields\n");

        const KeyboardDefinition definition = DecodeDefinition(ModelFDefinition());

        Check(definition.name == "leyden_jar/B104", "the name comes through");
        Check(definition.vendorId == 0x1209, "the vendor id parses from its \"0x1209\" string");
        Check(definition.productId == 0x4704, "the product id parses the same way");
        Check(definition.lighting == "none", "lighting is reported");
        Check(definition.matrixRows == 8 && definition.matrixColumns == 18, "the matrix is 8x18");
    }

    void TestKeyGeometry()
    {
        std::printf("key geometry\n");

        const KeyboardDefinition definition = DecodeDefinition(ModelFDefinition());

        Check(definition.keys.size() == 129, "129 keys were found");

        // The first row starts with an x offset and no y offset.
        const DefinitionKey& first = definition.keys.front();
        Check(first.x == 15.5f && first.y == 0.0f, "the first key sits where its x offset puts it");
        Check(first.row == 6 && first.column == 5, "its matrix cell comes from the first label");

        // The third key is the first of row two: the row advance (1.0) plus that row's
        // own y offset (0.25). Getting relative-versus-absolute wrong shows up here.
        const DefinitionKey& third = definition.keys[2];
        Check(third.x == 2.5f && third.y == 1.25f, "row offsets accumulate on top of the row advance");
        Check(third.row == 0 && third.column == 0, "and it is matrix (0,0)");

        const DefinitionKey& last = definition.keys.back();
        Check(last.x == 22.0f && last.y == 8.0f, "the last key lands at the far corner");
        Check(last.row == 1 && last.column == 9, "with its own matrix cell");

        bool foundSpacebar = false;
        for (const DefinitionKey& key : definition.keys)
            if (key.width == 6.25f)
                foundSpacebar = true;

        Check(foundSpacebar, "a 6.25u key survived the width reset between keys");

        for (const DefinitionKey& key : definition.keys)
        {
            if (key.row >= definition.matrixRows || key.column >= definition.matrixColumns)
            {
                Check(false, "every key sits inside the declared matrix");
                return;
            }
        }
        Check(true, "every key sits inside the declared matrix");
    }

    void TestLayoutOptions()
    {
        std::printf("layout options\n");

        const KeyboardDefinition definition = DecodeDefinition(ModelFDefinition());

        int optional = 0;
        for (const DefinitionKey& key : definition.keys)
            if (key.layoutIndex >= 0)
                ++optional;

        Check(optional == 41, "41 keys belong to a layout option");

        const DefinitionKey& first = definition.keys.front();
        Check(first.layoutIndex == 0 && first.layoutOption == 1,
              "the fourth label becomes layoutIndex and layoutOption");

        Check(definition.keys[2].layoutIndex == -1,
              "a key with no fourth label is always present");

        Check(!definition.layoutLabels.empty(), "the layout option labels are kept");
    }

    void TestParseFailures()
    {
        std::printf("parse failures\n");

        auto bytes = [](const char* text)
        {
            return std::vector<uint8_t>(text, text + std::char_traits<char>::length(text));
        };

        Check(Throws([&] { (void)ParseDefinition(bytes("not json at all")); }),
              "invalid JSON is rejected");

        Check(Throws([&] { (void)ParseDefinition(bytes("[1,2,3]")); }),
              "JSON that is not an object is rejected");

        Check(Throws([&] { (void)ParseDefinition(bytes(R"({"name":"x"})")); }),
              "a definition with no matrix is rejected");

        Check(Throws([&] { (void)ParseDefinition(bytes(R"({"matrix":{"rows":0,"cols":0},"layouts":{"keymap":[]}})")); }),
              "an empty matrix is rejected");

        Check(Throws([&] { (void)ParseDefinition(bytes(R"({"matrix":{"rows":8,"cols":18}})")); }),
              "a definition with no layouts is rejected");

        Check(Throws([&] { (void)ParseDefinition(bytes(R"({"matrix":{"rows":8,"cols":18},"layouts":{"keymap":[["decal"]]}})")); }),
              "a keymap with no usable keys is rejected");
    }

    // A decal or legend-only entry has no "row,col" label. It must not become a key,
    // but it must still take up its space, or everything after it shifts left.
    void TestDecalsAdvanceWithoutBecomingKeys()
    {
        std::printf("decals\n");

        const char* text =
            R"({"matrix":{"rows":2,"cols":2},"layouts":{"keymap":[["0,0","decal","1,1"]]}})";

        const KeyboardDefinition definition =
            ParseDefinition(std::vector<uint8_t>(text, text + std::char_traits<char>::length(text)));

        Check(definition.keys.size() == 2, "the decal did not become a key");
        Check(definition.keys[1].x == 2.0f, "but it still advanced the position");
    }

    const DefinitionKey* FindKey(const KeyboardDefinition& definition, int row, int column, int option)
    {
        for (const DefinitionKey& key : definition.keys)
            if (!key.decal && key.row == row && key.column == column && key.layoutOption == option)
                return &key;
        return nullptr;
    }

    int CountDecals(const KeyboardDefinition& definition)
    {
        int decals = 0;
        for (const DefinitionKey& key : definition.keys)
            decals += key.decal ? 1 : 0;
        return decals;
    }

    // A key marked "d" in KLE is a decal even when its label names a matrix cell, and
    // it keeps its layout option -- lining up the choices depends on it.
    void TestSourceDecalMarkedWithD()
    {
        std::printf("source form: decals marked with d\n");

        const KeyboardDefinition definition = ParseDefinition(IsoMacroSource());

        Check(definition.keys.size() == 11, "7 keys, 2 per choice, and the decal");
        Check(CountDecals(definition) == 1, "one of them is a decal");

        const DefinitionKey& decal = definition.keys.front();
        Check(decal.decal && decal.x == 0.0f && decal.y == 0.0f, "the decal is the first entry, at the origin");
        Check(decal.layoutIndex == 0 && decal.layoutOption == 1, "and belongs to choice 1 of the option");

        const DefinitionKey* next = FindKey(definition, 2, 1, 0);
        Check(next != nullptr && next->x == 1.5f, "the decal still took its space");
    }

    // KLE details real definitions rely on, each found by comparing every board in VIA's
    // registry with VIA's own conversion of it (via-registry.md, "The converted-form entry").
    void TestKleDetails()
    {
        std::printf("KLE details from real definitions\n");

        auto parse = [](const char* keymap)
        {
            const std::string text = std::string(R"({"matrix":{"rows":4,"cols":4},"layouts":{"keymap":)") + keymap + "}}";
            return ParseDefinition(std::vector<uint8_t>(text.begin(), text.end()));
        };

        // bevi: a second property object must not undo the first one's width.
        const KeyboardDefinition consecutive = parse(R"([[{"w":2.25},{"c":"#777777"},"0,0","0,1"]])");
        Check(consecutive.keys[0].width == 2.25f && consecutive.keys[1].x == 2.25f,
              "a property object changes only what it names");

        // dz60: rx/ry move the cursor, and later rows start at rx -- with no rotation.
        const KeyboardDefinition cluster = parse(R"([["0,0"],[{"rx":0.25,"y":6.5,"x":13.5},"1,0"],["2,0"]])");
        Check(cluster.keys[1].x == 13.75f && cluster.keys[1].y == 6.5f, "rx/ry move the cursor to the cluster");
        Check(cluster.keys[2].x == 0.25f && cluster.keys[2].y == 7.5f, "and the next row starts at rx");

        // bm16a: a row with a property object and no key drops it.
        const KeyboardDefinition dropped = parse(R"([[{"x":4.25,"w":14,"h":5,"d":true}],["0,0","0,1"]])");
        Check(dropped.keys.size() == 2 && !dropped.keys[0].decal && dropped.keys[0].width == 1.0f &&
                  dropped.keys[0].x == 0.0f,
              "properties left at the end of a row do not reach the next one");

        // Rotation: r holds across keys and rows until the next r; each key turns about
        // the cluster origin rx/ry that was current when it was placed.
        const KeyboardDefinition rotated =
            parse(R"([[{"r":15,"rx":1,"ry":2},"0,0","0,1"],["1,0"],[{"r":-30,"rx":5},"2,0"],[{"r":0},"3,0"]])");
        Check(rotated.keys[0].rotation == 15.0f && rotated.keys[0].rotationX == 1.0f &&
                  rotated.keys[0].rotationY == 2.0f && rotated.keys[0].x == 1.0f && rotated.keys[0].y == 2.0f,
              "r, rx and ry give the angle and the origin, and move the cursor there");
        Check(rotated.keys[1].rotation == 15.0f && rotated.keys[2].rotation == 15.0f && rotated.keys[2].x == 1.0f &&
                  rotated.keys[2].y == 3.0f,
              "the angle holds for the next key and the next row");
        Check(rotated.keys[3].rotation == -30.0f && rotated.keys[3].rotationX == 5.0f &&
                  rotated.keys[3].rotationY == 2.0f && rotated.keys[3].y == 2.0f,
              "a new rx keeps the previous ry, and the cursor jumps to both");
        Check(rotated.keys[4].rotation == 0.0f && rotated.keys[4].rotationX == 5.0f,
              "r: 0 ends the rotation but keeps the cluster");

        // xelus pachi, stratos: spaces around the numbers of a label.
        const KeyboardDefinition spaced = parse(R"([["0,8    ","2,1\n\n\n1 ,0"]])");
        Check(spaced.keys.size() == 2 && spaced.keys[0].column == 8, "trailing spaces in a matrix cell are allowed");
        Check(spaced.keys[1].layoutIndex == 1 && spaced.keys[1].layoutOption == 0,
              "a space before the comma of a layout option is allowed");
    }

    // ogr, fallacy: string ids are hex even without "0x", as VIA reads them.
    void TestUsbIdStrings()
    {
        std::printf("USB id strings\n");

        auto ids = [](const char* vendor, const char* product)
        {
            const std::string text = std::string(R"({"vendorId":)") + vendor + R"(,"productId":)" + product +
                                     R"(,"matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0"]]}})";
            return ParseDefinition(std::vector<uint8_t>(text.begin(), text.end()));
        };

        const KeyboardDefinition bare = ids(R"("414B")", R"("BF00")");
        Check(bare.vendorId == 0x414B && bare.productId == 0xBF00, "a string with no 0x is still hex");
        Check(ids(R"("0X1209")", "4617").vendorId == 0x1209, "0X works, and a JSON number is taken as it is");

        Check(Throws([&] { (void)ids(R"("0x12345")", R"("1")"); }), "an id wider than 16 bits is rejected");
        Check(Throws([&] { (void)ids(R"("12G4")", R"("1")"); }), "an id that is not hex is rejected");
    }

    void TestConvertedForm()
    {
        std::printf("converted form\n");

        const KeyboardDefinition definition = ParseDefinition(IsoMacroConverted());

        Check(definition.name == "ISO Macro", "the name comes through");
        Check(definition.vendorId == 0x4D65 && definition.productId == 0x1200,
              "vendorProductId splits into the two USB ids");
        Check(definition.matrixRows == 3 && definition.matrixColumns == 3, "the matrix is 3x3");
        Check(definition.layoutLabels.size() == 1 && definition.layoutLabels[0] == "Single Encoder",
              "the layout option label is kept");

        Check(definition.keys.size() == 11, "7 keys, 2 per choice, and the decal");
        Check(CountDecals(definition) == 1, "the row -1 key marked d is a decal");

        const DefinitionKey* first = FindKey(definition, 0, 0, -1);
        Check(first != nullptr && first->x == 1.25f && first->y == 0.0f && first->layoutIndex == -1,
              "a key from layouts.keys keeps its absolute position and no option");

        const DefinitionKey* enter = FindKey(definition, 2, 0, -1);
        Check(enter != nullptr && enter->width == 1.25f && enter->height == 2.0f && enter->secondX == -0.25f &&
                  enter->secondWidth == 1.5f && enter->secondHeight == 1.0f,
              "ISO Enter keeps its second rectangle");

        const DefinitionKey* optional = FindKey(definition, 2, 2, 1);
        Check(optional != nullptr && optional->layoutIndex == 0 && optional->y == 1.0f,
              "optionKeys[group][choice] becomes layoutIndex and layoutOption");
    }

    void TestConvertedEdgeCases()
    {
        std::printf("converted form: edge cases\n");

        // An encoder drawn on the board has no matrix cell and is not a decal: skipped,
        // like a legend-only key in the source form. A cell id too large for a byte is
        // skipped too, rather than wrapped into a real cell.
        const KeyboardDefinition definition = ParseDefinition(IsoMacroBytes(
            R"({"name":{"options":["EC60X","DC60"],"content":["id_board_variant",0,245]},)"
            R"("vendorProductId":1298469376,"matrix":{"rows":1,"cols":2},"layouts":{"keys":[)"
            R"({"row":0,"col":0,"x":0,"y":0,"r":-10,"rx":2.5,"ry":-1},)"
            R"({"row":-1,"col":-1,"x":1,"y":0,"ei":0},)"
            R"({"row":4294967296,"col":1,"x":2,"y":0}]}})"));

        Check(definition.keys.size() == 1, "only the key with a real matrix cell is kept");
        Check(definition.keys[0].rotation == -10.0f && definition.keys[0].rotationX == 2.5f &&
                  definition.keys[0].rotationY == -1.0f,
              "r, rx and ry are read as they are");
        Check(definition.name == "EC60X", "a dynamic name gives its first option");

        Check(Throws([] { (void)ParseDefinition(IsoMacroBytes(
                  R"({"matrix":{"rows":1,"cols":1},"layouts":{"keys":[{"row":-1,"col":-1,"d":true}]}})")); }),
              "a layout of decals only is rejected");

        Check(Throws([] { (void)ParseDefinition(IsoMacroBytes(
                  R"({"vendorProductId":4294967296,"matrix":{"rows":1,"cols":1},"layouts":{"keys":[{"row":0,"col":0}]}})")); }),
              "a vendorProductId wider than 32 bits is rejected");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestDecompression();
    TestDecompressionRejectsRubbish();
    TestDefinitionFields();
    TestKeyGeometry();
    TestLayoutOptions();
    TestParseFailures();
    TestDecalsAdvanceWithoutBecomingKeys();
    TestSourceDecalMarkedWithD();
    TestKleDetails();
    TestUsbIdStrings();
    TestConvertedForm();
    TestConvertedEdgeCases();

    return TestResult();
}
