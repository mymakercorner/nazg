// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgCommunityBundle.h"

#include <charconv>
#include <string_view>

#include <nlohmann/json.hpp>

#include "adapters/NazgXz.h"
#include "adapters/via/NazgViaBundle.h"     // ForEachTarFile
#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

namespace nazg
{
    namespace
    {
        // Far above the 9 MB the first bundle inflates to; a bound on a corrupt or hostile one.
        constexpr uint32_t c_MaxBundleSize = 128u * 1024u * 1024u;

        // "0x320F", as the index writes ids; nullopt for anything else.
        std::optional<uint16_t> ReadId(const nlohmann::json& value)
        {
            if (!value.is_string())
                return std::nullopt;
            std::string_view text = value.get_ref<const std::string&>();
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                text.remove_prefix(2);
            uint16_t id             = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id, 16);
            if (text.empty() || error != std::errc() || end != text.data() + text.size())
                return std::nullopt;
            return id;
        }
    }

    CommunityDefinitionBundle::CommunityDefinitionBundle(const std::vector<uint8_t>& bundle)
        : m_Tar(DecompressXz(bundle, c_MaxBundleSize, "the community bundle"))
    {
        ForEachTarFile(m_Tar, [this](const std::string& name, const uint8_t* data, size_t size)
        {
            m_Files[name] = { static_cast<size_t>(data - m_Tar.data()), size };
            return true;
        });

        const auto parse = [this](const char* name)
        {
            const auto found = m_Files.find(name);
            if (found == m_Files.end())
                return nlohmann::json();
            const uint8_t* text = m_Tar.data() + found->second.offset;
            return nlohmann::json::parse(text, text + found->second.size, nullptr, false);
        };

        const nlohmann::json index = parse("index.json");
        if (!index.is_array())
            throw ProtocolError("the community bundle has no index");

        for (const nlohmann::json& entry : index)
        {
            if (!entry.is_object())
                continue;
            CommunityDefinition definition;
            definition.path            = entry.value("path", std::string());
            const auto vendorId        = ReadId(entry.value("vendorId", nlohmann::json()));
            const auto productId       = ReadId(entry.value("productId", nlohmann::json()));
            if (!vendorId || !productId || m_Files.find(definition.path) == m_Files.end())
                continue;
            definition.vendorId  = *vendorId;
            definition.productId = *productId;
            definition.name      = entry.value("name", std::string());
            definition.publisher = entry.value("publisher", std::string());
            definition.licence   = entry.value("licence", std::string());
            definition.status    = entry.value("status", std::string());
            m_Definitions.push_back(std::move(definition));
        }

        if (const nlohmann::json manifest = parse("manifest.json"); manifest.is_object())
        {
            m_Manifest.emplace();
            m_Manifest->release     = manifest.value("release", std::string());
            m_Manifest->definitions = manifest.value("definitions", 0);
        }
    }

    std::vector<const CommunityDefinition*> CommunityDefinitionBundle::Find(uint16_t vendorId, uint16_t productId) const
    {
        std::vector<const CommunityDefinition*> found;
        for (const CommunityDefinition& definition : m_Definitions)
            if (definition.vendorId == vendorId && definition.productId == productId)
                found.push_back(&definition);
        return found;
    }

    const CommunityDefinition* CommunityDefinitionBundle::Entry(const std::string& path) const
    {
        for (const CommunityDefinition& definition : m_Definitions)
            if (definition.path == path)
                return &definition;
        return nullptr;
    }

    std::optional<std::vector<uint8_t>> CommunityDefinitionBundle::Bytes(const std::string& path) const
    {
        const auto found = m_Files.find(path);
        if (found == m_Files.end())
            return std::nullopt;
        const uint8_t* data = m_Tar.data() + found->second.offset;
        return std::vector<uint8_t>(data, data + found->second.size);
    }
}
