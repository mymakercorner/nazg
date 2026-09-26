// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// User definitions: the keyboard definitions the user imported, as opposed to the official
// ones from VIA's bundle -- kept in a folder Nazg owns, not as paths to files that move.
//
//     <folder>/user_definitions/index.json    the index -- the only file ever rewritten
//     <folder>/user_definitions/7-r2.json     entry 7, revision 2, byte for byte as imported
//     <folder>/user_definitions/7-r1.json     its one backup, the revision r2 replaced
//
// Replacing an entry -- a re-import of its file, or a newer file for the same board --
// keeps its number, so the choices pointing at it follow, and keeps the revision it
// replaced as a backup, one deep; restoring swaps the two. No full history: the file's
// author has that in git. Nazg never watches a file for changes; the user re-imports.
//
// <folder> is Nazg's data folder, which will hold other things; everything of the library
// is in user_definitions/, so it is backed up, moved or zipped as one folder.
//
// A definition is checked by parsing it before it is let in, then stored exactly as
// given, so an export gives back what came in and a better parser also reads old imports.
// A definition file is never modified once written. The index is written whole to a
// temporary file and renamed over the old one, so a crash leaves the old index or the
// new, never half of each; a file no entry points to is an orphan of such a crash and is
// deleted when the library opens.
//
// Entry numbers only grow -- `nextId` in the index -- so a number is never given to
// another definition, even after a removal.
//
// The index also keeps the choices -- which definition draws which device, see
// NazgDefinitionChoice.h -- beside the entries they point at, so the two never part: an
// entry removed takes its choices with it.
//
// This is plain file I/O on a folder the caller chooses -- SDL_GetPrefPath() in Main.cpp,
// an IDBFS mount in a web build, a temporary folder in the tests. The design, and what is
// still to come (export and import of the whole library): see
// docs/research_material/via-registry.md, "Storage".

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"
#include "library/NazgDefinitionChoice.h"

namespace nazg
{
    // The library's folder or index cannot be read or written. A definition that does not
    // parse is a ProtocolError instead, as everywhere else.
    class LibraryError : public std::runtime_error
    {
    public:
        explicit LibraryError(const std::string& message) : std::runtime_error(message) {}
    };

    struct LibraryEntry
    {
        uint32_t id               = 0;
        uint32_t revision         = 1;
        uint32_t previousRevision = 0;   // the backup; 0 when there is none

        std::string origin;   // where it was last imported from: a path today, a URL later
        std::string added;    // when the entry was first imported, as the caller gave it (ISO 8601, UTC)

        // Copied from the definition, to list and match without reading every file.
        std::string name;
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;
    };

    class DefinitionLibrary
    {
    public:
        // Opens the library in `folder`, creating what it needs there. Throws
        // LibraryError if the index exists but cannot be read -- it is then left
        // untouched rather than replaced, so nothing in it is lost.
        explicit DefinitionLibrary(std::filesystem::path folder);

        const std::filesystem::path&     Folder() const noexcept { return m_Folder; }
        const std::vector<LibraryEntry>& Entries() const noexcept { return m_Entries; }

        // Checks the definition parses, stores it and records it. Throws ProtocolError if
        // it does not parse or has no USB ids to be matched by, LibraryError if it cannot
        // be written; the library is unchanged either way.
        const LibraryEntry& Import(const std::vector<uint8_t>& definition, std::string origin, std::string added);

        // Replaces an entry's definition, keeping its number: its current revision becomes
        // the backup, and the backup before it is deleted. Name and ids are taken from the
        // new definition -- a board whose ids changed stops being a candidate for the old
        // ones. A definition byte for byte the same as the current one makes no revision, so
        // the backup stays; only `origin` is updated. Throws as Import() does, and
        // LibraryError for an unknown id; the library is unchanged then.
        const LibraryEntry& Replace(uint32_t id, const std::vector<uint8_t>& definition, std::string origin);

        // Swaps an entry's current revision and its backup, so a restore is undone the same
        // way. Throws LibraryError if the entry has no backup or it cannot be read,
        // ProtocolError if it no longer parses.
        const LibraryEntry& RestorePrevious(uint32_t id);

        // Forgets an entry, and the choices pointing at it, and deletes its files. An
        // unknown id does nothing.
        void Remove(uint32_t id);

        // The entry by number; null when there is none.
        [[nodiscard]] const LibraryEntry* Find(uint32_t id) const noexcept;

        // The entry a definition would replace: the first with its VID:PID and name. Null
        // when there is none, and the import is simply a new entry.
        [[nodiscard]] const LibraryEntry* FindSameBoard(const KeyboardDefinition& definition) const noexcept;

        // The stored bytes of an entry's current revision. Throws LibraryError if its file
        // cannot be read.
        [[nodiscard]] std::vector<uint8_t> Read(const LibraryEntry& entry) const;

        // Every entry for this VID:PID, read and parsed, in import order. Throws
        // LibraryError if a file cannot be read, ProtocolError if one no longer parses.
        [[nodiscard]] std::vector<DefinitionCandidate> Candidates(uint16_t vendorId, uint16_t productId) const;

        // Oldest first. See NazgDefinitionChoice.h.
        const std::vector<DefinitionChoice>& Choices() const noexcept { return m_Choices; }

        // The choice for a device, exact or by its VID:PID and strings; null when none.
        [[nodiscard]] const DefinitionChoice* FindChoice(const DeviceIdentity& device) const noexcept
        {
            return nazg::FindChoice(m_Choices, device);
        }

        // Remembers the definition for exactly this device, replacing its earlier choice
        // if it had one. Throws LibraryError if it cannot be saved; nothing changes then.
        void Choose(const DeviceIdentity& device, DefinitionRef definition);

        // Forgets every choice FindChoice() could return for this device -- its board's,
        // whatever their release number and serial -- so the next connect asks again.
        // Throws as Choose() does.
        void Forget(const DeviceIdentity& device);

    private:
        std::filesystem::path FileOf(uint32_t id, uint32_t revision) const;
        std::filesystem::path FileOf(const LibraryEntry& entry) const { return FileOf(entry.id, entry.revision); }
        LibraryEntry*         FindMutable(uint32_t id) noexcept;

        // Puts `updated` in place of the entry with its number and saves the index; on
        // failure puts the old one back and throws.
        void SaveEntry(const LibraryEntry& updated);
        void                  LoadIndex();
        void                  SaveIndex() const;
        void                  DeleteOrphans() const;

        // Replaces the choices and saves; on failure puts the old ones back and throws.
        void SaveChoices(std::vector<DefinitionChoice> choices);

        std::filesystem::path         m_Folder;
        std::vector<LibraryEntry>     m_Entries;
        std::vector<DefinitionChoice> m_Choices;
        uint32_t                      m_NextId = 1;
    };
}
