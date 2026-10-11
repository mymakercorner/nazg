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
// Every file is checked for what a lookup relies on: it parses, the VID:PID inside it is
// the one its name says, and ViaDefinitionBundle::Find() returns it for those ids.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/NazgXz.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaBundle.h"
#include "adapters/via/NazgViaCustomMenu.h"

#include <nlohmann/json.hpp>

#include "TestSupport.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

    const std::vector<uint8_t>      tar = nazg::DecompressXz(bundle, 256u * 1024u * 1024u, "the bundle");
    const nazg::ViaDefinitionBundle kept(bundle);

    int                      parsed[2] = { 0, 0 };   // V2, V3
    std::vector<std::string> failures;

    // A board's own menus: every control the definition writes is one Nazg draws -- VIA's
    // color-palette aside, which none uses -- and every condition one it reads.
    size_t menus = 0, controls = 0;
    const auto typedEntries = [](const nlohmann::json& entry, const auto& self) -> size_t
    {
        if (!entry.is_object())
            return 0;
        if (entry.contains("type"))
            return entry["type"] != "color-palette" ? 1 : 0;
        size_t count = 0;
        if (const auto content = entry.find("content"); content != entry.end() && content->is_array())
            for (const nlohmann::json& each : *content)
                count += self(each, self);
        return count;
    };
    const auto checkMenus = [&](const std::string& name, const std::vector<uint8_t>& bytes,
                                const nazg::KeyboardDefinition& definition)
    {
        size_t written = 0;
        for (const nlohmann::json& entry : nlohmann::json::parse(bytes).value("menus", nlohmann::json::array()))
            written += typedEntries(entry, typedEntries);

        size_t                   drawn = 0;
        std::vector<std::string> conditions;
        for (const nazg::CustomMenu& menu : definition.customMenus)
        {
            conditions.insert(conditions.end(), menu.conditions.begin(), menu.conditions.end());
            for (const nazg::CustomSubmenu& submenu : menu.submenus)
            {
                conditions.insert(conditions.end(), submenu.conditions.begin(), submenu.conditions.end());
                for (const nazg::CustomControl& control : submenu.controls)
                {
                    ++drawn;
                    conditions.insert(conditions.end(), control.conditions.begin(), control.conditions.end());
                }
            }
        }
        if (drawn != written)
            failures.push_back(name + ": " + std::to_string(written) + " menu controls, " + std::to_string(drawn) + " read");
        for (const std::string& condition : conditions)
            if (!nazg::IsReadableCondition(condition))
                failures.push_back(name + ": a condition Nazg cannot read: " + condition);
        menus += definition.customMenus.size();
        controls += drawn;
    };

    nazg::ForEachTarFile(tar, [&](const std::string& name, const uint8_t* data, size_t size)
    {
        const std::vector<uint8_t> bytes(data, data + size);

        if (name == "manifest.json")
            return true;

        const bool isV3 = name.rfind("v3/", 0) == 0;
        if (!isV3 && name.rfind("v2/", 0) != 0)
        {
            failures.push_back(name + ": unexpected in the bundle");
            return true;
        }

        try
        {
            const nazg::KeyboardDefinition definition = nazg::ParseDefinition(bytes);
            if (isV3)
                checkMenus(name, bytes, definition);
            const uint32_t id = (static_cast<uint32_t>(definition.vendorId) << 16) | definition.productId;
            // What a board with these ids gets from the kept bundle, at a protocol that
            // picks this file's version.
            const auto found = kept.Find(definition.vendorId, definition.productId, isV3 ? 12 : 10);

            if (name.substr(3) != std::to_string(id) + ".json")
                failures.push_back(name + ": holds " + std::to_string(id));
            else if (found != bytes)
                failures.push_back(name + ": not what a lookup of its ids returns");
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

    const auto& manifest = kept.Manifest();

    std::printf("  %d V3 and %d V2 definitions; %zu custom menus, %zu controls\n", parsed[1], parsed[0], menus, controls);
    Check(failures.empty(), "every definition parses, under the id its name says, and a lookup finds it");
    Check(manifest && parsed[1] == manifest->v3 && parsed[0] == manifest->v2, "and the manifest counts them all");
    Check(manifest && manifest->commit.size() == 40, "and names the commit they come from");
    Check(parsed[1] > 0, "the bundle is not empty");

    return TestResult();
}
