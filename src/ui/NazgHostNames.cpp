// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "ui/NazgHostNames.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "adapters/qmk/NazgQmkExpression.h"

namespace nazg
{
    namespace
    {
        // A name's expression read as a keycode. The names hold basic keycodes and modifiers only,
        // the same in every keycode version, so the latest reads them all.
        std::optional<Keycode> KeycodeOf(const HostName& entry, qmk::KeycodeVersion version)
        {
            return qmk::ParseExpression(entry.expression, version);
        }

        const HostName* FindName(std::string_view layoutId, std::string_view name)
        {
            for (const HostName& entry : HostNamesOf(layoutId))
                if (entry.name == name)
                    return &entry;
            return nullptr;
        }

        // "a", "a and b", "a, b and c".
        std::string Listed(const std::vector<std::string_view>& items)
        {
            std::string text;
            for (size_t index = 0; index < items.size(); ++index)
            {
                if (index > 0)
                    text += index + 1 == items.size() ? " and " : ", ";
                text += items[index];
            }
            return text;
        }
    }

    std::string_view HostNameOf(const Keycode& keycode, const HostLayout& layout)
    {
        // Built once per layout: each name's keycode as FormatKeycode() prints it, the first name
        // QMK gives a keycode winning.
        static std::map<std::string_view, std::map<std::string, std::string_view>> s_Names;
        auto [found, isNew] = s_Names.try_emplace(layout.id);
        if (isNew)
            for (const HostName& entry : HostNamesOf(layout.id))
                if (const std::optional<Keycode> parsed = KeycodeOf(entry, qmk::c_LatestKeycodeVersion))
                    found->second.try_emplace(FormatKeycode(*parsed), entry.name);

        const auto name = found->second.find(FormatKeycode(keycode));
        return name != found->second.end() ? name->second : std::string_view{};
    }

    HostNameReading ReadHostName(std::string_view typed, const HostLayout& chosen, qmk::KeycodeVersion version)
    {
        std::string name;
        for (char c : typed)
            if (!std::isspace(static_cast<unsigned char>(c)))
                name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        HostNameReading reading;
        if (const HostName* own = FindName(chosen.id, name))
        {
            reading.known   = true;
            reading.keycode = KeycodeOf(*own, version);
            return reading;
        }

        // Another layout's name: the layouts having it, grouped by the keycode each gives it.
        std::vector<std::pair<std::string, std::vector<std::string_view>>> meanings;
        std::optional<Keycode>                                             first;
        for (const HostLayout& layout : HostLayouts())
        {
            const HostName* entry = FindName(layout.id, name);
            if (entry == nullptr)
                continue;
            const std::optional<Keycode> parsed = KeycodeOf(*entry, version);
            if (!parsed)
                continue;
            const std::string written = FormatKeycode(*parsed);
            auto meaning = std::find_if(meanings.begin(), meanings.end(), [&](const auto& m) { return m.first == written; });
            if (meaning == meanings.end())
            {
                meanings.push_back({ written, {} });
                meaning = std::prev(meanings.end());
            }
            meaning->second.push_back(layout.name);
            if (!first)
                first = parsed;
        }
        if (meanings.empty())
            return reading;

        reading.known = true;
        if (meanings.size() == 1)
        {
            reading.keycode = first;
            reading.note    = name + " is " + meanings.front().first + " on " + Listed(meanings.front().second) +
                           " -- the host layout is " + std::string(chosen.name);
            return reading;
        }

        reading.note = name + " differs between layouts:";
        for (size_t index = 0; index < meanings.size(); ++index)
            reading.note += (index > 0 ? "; " : " ") + meanings[index].first + " on " + Listed(meanings[index].second);
        reading.note += " -- choose one as the host layout";
        return reading;
    }
}
