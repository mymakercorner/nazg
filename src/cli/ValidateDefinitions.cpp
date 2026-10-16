// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// nazg-validate: VIA definitions through Nazg's own checks (adapters/via/NazgDefinitionCheck.h),
// without a window or a board. For the community bundle's seed and its CI, and for anyone
// writing a definition (via-registry.md, "Sharing: three levels").
//
//   nazg-validate [--json] [--all] <file or folder>...
//
// A folder is walked for .json files, any case. Each file that fails is listed with what is
// wrong; --all lists the ones that pass too, with their board and VID:PID. --json prints one
// JSON array instead, an object per file, for scripts -- with the board's name, VID:PID, matrix
// and wiring when it parsed. Exit code: 0 when every file passes, 1
// when one fails, 2 on a usage error or an unreadable path.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "adapters/via/NazgDefinitionCheck.h"
#include "files/NazgFiles.h"

namespace
{
    namespace fs = std::filesystem;

    bool IsJson(const fs::path& path)
    {
        std::string extension = nazg::Utf8FromPath(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".json";
    }

    std::string Hex(uint16_t value)
    {
        char text[8];
        std::snprintf(text, sizeof text, "0x%04X", value);
        return text;
    }

    int Usage()
    {
        std::fprintf(stderr, "usage: nazg-validate [--json] [--all] <file or folder>...\n");
        return 2;
    }
}

int main(int argc, char** argv)
{
    bool                  json = false, all = false;
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--json")
            json = true;
        else if (argument == "--all")
            all = true;
        else if (argument.rfind("--", 0) == 0)
            return Usage();
        else
        {
            const fs::path  path = fs::u8path(argument);
            std::error_code error;
            if (fs::is_directory(path, error))
            {
                for (const fs::directory_entry& entry : fs::recursive_directory_iterator(path, error))
                    if (entry.is_regular_file() && IsJson(entry.path()))
                        files.push_back(entry.path());
            }
            else if (fs::is_regular_file(path, error))
                files.push_back(path);
            else
            {
                std::fprintf(stderr, "nazg-validate: no file or folder %s\n", argument.c_str());
                return 2;
            }
        }
    }
    if (files.empty())
        return Usage();
    std::sort(files.begin(), files.end());

    nlohmann::json report = nlohmann::json::array();
    size_t         failed = 0;
    for (const fs::path& file : files)
    {
        // Unreadable, it is empty -- and fails as a definition that does not parse.
        const std::vector<uint8_t> bytes = nazg::ReadWholeFile(file).value_or(std::vector<uint8_t>());
        const nazg::via::DefinitionCheck check = nazg::via::CheckDefinition(bytes);
        if (!check.Passed())
            ++failed;

        if (json)
        {
            nlohmann::json entry = { { "file", nazg::Utf8FromPath(file) }, { "passed", check.Passed() }, { "problems", check.problems } };
            if (check.definition)
            {
                entry["name"]      = check.definition->name;
                entry["vendorId"]  = Hex(check.definition->vendorId);
                entry["productId"] = Hex(check.definition->productId);
                entry["matrix"]    = { check.definition->matrixRows, check.definition->matrixColumns };

                // The switches the keys are wired to, "row,column", sorted: two files with the same
                // VID:PID and the same wiring describe one board.
                std::vector<std::string> wiring;
                for (const nazg::via::DefinitionKey& key : check.definition->keys)
                    if (key.IsSwitch())
                        wiring.push_back(std::to_string(key.row) + "," + std::to_string(key.column));
                std::sort(wiring.begin(), wiring.end());
                wiring.erase(std::unique(wiring.begin(), wiring.end()), wiring.end());
                entry["wiring"] = wiring;
            }
            report.push_back(std::move(entry));
        }
        else if (!check.Passed())
            for (const std::string& problem : check.problems)
                std::printf("FAIL %s: %s\n", nazg::Utf8FromPath(file).c_str(), problem.c_str());
        else if (all)
            std::printf("ok   %s: %s, %s:%s\n", nazg::Utf8FromPath(file).c_str(), check.definition->name.c_str(),
                        Hex(check.definition->vendorId).c_str(), Hex(check.definition->productId).c_str());
    }

    if (json)
        // A file's bytes can reach a message; what is not UTF-8 is replaced rather than thrown on.
        std::printf("%s\n", report.dump(1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    else
        std::printf("%zu files, %zu pass, %zu fail\n", files.size(), files.size() - failed, failed);
    return failed == 0 ? 0 : 1;
}
