// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The real community bundle, all of it, through Nazg's own checks.
//
// The bundle is fetched by tools/update_community_bundle.py into build_resources/ and is not in
// the repository, so this test SKIPS when it is not there, as via_bundle_contents does. When it
// is there, a new release of nazg-community-definitions holding a file Nazg cannot use fails
// here, not on a user's board.
//
// Every definition the index lists is checked as any bundled definition is
// (adapters/via/NazgDefinitionCheck.h), and must be the board the index says: its name and
// VID:PID. Find() must return it for that VID:PID.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgCommunityBundle.h"
#include "adapters/via/NazgDefinitionCheck.h"

#include "TestSupport.h"

#include <algorithm>
#include <cstdio>
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
        std::printf("no bundle at %s -- run tools/update_community_bundle.py; skipped\n", path.string().c_str());
        return c_Skipped;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    std::printf("the bundle at %s\n", path.string().c_str());
    const nazg::CommunityDefinitionBundle bundle(bytes);

    std::vector<std::string> failures;
    size_t                   passed = 0;
    for (const nazg::CommunityDefinition& entry : bundle.Definitions())
    {
        const nazg::DefinitionCheck check = nazg::CheckDefinition(*bundle.Bytes(entry.path));
        for (const std::string& problem : check.problems)
            failures.push_back(entry.path + ": " + problem);
        if (!check.definition)
            continue;

        const auto found = bundle.Find(entry.vendorId, entry.productId);
        if (check.definition->vendorId != entry.vendorId || check.definition->productId != entry.productId)
            failures.push_back(entry.path + ": not the VID:PID the index says");
        else if (check.definition->name != entry.name)
            failures.push_back(entry.path + ": named " + check.definition->name + ", the index says " + entry.name);
        else if (std::find(found.begin(), found.end(), &entry) == found.end())
            failures.push_back(entry.path + ": not found under its VID:PID");
        else if (check.Passed())
            ++passed;
    }

    for (const std::string& failure : failures)
        std::printf("  %s\n", failure.c_str());

    const auto& manifest = bundle.Manifest();
    std::printf("  %zu definitions, release %s\n", passed, manifest ? manifest->release.c_str() : "(none)");

    Check(failures.empty(), "every definition passes Nazg's checks, is the board the index says, and a lookup finds it");
    Check(manifest && passed == static_cast<size_t>(manifest->definitions), "and the manifest counts them all");
    Check(manifest && !manifest->release.empty(), "and names its release");
    Check(passed > 0, "the bundle is not empty");

    return TestResult();
}
