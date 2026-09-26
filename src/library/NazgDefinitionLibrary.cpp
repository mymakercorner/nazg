// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDefinitionLibrary.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

namespace nazg
{
    namespace
    {
        // The index's layout. A later Nazg that changes it bumps this and migrates; this
        // one refuses an index it does not know rather than guess at it.
        constexpr int c_Format = 1;

        // Everything lives in one folder -- the index beside the definitions -- so the
        // library is backed up, moved or zipped as a whole.
        constexpr char c_Definitions[] = "user_definitions";
        constexpr char c_Index[]       = "index.json";
        constexpr char c_IndexUpdate[] = "index.json.tmp";

        // In C++20 path::u8string() is a std::u8string; messages are plain std::string.
        std::string Utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        std::string HexId(uint16_t id)
        {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%04X", id);
            return text;
        }

        uint16_t ParseHexId(const nlohmann::json& value)
        {
            const std::string text = value.get<std::string>();
            size_t            used = 0;
            const unsigned long id = std::stoul(text, &used, 16);
            if (used != text.size() || id > 0xFFFF)
                throw std::invalid_argument(text);
            return static_cast<uint16_t>(id);
        }

        std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw LibraryError("cannot read " + Utf8(path));
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }

        void WriteFile(const std::filesystem::path& path, const void* data, size_t size)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            stream.close();
            if (!stream)
                throw LibraryError("cannot write " + Utf8(path));
        }
    }

    DefinitionLibrary::DefinitionLibrary(std::filesystem::path folder) : m_Folder(std::move(folder))
    {
        std::error_code failure;
        std::filesystem::create_directories(m_Folder / c_Definitions, failure);
        if (failure)
            throw LibraryError("cannot create " + Utf8(m_Folder / c_Definitions) + ": " + failure.message());

        if (std::filesystem::exists(m_Folder / c_Definitions / c_Index))
            LoadIndex();

        DeleteOrphans();
    }

    std::filesystem::path DefinitionLibrary::FileOf(uint32_t id, uint32_t revision) const
    {
        return m_Folder / c_Definitions / (std::to_string(id) + "-r" + std::to_string(revision) + ".json");
    }

    void DefinitionLibrary::LoadIndex()
    {
        const std::vector<uint8_t> bytes = ReadFile(m_Folder / c_Definitions / c_Index);

        try
        {
            const nlohmann::json index = nlohmann::json::parse(bytes.begin(), bytes.end());

            const int format = index.at("format").get<int>();
            if (format != c_Format)
                throw LibraryError("user_definitions/index.json is format " + std::to_string(format) + ", which this Nazg does not know");

            m_NextId = index.at("nextId").get<uint32_t>();

            for (const nlohmann::json& item : index.at("definitions"))
            {
                LibraryEntry entry;
                entry.id        = item.at("id").get<uint32_t>();
                entry.revision  = item.at("revision").get<uint32_t>();
                entry.previousRevision = item.value("previousRevision", 0u);   // absent: no backup
                entry.origin    = item.at("origin").get<std::string>();
                entry.added     = item.at("added").get<std::string>();
                entry.name      = item.at("name").get<std::string>();
                entry.vendorId  = ParseHexId(item.at("vendorId"));
                entry.productId = ParseHexId(item.at("productId"));

                // A number is never reused, so nextId must be past every entry -- kept
                // true even if the index was edited by hand.
                m_NextId = std::max(m_NextId, entry.id + 1);
                m_Entries.push_back(std::move(entry));
            }

            // Absent in an index written before choices existed: none.
            if (const auto choices = index.find("choices"); choices != index.end())
            {
                for (const nlohmann::json& item : *choices)
                {
                    DefinitionChoice choice;
                    choice.device.vendorId     = ParseHexId(item.at("vendorId"));
                    choice.device.productId    = ParseHexId(item.at("productId"));
                    choice.device.manufacturer = item.at("manufacturer").get<std::string>();
                    choice.device.product      = item.at("product").get<std::string>();
                    choice.device.release      = ParseHexId(item.at("release"));
                    choice.device.serial       = item.at("serial").get<std::string>();

                    const std::string definition = item.at("definition").get<std::string>();
                    const auto        ref        = ParseDefinitionRef(definition);
                    if (!ref)
                        throw std::invalid_argument("unknown definition " + definition);
                    choice.definition = *ref;

                    m_Choices.push_back(std::move(choice));
                }
            }
        }
        catch (const LibraryError&)
        {
            throw;
        }
        catch (const std::exception& failure)
        {
            m_Entries.clear();
            m_Choices.clear();
            throw LibraryError("user_definitions/index.json is damaged: " + std::string(failure.what()));
        }
    }

    void DefinitionLibrary::SaveIndex() const
    {
        nlohmann::json definitions = nlohmann::json::array();
        for (const LibraryEntry& entry : m_Entries)
        {
            nlohmann::json item = { { "id", entry.id },
                                    { "revision", entry.revision },
                                    { "origin", entry.origin },
                                    { "added", entry.added },
                                    { "name", entry.name },
                                    { "vendorId", HexId(entry.vendorId) },
                                    { "productId", HexId(entry.productId) } };
            if (entry.previousRevision != 0)
                item["previousRevision"] = entry.previousRevision;
            definitions.push_back(std::move(item));
        }

        nlohmann::json choices = nlohmann::json::array();
        for (const DefinitionChoice& choice : m_Choices)
        {
            choices.push_back({ { "vendorId", HexId(choice.device.vendorId) },
                                { "productId", HexId(choice.device.productId) },
                                { "manufacturer", choice.device.manufacturer },
                                { "product", choice.device.product },
                                { "release", HexId(choice.device.release) },
                                { "serial", choice.device.serial },
                                { "definition", FormatDefinitionRef(choice.definition) } });
        }

        const nlohmann::json index = { { "format", c_Format },
                                       { "nextId", m_NextId },
                                       { "definitions", definitions },
                                       { "choices", choices } };

        std::string text;
        try
        {
            text = index.dump(2) + "\n";
        }
        catch (const nlohmann::json::exception& failure)
        {
            throw LibraryError("cannot write user_definitions/index.json: " + std::string(failure.what()));
        }

        // Whole, then renamed over the old one: a crash leaves one index or the other.
        const std::filesystem::path update = m_Folder / c_Definitions / c_IndexUpdate;
        WriteFile(update, text.data(), text.size());

        std::error_code failure;
        std::filesystem::rename(update, m_Folder / c_Definitions / c_Index, failure);
        if (failure)
            throw LibraryError("cannot replace user_definitions/index.json: " + failure.message());
    }

    void DefinitionLibrary::DeleteOrphans() const
    {
        std::error_code ignored;
        std::filesystem::remove(m_Folder / c_Definitions / c_IndexUpdate, ignored);

        std::vector<std::filesystem::path> orphans;
        for (const auto& file : std::filesystem::directory_iterator(m_Folder / c_Definitions, ignored))
        {
            const std::filesystem::path name  = file.path().filename();
            const bool                  known = name == c_Index ||
                                std::any_of(m_Entries.begin(), m_Entries.end(), [&](const LibraryEntry& entry)
                                            {
                                                return FileOf(entry).filename() == name ||
                                                       (entry.previousRevision != 0 &&
                                                        FileOf(entry.id, entry.previousRevision).filename() == name);
                                            });
            if (!known && file.is_regular_file(ignored))
                orphans.push_back(file.path());
        }

        for (const std::filesystem::path& orphan : orphans)
            std::filesystem::remove(orphan, ignored);
    }

    const LibraryEntry& DefinitionLibrary::Import(const std::vector<uint8_t>& definition, std::string origin,
                                                  std::string added)
    {
        const KeyboardDefinition parsed = ParseDefinition(definition);
        if (parsed.vendorId == 0 && parsed.productId == 0)
            throw ProtocolError("the definition has no vendorId and productId to match a board by");

        LibraryEntry entry;
        entry.id        = m_NextId;
        entry.origin    = std::move(origin);
        entry.added     = std::move(added);
        entry.name      = parsed.name;
        entry.vendorId  = parsed.vendorId;
        entry.productId = parsed.productId;

        // The file first: until the index names it, it is an orphan, deleted at the next
        // open if the index write never happens.
        const std::filesystem::path file = FileOf(entry);
        WriteFile(file, definition.data(), definition.size());

        m_Entries.push_back(entry);
        ++m_NextId;

        try
        {
            SaveIndex();
        }
        catch (const LibraryError&)
        {
            m_Entries.pop_back();
            --m_NextId;
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
            throw;
        }

        return m_Entries.back();
    }

    void DefinitionLibrary::Remove(uint32_t id)
    {
        const auto found =
            std::find_if(m_Entries.begin(), m_Entries.end(), [id](const LibraryEntry& entry) { return entry.id == id; });
        if (found == m_Entries.end())
            return;

        const LibraryEntry                  removed  = *found;
        const std::ptrdiff_t                position = found - m_Entries.begin();
        const std::vector<DefinitionChoice> choices  = m_Choices;

        m_Entries.erase(found);
        m_Choices.erase(std::remove_if(m_Choices.begin(), m_Choices.end(), [id](const DefinitionChoice& choice)
                                       { return choice.definition == DefinitionRef::User(id); }),
                        m_Choices.end());

        try
        {
            SaveIndex();
        }
        catch (const LibraryError&)
        {
            m_Entries.insert(m_Entries.begin() + position, removed);
            m_Choices = choices;
            throw;
        }

        // After the index: a file left behind is an orphan, deleted at the next open.
        std::error_code ignored;
        std::filesystem::remove(FileOf(removed), ignored);
        if (removed.previousRevision != 0)
            std::filesystem::remove(FileOf(removed.id, removed.previousRevision), ignored);
    }

    LibraryEntry* DefinitionLibrary::FindMutable(uint32_t id) noexcept
    {
        for (LibraryEntry& entry : m_Entries)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    const LibraryEntry* DefinitionLibrary::Find(uint32_t id) const noexcept
    {
        return const_cast<DefinitionLibrary*>(this)->FindMutable(id);
    }

    const LibraryEntry* DefinitionLibrary::FindSameBoard(const KeyboardDefinition& definition) const noexcept
    {
        for (const LibraryEntry& entry : m_Entries)
            if (entry.vendorId == definition.vendorId && entry.productId == definition.productId &&
                entry.name == definition.name)
                return &entry;
        return nullptr;
    }

    void DefinitionLibrary::SaveEntry(const LibraryEntry& updated)
    {
        LibraryEntry&      entry    = *FindMutable(updated.id);
        const LibraryEntry previous = entry;

        entry = updated;
        try
        {
            SaveIndex();
        }
        catch (const LibraryError&)
        {
            entry = previous;
            throw;
        }
    }

    const LibraryEntry& DefinitionLibrary::Replace(uint32_t id, const std::vector<uint8_t>& definition,
                                                   std::string origin)
    {
        const LibraryEntry* current = Find(id);
        if (current == nullptr)
            throw LibraryError("no user definition " + std::to_string(id));

        const KeyboardDefinition parsed = ParseDefinition(definition);
        if (parsed.vendorId == 0 && parsed.productId == 0)
            throw ProtocolError("the definition has no vendorId and productId to match a board by");

        // The same bytes again -- a re-import of a file not edited since -- make no revision:
        // one would push the real backup out for nothing. Only where it came from is kept.
        std::vector<uint8_t> stored;
        try
        {
            stored = Read(*current);
        }
        catch (const LibraryError&)
        {
            // Unreadable: the new version is welcome.
        }

        if (stored == definition)
        {
            if (current->origin != origin)
            {
                LibraryEntry moved = *current;
                moved.origin       = std::move(origin);
                SaveEntry(moved);
            }
            return *Find(id);
        }

        // Past both revisions: after a restore the backup is the higher one.
        LibraryEntry updated     = *current;
        updated.revision         = std::max(current->revision, current->previousRevision) + 1;
        updated.previousRevision = current->revision;
        updated.origin           = std::move(origin);
        updated.name             = parsed.name;
        updated.vendorId         = parsed.vendorId;
        updated.productId        = parsed.productId;

        const uint32_t dropped = current->previousRevision;

        // As in Import(): the file, then the index, then the file no longer named. A crash
        // in between leaves an orphan, deleted at the next open.
        const std::filesystem::path file = FileOf(updated);
        WriteFile(file, definition.data(), definition.size());

        try
        {
            SaveEntry(updated);
        }
        catch (const LibraryError&)
        {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
            throw;
        }

        if (dropped != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(FileOf(id, dropped), ignored);
        }

        return *Find(id);
    }

    const LibraryEntry& DefinitionLibrary::RestorePrevious(uint32_t id)
    {
        const LibraryEntry* current = Find(id);
        if (current == nullptr)
            throw LibraryError("no user definition " + std::to_string(id));
        if (current->previousRevision == 0)
            throw LibraryError("user definition " + std::to_string(id) + " has no previous version");

        // Its name and ids may differ from the current revision's; the index copies them.
        const KeyboardDefinition parsed = ParseDefinition(ReadFile(FileOf(id, current->previousRevision)));

        LibraryEntry updated     = *current;
        updated.revision         = current->previousRevision;
        updated.previousRevision = current->revision;
        updated.name             = parsed.name;
        updated.vendorId         = parsed.vendorId;
        updated.productId        = parsed.productId;

        SaveEntry(updated);
        return *Find(id);
    }

    std::vector<uint8_t> DefinitionLibrary::Read(const LibraryEntry& entry) const
    {
        return ReadFile(FileOf(entry));
    }

    std::vector<DefinitionCandidate> DefinitionLibrary::Candidates(uint16_t vendorId, uint16_t productId) const
    {
        std::vector<DefinitionCandidate> candidates;
        for (const LibraryEntry& entry : m_Entries)
            if (entry.vendorId == vendorId && entry.productId == productId)
                candidates.push_back({ DefinitionRef::User(entry.id), ParseDefinition(Read(entry)), entry.origin });
        return candidates;
    }

    void DefinitionLibrary::SaveChoices(std::vector<DefinitionChoice> choices)
    {
        std::swap(m_Choices, choices);
        try
        {
            SaveIndex();
        }
        catch (const LibraryError&)
        {
            std::swap(m_Choices, choices);
            throw;
        }
    }

    void DefinitionLibrary::Choose(const DeviceIdentity& device, DefinitionRef definition)
    {
        // The device's earlier choice goes, and the new one is appended: the list stays
        // in the order choices were made, which FindChoice()'s second step relies on.
        std::vector<DefinitionChoice> choices = m_Choices;
        choices.erase(std::remove_if(choices.begin(), choices.end(),
                                     [&](const DefinitionChoice& choice) { return choice.device == device; }),
                      choices.end());
        choices.push_back({ device, std::move(definition) });

        SaveChoices(std::move(choices));
    }

    void DefinitionLibrary::Forget(const DeviceIdentity& device)
    {
        std::vector<DefinitionChoice> choices = m_Choices;
        choices.erase(std::remove_if(choices.begin(), choices.end(),
                                     [&](const DefinitionChoice& choice) { return IsSameBoard(choice.device, device); }),
                      choices.end());

        if (choices.size() != m_Choices.size())
            SaveChoices(std::move(choices));
    }
}
