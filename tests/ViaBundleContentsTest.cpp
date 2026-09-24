// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The real bundle of VIA's official definitions, all of it, through Nazg's own parser.
//
// The bundle is built by tools/update_via_bundle.py into build_resources/ and is not in the
// repository, so this test SKIPS when it is not there -- a fresh clone builds and passes
// without Python or a network. When it is there, a refresh of the pin that brings in a
// definition Nazg cannot read fails here, not on a user's board.
//
// Every file is checked for what a lookup relies on: it parses, and the VID:PID inside it
// is the one its name says.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/NazgXz.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaBundle.h"

#include "TestSupport.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
    // CTest's SKIP_RETURN_CODE for this test, set in CMakeLists.txt.
    constexpr int c_Skipped = 77;
}

int main(int argc, char** argv)
{
    ConfigureCrtReporting();

    const std::filesystem::path path = argc > 1 ? argv[1] : "";
    std::ifstream               stream(path, std::ios::binary);
    if (!stream)
    {
        std::printf("no bundle at %s -- run tools/update_via_bundle.py; skipped\n", path.string().c_str());
        return c_Skipped;
    }
    const std::vector<uint8_t> bundle((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    std::printf("the bundle at %s\n", path.string().c_str());

    const std::vector<uint8_t> tar = nazg::DecompressXz(bundle, 256u * 1024u * 1024u, "the bundle");

    int                      parsed[2]  = { 0, 0 };   // V2, V3
    int                      manifest[2] = { -1, -1 };
    std::vector<std::string> failures;

    nazg::ForEachTarFile(tar, [&](const std::string& name, const uint8_t* data, size_t size)
    {
        const std::vector<uint8_t> bytes(data, data + size);

        if (name == "manifest.json")
        {
            const nlohmann::json document = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
            manifest[0] = document.value("v2", -1);
            manifest[1] = document.value("v3", -1);
            return true;
        }

        const bool isV3 = name.rfind("v3/", 0) == 0;
        if (!isV3 && name.rfind("v2/", 0) != 0)
        {
            failures.push_back(name + ": unexpected in the bundle");
            return true;
        }

        try
        {
            const nazg::KeyboardDefinition definition = nazg::ParseDefinition(bytes);
            const uint32_t id = (static_cast<uint32_t>(definition.vendorId) << 16) | definition.productId;
            if (name.substr(3) != std::to_string(id) + ".json")
                failures.push_back(name + ": holds " + std::to_string(id));
            else
                ++parsed[isV3 ? 1 : 0];
        }
        catch (const std::exception& failure)
        {
            failures.push_back(name + ": " + failure.what());
        }
        return true;
    });

    for (const std::string& failure : failures)
        std::printf("  %s\n", failure.c_str());

    std::printf("  %d V3 and %d V2 definitions\n", parsed[1], parsed[0]);
    Check(failures.empty(), "every definition parses, under the id its name says");
    Check(parsed[1] == manifest[1] && parsed[0] == manifest[0], "and the manifest counts them all");
    Check(parsed[1] > 0, "the bundle is not empty");

    return TestResult();
}
