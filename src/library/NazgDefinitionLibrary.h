// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// User definitions: the keyboard definitions the user imported, as opposed to the official
// ones from VIA's bundle -- kept in a folder Nazg owns, not as paths to files that move.
//
//     <folder>/user_definitions/index.json    the index -- the only file ever rewritten
//     <folder>/user_definitions/7-r1.json     entry 7, revision 1, byte for byte as imported
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
// This is plain file I/O on a folder the caller chooses -- SDL_GetPrefPath() in Main.cpp,
// an IDBFS mount in a web build, a temporary folder in the tests. The design, and what is
// still to come (per-device choices, linked entries, replacing, export): see
// docs/research_material/via-registry.md, "Storage".

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

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
        uint32_t id       = 0;
        uint32_t revision = 1;

        std::string origin;   // where it was imported from: a path today, a URL later
        std::string added;    // when, as the caller gave it (ISO 8601, UTC)

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

        // Forgets an entry and deletes its file. An unknown id does nothing.
        void Remove(uint32_t id);

        // The stored bytes of an entry. Throws LibraryError if its file cannot be read.
        [[nodiscard]] std::vector<uint8_t> Read(const LibraryEntry& entry) const;

        // The first entry for this VID:PID, in import order; null when there is none.
        [[nodiscard]] const LibraryEntry* Find(uint16_t vendorId, uint16_t productId) const noexcept;

    private:
        std::filesystem::path FileOf(const LibraryEntry& entry) const;
        void                  LoadIndex();
        void                  SaveIndex() const;
        void                  DeleteOrphans() const;

        std::filesystem::path     m_Folder;
        std::vector<LibraryEntry> m_Entries;
        uint32_t                  m_NextId = 1;
    };
}
