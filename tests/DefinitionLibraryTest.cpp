// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The library of user definitions, on a real folder: a fresh one under the system's
// temporary directory for each case, removed afterwards.
//
// What matters most is what is NOT lost: a definition that does not parse never gets in,
// an index that cannot be read is left alone rather than replaced, and a number once
// given is never given again.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "library/NazgDefinitionLibrary.h"

#include "adapters/via/NazgViaProtocol.h"

#include "IsoMacroDefinition.h"
#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using nazg::DefinitionLibrary;
using nazg::LibraryEntry;
using nazg::LibraryError;
using nazg::ProtocolError;

namespace fs = std::filesystem;

namespace
{
    // A folder of its own for one case, deleted when the case ends.
    struct TemporaryFolder
    {
        fs::path path;

        TemporaryFolder()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("nazg-library-test-" + std::to_string(stamp));
        }

        ~TemporaryFolder()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };

    template <typename TError, typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const TError&)
        {
            return true;
        }
        return false;
    }

    std::string ReadText(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    size_t CountFiles(const fs::path& folder)
    {
        return static_cast<size_t>(std::distance(fs::directory_iterator(folder), fs::directory_iterator()));
    }

    void TestImport()
    {
        std::printf("import\n");

        TemporaryFolder folder;
        DefinitionLibrary library(folder.path);

        Check(library.Entries().empty(), "a new library is empty");
        Check(fs::is_directory(folder.path / "user_definitions"), "and has made its folder");

        const LibraryEntry& entry = library.Import(IsoMacroSource(), "D:/boards/iso_macro.json", "2026-09-24T20:00:00Z");
        Check(entry.id == 1 && entry.revision == 1, "the first import is entry 1, revision 1");
        Check(entry.name == "ISO Macro" && entry.vendorId == 0x4D65 && entry.productId == 0x1200,
              "its name and ids come from the definition");

        Check(fs::exists(folder.path / "user_definitions" / "1-r1.json"), "its file is user_definitions/1-r1.json");
        Check(library.Read(entry) == IsoMacroSource(), "stored byte for byte as imported");
        Check(fs::exists(folder.path / "user_definitions" / "index.json"), "and the index is written");

        Check(library.Find(0x4D65, 0x1200) == &library.Entries().front(), "it is found by VID:PID");
        Check(library.Find(0x4D65, 0x1201) == nullptr, "and nothing else is");
    }

    void TestReopen()
    {
        std::printf("reopen\n");

        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "first", "2026-09-24T20:00:00Z");
            (void)library.Import(IsoMacroConverted(), "second", "2026-09-24T20:01:00Z");
        }

        DefinitionLibrary reopened(folder.path);
        Check(reopened.Entries().size() == 2, "both entries come back");
        Check(reopened.Entries()[1].id == 2 && reopened.Entries()[1].origin == "second" &&
                  reopened.Entries()[1].added == "2026-09-24T20:01:00Z",
              "with every field");
        Check(reopened.Find(0x4D65, 0x1200)->origin == "first", "the first import wins a shared VID:PID, for now");
        Check(reopened.Read(reopened.Entries()[1]) == IsoMacroConverted(), "and the files are still there");
    }

    void TestRejects()
    {
        std::printf("rejected imports\n");

        TemporaryFolder folder;
        DefinitionLibrary library(folder.path);

        Check(Throws<ProtocolError>([&] { (void)library.Import(IsoMacroBytes("not json"), "x", "t"); }),
              "a file that does not parse is refused");
        Check(Throws<ProtocolError>([&] {
                  (void)library.Import(IsoMacroBytes(R"({"matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0"]]}})"),
                                       "x", "t");
              }),
              "and so is one with no USB ids to match a board by");

        Check(library.Entries().empty() && CountFiles(folder.path / "user_definitions") == 0,
              "neither left anything behind, not even an index");
    }

    void TestNumbersNeverReused()
    {
        std::printf("numbers\n");

        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "a", "t");
            (void)library.Import(IsoMacroSource(), "b", "t");
            library.Remove(2);
            Check(!fs::exists(folder.path / "user_definitions" / "2-r1.json"), "a removed entry's file is deleted");
            library.Remove(99);
            Check(library.Entries().size() == 1, "removing an unknown number does nothing");
        }

        DefinitionLibrary reopened(folder.path);
        const LibraryEntry& next = reopened.Import(IsoMacroSource(), "c", "t");
        Check(next.id == 3, "after removing entry 2 and reopening, the next import is 3, not 2");
    }

    void TestOrphans()
    {
        std::printf("orphans\n");

        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "a", "t");
        }

        // What a crash between writing a file and writing the index leaves behind.
        std::ofstream(folder.path / "user_definitions" / "7-r1.json") << "{}";
        std::ofstream(folder.path / "user_definitions" / "index.json.tmp") << "{";

        DefinitionLibrary reopened(folder.path);
        Check(!fs::exists(folder.path / "user_definitions" / "7-r1.json"), "a file no entry names is deleted on open");
        Check(!fs::exists(folder.path / "user_definitions" / "index.json.tmp"), "and so is a half-written index");
        Check(fs::exists(folder.path / "user_definitions" / "1-r1.json"), "the entries' own files stay");
        Check(fs::exists(folder.path / "user_definitions" / "index.json"), "and so does the index beside them");
    }

    void TestDamagedIndexIsLeftAlone()
    {
        std::printf("a damaged index\n");

        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "a", "t");
        }

        const std::string damaged = R"({"format":1,"nextId":2,"definitions":[{"id":1)";
        std::ofstream(folder.path / "user_definitions" / "index.json", std::ios::binary) << damaged;

        Check(Throws<LibraryError>([&] { DefinitionLibrary library(folder.path); }), "it refuses to open");
        Check(ReadText(folder.path / "user_definitions" / "index.json") == damaged, "the index is left as it was");
        Check(fs::exists(folder.path / "user_definitions" / "1-r1.json"), "and no definition is treated as an orphan");

        std::ofstream(folder.path / "user_definitions" / "index.json", std::ios::binary) << R"({"format":2,"nextId":1,"definitions":[]})";
        Check(Throws<LibraryError>([&] { DefinitionLibrary library(folder.path); }),
              "an index from a newer format is refused too");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestImport();
    TestReopen();
    TestRejects();
    TestNumbersNeverReused();
    TestOrphans();
    TestDamagedIndexIsLeftAlone();

    return TestResult();
}
