// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLibraryActions.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>

#include "adapters/via/NazgKeyboardDefinition.h"
#include "files/NazgFiles.h"

namespace nazg
{
    UserLibrary OpenUserLibrary(const std::optional<std::filesystem::path>& folder, const std::string& folderError)
    {
        UserLibrary result;
        if (!folder)
        {
            result.error = folderError;
            return result;
        }

        try
        {
            result.library.emplace(*folder);
        }
        catch (const std::exception& failure)
        {
            result.error = failure.what();
        }
        return result;
    }

    std::string FileName(const std::string& path)
    {
        return Utf8FromPath(PathFromUtf8(path).filename());
    }

    std::string ForeignImportNote(const std::string& path, const LibraryEntry& entry, const DeviceIdentity& board)
    {
        char ids[64];
        std::snprintf(ids, sizeof(ids), "(%04X:%04X), not this one (%04X:%04X)", entry.vendorId, entry.productId,
                      board.vendorId, board.productId);
        return FileName(path) + " is the definition of another keyboard " + ids +
               ", so Nazg does not offer it here. Use only a definition made for this keyboard: one made for "
               "another, however alike, can put keys on the wrong switches. The file stays in your definitions, "
               "for the keyboard it describes.";
    }

    std::vector<std::pair<std::string, LibraryEntry>> ImportDefinitions(UserLibrary&                    library,
                                                                        const std::vector<std::string>& paths,
                                                                        bool                            askOnSameBoard,
                                                                        const std::string&              now)
    {
        library.messages.clear();
        std::vector<std::pair<std::string, LibraryEntry>> imported;

        for (const std::string& path : paths)
        {
            try
            {
                std::optional<std::vector<uint8_t>> bytes = ReadWholeFile(PathFromUtf8(path));
                if (!bytes)
                    throw std::runtime_error("cannot open the file");

                if (askOnSameBoard)
                    if (const LibraryEntry* same = library.library->FindSameBoard(via::ParseDefinition(*bytes)))
                    {
                        library.replacements.push_back({ path, std::move(*bytes), same->id });
                        continue;
                    }

                const LibraryEntry& entry = library.library->Import(*bytes, path, now);
                library.messages.push_back("imported " + FileName(path) + ": " + entry.name);
                imported.emplace_back(path, entry);
            }
            catch (const std::exception& failure)
            {
                library.messages.push_back("not imported " + FileName(path) + ": " + failure.what());
            }
        }
        return imported;
    }

    const LibraryEntry* ImportKeepingBoth(UserLibrary& library, const PendingReplacement& pending, const std::string& now)
    {
        try
        {
            const LibraryEntry& added = library.library->Import(pending.bytes, pending.path, now);
            library.messages.push_back("imported " + FileName(pending.path) + ": " + added.name);
            return &added;
        }
        catch (const std::exception& failure)
        {
            library.messages.push_back("not imported " + FileName(pending.path) + ": " + failure.what());
            return nullptr;
        }
    }

    bool ReplaceDefinition(UserLibrary& library, uint32_t id, const std::vector<uint8_t>& bytes, const std::string& path)
    {
        try
        {
            std::optional<std::vector<std::string>> before;
            uint32_t                                revision = 0;
            if (const LibraryEntry* entry = library.library->Find(id))
            {
                revision = entry->revision;
                try
                {
                    before = via::ParseDefinition(library.library->Read(*entry)).layoutLabels;
                }
                catch (const std::exception&)
                {
                    // Unreadable or unparseable: nothing to compare with, which is no reason
                    // to refuse the new version.
                }
            }

            const LibraryEntry& replaced = library.library->Replace(id, bytes, path);
            if (replaced.revision == revision)
            {
                library.messages.push_back(FileName(path) + " is unchanged -- " + replaced.name + " stays as it was");
                return false;
            }

            library.messages.push_back("replaced " + replaced.name + " with " + FileName(path) + " -- the version " +
                                       "before is kept, see \"Restore previous\"");

            if (before && *before != via::ParseDefinition(bytes).layoutLabels)
                library.messages.push_back("its layout options changed: a board's saved layout may now select "
                                           "different keys -- check it");
            return true;
        }
        catch (const std::exception& failure)
        {
            library.messages.push_back("not replaced with " + FileName(path) + ": " + failure.what());
            return false;
        }
    }

    bool ReimportDefinition(UserLibrary& library, uint32_t id)
    {
        library.messages.clear();
        const LibraryEntry* entry = library.library->Find(id);
        if (entry == nullptr)
            return false;

        const std::string                         origin = entry->origin;
        const std::optional<std::vector<uint8_t>> bytes  = ReadWholeFile(PathFromUtf8(origin));
        if (!bytes)
        {
            library.messages.push_back("failed: cannot read " + origin +
                                       " -- if it moved, import it from where it is now and answer Replace");
            return false;
        }
        return ReplaceDefinition(library, id, *bytes, origin);
    }

    bool RestorePreviousDefinition(UserLibrary& library, uint32_t id)
    {
        library.messages.clear();
        try
        {
            const LibraryEntry& restored = library.library->RestorePrevious(id);
            library.messages.push_back("restored the previous version of " + restored.name);
            return true;
        }
        catch (const std::exception& failure)
        {
            library.messages.push_back(std::string("failed: ") + failure.what());
            return false;
        }
    }

    bool RemoveDefinition(UserLibrary& library, uint32_t id)
    {
        library.messages.clear();
        try
        {
            library.library->Remove(id);
            return true;
        }
        catch (const std::exception& failure)
        {
            library.messages.push_back(std::string("failed: ") + failure.what());
            return false;
        }
    }

    std::string ExportFileName(const std::string& name)
    {
        std::string file;
        for (const char c : name)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                file.push_back(c);
            else if (c == ' ' && !file.empty() && file.back() != '_')
                file.push_back('_');
        }
        while (!file.empty() && file.back() == '_')
            file.pop_back();
        return (file.empty() ? "definition" : file) + ".json";
    }

    std::string WriteExport(const std::vector<uint8_t>& bytes, const std::string& name, const std::string& path)
    {
        std::ofstream stream(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream)
            return "not exported to " + path + ": cannot write the file";
        return "exported " + name + " to " + path;
    }
}
