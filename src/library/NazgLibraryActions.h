// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// LibraryActions - what the user does with definition files -- import, replace, re-import,
// restore, remove, export -- each saying what it did, in the words Settings and the picker
// show. Main.cpp asks the questions (Replace or Keep both) and shows the words; the work, and
// its wording, is here, where a test can reach it.
//
// User definitions all live in user_definitions\ of the data folder, index.json included
// (library/NazgDefinitionLibrary.h). A library that cannot be opened -- a damaged index, which
// it leaves untouched -- is reported, and the app runs without one.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "library/NazgDefinitionChoice.h"
#include "library/NazgDefinitionLibrary.h"

namespace nazg
{
    // A file picked for import that is for a board the library already has a definition of --
    // same VID:PID and name -- waiting for the user to say Replace or Keep both.
    struct PendingReplacement
    {
        std::string          path;
        std::vector<uint8_t> bytes;
        uint32_t             entryId = 0;
    };

    // The library as the app holds it.
    struct UserLibrary
    {
        std::optional<DefinitionLibrary> library;
        std::string                      error;          // why it could not be opened
        std::vector<std::string>         messages;       // what the last action did
        std::vector<PendingReplacement>  replacements;   // asked about one at a time
    };

    // The library in `folder`, or none, with `folderError` -- why there is no data folder -- or
    // why it could not be opened.
    [[nodiscard]] UserLibrary OpenUserLibrary(const std::optional<std::filesystem::path>& folder,
                                              const std::string&                          folderError);

    // A UTF-8 path's file name: "C:/x/Phoenix.json" -> "Phoenix.json".
    [[nodiscard]] std::string FileName(const std::string& path);

    // What the picker says of a file just imported for another keyboard than the one it asks
    // about: Nazg matches by VID:PID only, and another keyboard's definition, however alike, can
    // put keys on the wrong switches -- a user is never to use one (Rico, 2026-10-12).
    [[nodiscard]] std::string ForeignImportNote(const std::string& path, const LibraryEntry& entry,
                                                const DeviceIdentity& board);

    // Copy files into the library, `now` the time they are recorded as added. Each outcome is a
    // message. With `askOnSameBoard`, a file for a board the library already has is set aside in
    // `replacements` for the user's answer rather than imported. Returns what was imported, each
    // with the path it came from. The library must be open.
    std::vector<std::pair<std::string, LibraryEntry>> ImportDefinitions(UserLibrary&                    library,
                                                                        const std::vector<std::string>& paths,
                                                                        bool                            askOnSameBoard,
                                                                        const std::string&              now);

    // Keep both: the set-aside file imported beside the definition it resembles. The entry, or
    // null when it was refused -- either way said in the messages.
    const LibraryEntry* ImportKeepingBoth(UserLibrary& library, const PendingReplacement& pending, const std::string& now);

    // A new version of an entry, in its place. A changed set of layout options is worth a
    // warning: the board keeps its layout choice as bits whose meaning the definition gives, so
    // groups added or reordered make the saved value pick different keys. True when the entry
    // has a new version -- not when the file was unchanged, or refused.
    bool ReplaceDefinition(UserLibrary& library, uint32_t id, const std::vector<uint8_t>& bytes, const std::string& path);

    // Settings' actions on one entry. True when the entry changed -- for a board drawn with it to
    // load again.
    bool ReimportDefinition(UserLibrary& library, uint32_t id);   // from the file it came from
    bool RestorePreviousDefinition(UserLibrary& library, uint32_t id);
    bool RemoveDefinition(UserLibrary& library, uint32_t id);

    // A file name from a board's name: "Phoenix Project No 1" -> "Phoenix_Project_No_1.json".
    // Only what every file system accepts; a name with nothing left gives "definition.json".
    [[nodiscard]] std::string ExportFileName(const std::string& name);

    // A definition written out byte for byte, as its source has it -- so it imports again
    // anywhere, into Nazg or VIA. The outcome, for the window that asked.
    [[nodiscard]] std::string WriteExport(const std::vector<uint8_t>& bytes, const std::string& name,
                                          const std::string& path);
}
