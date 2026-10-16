// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What the user does with definition files, against a library in a temporary folder: an
// import, the same board imported again -- set aside for Replace or Keep both -- each answer,
// a re-import from a file that moved, removal, and the words each one leaves for Settings.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "library/NazgLibraryActions.h"

#include "files/NazgFiles.h"

#include "IsoMacroDefinition.h"
#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using nazg::LibraryEntry;
using nazg::UserLibrary;

namespace
{
    constexpr char c_Now[] = "2026-10-16T12:00:00Z";

    // A folder of its own for one case, deleted when the case ends.
    struct TemporaryFolder
    {
        fs::path path;

        TemporaryFolder()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("nazg-library-actions-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }

        ~TemporaryFolder()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };

    std::string WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return nazg::Utf8FromPath(path);
    }

    bool StartsWith(const std::string& text, const std::string& start)
    {
        return text.rfind(start, 0) == 0;
    }

    void TestNames()
    {
        std::printf("names\n");

        Check(nazg::ExportFileName("Phoenix Project No 1") == "Phoenix_Project_No_1.json", "spaces become underscores");
        Check(nazg::ExportFileName("Model F / B104 ") == "Model_F_B104.json", "what a file system refuses goes");
        Check(nazg::ExportFileName("???") == "definition.json", "a name with nothing left");
        Check(nazg::FileName("C:/definitions/iso.json") == "iso.json", "a path's file name");

        LibraryEntry entry;
        entry.vendorId  = 0x4D65;
        entry.productId = 0x1200;
        const std::string note = nazg::ForeignImportNote("C:/x/iso.json", entry, { 0xFEED, 0x0001, {}, {}, 0, {} });
        Check(StartsWith(note, "iso.json is the definition of another keyboard (4D65:1200), not this one (FEED:0001)"),
              "a file for another keyboard says which, and which this one is");
    }

    void TestNoFolder()
    {
        std::printf("no data folder\n");

        const UserLibrary library = nazg::OpenUserLibrary(std::nullopt, "no folder for Nazg's data");
        Check(!library.library && library.error == "no folder for Nazg's data", "no library, and why");
    }

    void TestImportAndAnswers()
    {
        std::printf("import, again, and the answers\n");

        TemporaryFolder   data;
        TemporaryFolder   files;
        const std::string iso = WriteFile(files.path / "iso.json", IsoMacroConverted());

        UserLibrary library = nazg::OpenUserLibrary(data.path, {});
        Check(library.library.has_value() && library.error.empty(), "the library opens");

        auto imported = nazg::ImportDefinitions(library, { iso }, true, c_Now);
        Check(imported.size() == 1 && imported[0].second.name == "ISO Macro" && imported[0].second.added == c_Now,
              "imported, with the time it was added");
        Check(library.messages.size() == 1 && library.messages[0] == "imported iso.json: ISO Macro", "and said so");

        imported = nazg::ImportDefinitions(library, { iso }, true, c_Now);
        Check(imported.empty() && library.replacements.size() == 1 && library.messages.empty(),
              "the same board again: set aside for the user's answer, nothing imported yet");

        const nazg::PendingReplacement pending = library.replacements.front();
        Check(!nazg::ReplaceDefinition(library, pending.entryId, pending.bytes, pending.path) &&
                  library.messages.back() == "iso.json is unchanged -- ISO Macro stays as it was",
              "Replace with the same bytes: no new version, and said so");

        const LibraryEntry* both = nazg::ImportKeepingBoth(library, pending, c_Now);
        Check(both != nullptr && library.library->Entries().size() == 2, "Keep both: a second entry");

        imported = nazg::ImportDefinitions(library, { nazg::Utf8FromPath(files.path / "missing.json") }, true, c_Now);
        Check(imported.empty() && library.messages.size() == 1 &&
                  library.messages[0] == "not imported missing.json: cannot open the file",
              "a file that is not there: not imported, and why");
    }

    void TestSettingsActions()
    {
        std::printf("re-import, remove\n");

        TemporaryFolder data;
        TemporaryFolder files;
        const fs::path  path = files.path / "iso.json";
        const std::string iso = WriteFile(path, IsoMacroConverted());

        UserLibrary library = nazg::OpenUserLibrary(data.path, {});
        const uint32_t id   = nazg::ImportDefinitions(library, { iso }, false, c_Now).at(0).second.id;

        fs::remove(path);
        Check(!nazg::ReimportDefinition(library, id) && library.messages.size() == 1 &&
                  StartsWith(library.messages[0], "failed: cannot read ") &&
                  library.messages[0].find("answer Replace") != std::string::npos,
              "re-import from a file that moved: refused, with what to do instead");

        WriteFile(path, IsoMacroSource());
        Check(nazg::ReimportDefinition(library, id) && StartsWith(library.messages.at(0), "replaced ISO Macro with iso.json"),
              "re-import of a changed file: a new version");

        Check(nazg::RestorePreviousDefinition(library, id) &&
                  library.messages.at(0) == "restored the previous version of ISO Macro",
              "restore: the version before");

        Check(nazg::RemoveDefinition(library, id) && library.library->Entries().empty(), "remove: gone");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestNames();
    TestNoFolder();
    TestImportAndAnswers();
    TestSettingsActions();

    return TestResult();
}
