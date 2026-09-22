// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Decodes a real keyboard's definition: XZ through minlzma, then JSON through
// nlohmann, then the KLE walk that turns it into keys with matrix positions.
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

#include "adapters/vial/NazgVialDefinition.h"
#include "adapters/via/NazgViaProtocol.h"

#include "ModelFDefinition.h"
#include "TestSupport.h"

#include <string>
#include <vector>

using nazg::DecodeDefinition;
using nazg::DecompressDefinition;
using nazg::DefinitionKey;
using nazg::ParseDefinition;
using nazg::ProtocolError;
using nazg::VialDefinition;

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

        const VialDefinition definition = DecodeDefinition(ModelFDefinition());

        Check(definition.name == "leyden_jar/B104", "the name comes through");
        Check(definition.vendorId == 0x1209, "the vendor id parses from its \"0x1209\" string");
        Check(definition.productId == 0x4704, "the product id parses the same way");
        Check(definition.lighting == "none", "lighting is reported");
        Check(definition.matrixRows == 8 && definition.matrixColumns == 18, "the matrix is 8x18");
    }

    void TestKeyGeometry()
    {
        std::printf("key geometry\n");

        const VialDefinition definition = DecodeDefinition(ModelFDefinition());

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

        const VialDefinition definition = DecodeDefinition(ModelFDefinition());

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

        const VialDefinition definition =
            ParseDefinition(std::vector<uint8_t>(text, text + std::char_traits<char>::length(text)));

        Check(definition.keys.size() == 2, "the decal did not become a key");
        Check(definition.keys[1].x == 2.0f, "but it still advanced the position");
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

    return TestResult();
}
