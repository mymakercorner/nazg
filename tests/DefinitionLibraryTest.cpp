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

        const auto candidates = library.Candidates(0x4D65, 0x1200);
        Check(candidates.size() == 1 && candidates[0].ref == nazg::DefinitionRef::User(1) &&
                  candidates[0].definition.name == "ISO Macro" && candidates[0].origin == "D:/boards/iso_macro.json",
              "it is a candidate for its VID:PID, parsed");
        Check(library.Candidates(0x4D65, 0x1201).empty(), "and for nothing else");
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
        const auto candidates = reopened.Candidates(0x4D65, 0x1200);
        Check(candidates.size() == 2 && candidates[0].origin == "first" && candidates[1].origin == "second",
              "both are candidates for their shared VID:PID, in import order");
        Check(reopened.Read(reopened.Entries()[1]) == IsoMacroConverted(), "and the files are still there");
    }

    nazg::DeviceIdentity IsoMacroBoard(uint16_t release = 0x0001)
    {
        return { 0x4D65, 0x1200, "Rico", "ISO Macro", release, "" };
    }

    void TestChoices()
    {
        std::printf("choices\n");

        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "a", "t");
            (void)library.Import(IsoMacroConverted(), "b", "t");

            Check(library.FindChoice(IsoMacroBoard()) == nullptr, "a new library has no choice");

            library.Choose(IsoMacroBoard(), nazg::DefinitionRef::User(1));
            library.Choose(IsoMacroBoard(), nazg::DefinitionRef::User(2));
            Check(library.Choices().size() == 1 && library.FindChoice(IsoMacroBoard())->definition ==
                                                        nazg::DefinitionRef::User(2),
                  "choosing again for the same device replaces its choice");

            library.Choose(IsoMacroBoard(0x0002), nazg::DefinitionRef::Official("v3/1298469376"));
            Check(library.Choices().size() == 2, "another release of the board gets a choice of its own");
        }

        DefinitionLibrary reopened(folder.path);
        Check(reopened.Choices().size() == 2, "choices come back with the index");

        const nazg::DefinitionChoice* found = reopened.FindChoice(IsoMacroBoard(0x0002));
        Check(found != nullptr && found->device == IsoMacroBoard(0x0002) &&
                  found->definition == nazg::DefinitionRef::Official("v3/1298469376"),
              "with every field");

        reopened.Remove(2);
        Check(reopened.Choices().size() == 1 && reopened.FindChoice(IsoMacroBoard(0x0001))->definition ==
                                                    nazg::DefinitionRef::Official("v3/1298469376"),
              "removing an entry forgets the choices pointing at it; the release's near match remains");

        reopened.Choose(IsoMacroBoard(0x0003), nazg::DefinitionRef::User(1));
        reopened.Forget(IsoMacroBoard(0x0009));
        Check(reopened.Choices().empty(), "forgetting clears every choice of the board, whatever its release");

        DefinitionLibrary again(folder.path);
        Check(again.Choices().empty(), "and stays forgotten");
    }

    std::vector<uint8_t> OtherBoard()
    {
        return IsoMacroBytes(R"({"name":"Other","vendorId":"0x1234","productId":"0x5678",)"
                             R"("matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0"]]}})");
    }

    void TestReplace()
    {
        std::printf("replacing and restoring\n");

        const fs::path  files = "user_definitions";
        TemporaryFolder folder;
        {
            DefinitionLibrary library(folder.path);
            (void)library.Import(IsoMacroSource(), "D:/iso_macro.json", "2026-09-25T10:00:00Z");
            library.Choose(IsoMacroBoard(), nazg::DefinitionRef::User(1));

            Check(library.FindSameBoard(nazg::ParseDefinition(IsoMacroConverted())) == &library.Entries()[0],
                  "a file with the same VID:PID and name is for the same board");
            Check(library.FindSameBoard(nazg::ParseDefinition(OtherBoard())) == nullptr, "another is not");

            const LibraryEntry& second = library.Replace(1, IsoMacroConverted(), "D:/new/iso_macro.json");
            Check(second.id == 1 && second.revision == 2 && second.previousRevision == 1,
                  "a replacement keeps the number: revision 2, revision 1 kept");
            Check(second.origin == "D:/new/iso_macro.json" && second.added == "2026-09-25T10:00:00Z",
                  "it records where it came from, and keeps when the entry was first imported");
            Check(library.Read(second) == IsoMacroConverted(), "the new version is read");
            Check(fs::exists(folder.path / files / "1-r1.json") && fs::exists(folder.path / files / "1-r2.json"),
                  "both files are there");
            Check(library.FindChoice(IsoMacroBoard())->definition == nazg::DefinitionRef::User(1),
                  "and the choice still points at the entry");

            (void)library.Replace(1, IsoMacroConverted(), "E:/moved/iso_macro.json");
            Check(library.Entries()[0].revision == 2 && library.Entries()[0].previousRevision == 1 &&
                      library.Entries()[0].origin == "E:/moved/iso_macro.json",
                  "the same bytes again make no revision -- the backup stays -- and only the origin moves");

            (void)library.Replace(1, IsoMacroSource(), "D:/iso_macro.json");
            Check(library.Entries()[0].revision == 3 && library.Entries()[0].previousRevision == 2 &&
                      !fs::exists(folder.path / files / "1-r1.json"),
                  "a third version keeps only the one before it");

            const LibraryEntry& restored = library.RestorePrevious(1);
            Check(restored.revision == 2 && restored.previousRevision == 3 &&
                      library.Read(restored) == IsoMacroConverted(),
                  "restoring swaps the current version and the previous one");

            (void)library.Replace(1, OtherBoard(), "D:/other.json");
            const LibraryEntry& other = library.Entries()[0];
            Check(other.revision == 4 && other.previousRevision == 2 && !fs::exists(folder.path / files / "1-r3.json"),
                  "a version after a restore numbers past both, and drops the one the restore left aside");
            Check(other.name == "Other" && other.vendorId == 0x1234 && other.productId == 0x5678,
                  "name and ids follow the new version");
            Check(library.Candidates(0x4D65, 0x1200).empty(), "which is no longer a candidate for the old ids");

            Check(Throws<ProtocolError>([&] { (void)library.Replace(1, IsoMacroBytes("not json"), "x"); }),
                  "a version that does not parse is refused");
            Check(library.Entries()[0].revision == 4 && CountFiles(folder.path / files) == 3,
                  "and leaves the entry and its files as they were");
            Check(Throws<LibraryError>([&] { (void)library.Replace(9, IsoMacroSource(), "x"); }),
                  "replacing an unknown entry is refused");
        }

        DefinitionLibrary reopened(folder.path);
        Check(reopened.Entries()[0].previousRevision == 2 && fs::exists(folder.path / files / "1-r2.json"),
              "the backup comes back with the index, and is not taken for an orphan");

        (void)reopened.Import(IsoMacroSource(), "b", "t");
        Check(Throws<LibraryError>([&] { (void)reopened.RestorePrevious(2); }),
              "an entry never replaced has nothing to restore");

        reopened.Remove(1);
        Check(!fs::exists(folder.path / files / "1-r4.json") && !fs::exists(folder.path / files / "1-r2.json"),
              "removing an entry deletes its backup too");
    }

    void TestIndexWithoutChoices()
    {
        std::printf("an index from before choices\n");

        TemporaryFolder folder;
        fs::create_directories(folder.path / "user_definitions");
        std::ofstream(folder.path / "user_definitions" / "index.json", std::ios::binary)
            << R"({"format":1,"nextId":1,"definitions":[]})";

        DefinitionLibrary library(folder.path);
        Check(library.Choices().empty(), "opens, with no choices");

        std::ofstream(folder.path / "user_definitions" / "index.json", std::ios::binary)
            << R"({"format":1,"nextId":1,"definitions":[],"choices":[{"vendorId":"0x4D65","productId":"0x1200",)"
               R"("manufacturer":"","product":"","release":"0x0001","serial":"","definition":"community:3"}]})";
        Check(Throws<LibraryError>([&] { DefinitionLibrary damaged(folder.path); }),
              "a choice pointing at a kind of definition this Nazg does not know is refused, not dropped");
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
    TestChoices();
    TestReplace();
    TestIndexWithoutChoices();
    TestRejects();
    TestNumbersNeverReused();
    TestOrphans();
    TestDamagedIndexIsLeftAlone();

    return TestResult();
}
