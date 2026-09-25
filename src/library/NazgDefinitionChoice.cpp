// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDefinitionChoice.h"

#include <algorithm>
#include <charconv>

namespace nazg
{
    namespace
    {
        constexpr std::string_view c_UserPrefix     = "user:";
        constexpr std::string_view c_OfficialPrefix = "official:";

        // Lowercase ASCII letters and digits only: "KBDfans D60-B" -> "kbdfansd60b".
        std::string Simplified(std::string_view text)
        {
            std::string simple;
            for (const char c : text)
            {
                if (c >= 'A' && c <= 'Z')
                    simple.push_back(static_cast<char>(c - 'A' + 'a'));
                else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                    simple.push_back(c);
            }
            return simple;
        }

        bool NameMatchesProduct(const std::string& name, const std::string& product)
        {
            if (name.empty() || product.empty())
                return false;
            return name.find(product) != std::string::npos || product.find(name) != std::string::npos;
        }
    }

    std::string FormatDefinitionRef(const DefinitionRef& ref)
    {
        if (ref.kind == DefinitionRef::Kind::User)
            return std::string(c_UserPrefix) + std::to_string(ref.userId);
        return std::string(c_OfficialPrefix) + ref.officialPath;
    }

    std::optional<DefinitionRef> ParseDefinitionRef(std::string_view text)
    {
        if (text.substr(0, c_UserPrefix.size()) == c_UserPrefix)
        {
            const std::string_view number = text.substr(c_UserPrefix.size());
            uint32_t               id     = 0;
            const auto [end, error]       = std::from_chars(number.data(), number.data() + number.size(), id);
            if (number.empty() || error != std::errc() || end != number.data() + number.size())
                return std::nullopt;
            return DefinitionRef::User(id);
        }

        if (text.substr(0, c_OfficialPrefix.size()) == c_OfficialPrefix && text.size() > c_OfficialPrefix.size())
            return DefinitionRef::Official(std::string(text.substr(c_OfficialPrefix.size())));

        return std::nullopt;
    }

    bool IsSameBoard(const DeviceIdentity& a, const DeviceIdentity& b) noexcept
    {
        return a.vendorId == b.vendorId && a.productId == b.productId && a.manufacturer == b.manufacturer &&
               a.product == b.product;
    }

    const DefinitionChoice* FindChoice(const std::vector<DefinitionChoice>& choices, const DeviceIdentity& device) noexcept
    {
        for (const DefinitionChoice& choice : choices)
            if (choice.device == device)
                return &choice;

        // Newest first: of two revisions sharing their strings, the one chosen last.
        for (auto choice = choices.rbegin(); choice != choices.rend(); ++choice)
            if (IsSameBoard(choice->device, device))
                return &*choice;

        return nullptr;
    }

    void RankCandidates(std::vector<DefinitionCandidate>& candidates, std::string_view product)
    {
        const std::string simpleProduct = Simplified(product);

        const auto rank = [&](const DefinitionCandidate& candidate)
        {
            const int source = candidate.ref.kind == DefinitionRef::Kind::User ? 0 : 1;
            const int name   = NameMatchesProduct(Simplified(candidate.definition.name), simpleProduct) ? 0 : 1;
            return source * 2 + name;
        };

        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](const DefinitionCandidate& a, const DefinitionCandidate& b) { return rank(a) < rank(b); });
    }

    std::optional<size_t> ResolveCandidate(const std::vector<DefinitionCandidate>& candidates,
                                           const std::optional<DefinitionRef>&     chosen) noexcept
    {
        if (chosen)
            for (size_t index = 0; index < candidates.size(); ++index)
                if (candidates[index].ref == *chosen)
                    return index;

        if (candidates.size() == 1)
            return 0;

        return std::nullopt;
    }
}
