// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialKeycodeNames.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace nazg::vial
{
    namespace
    {
        using detail::NameKind;
        using detail::NameRow;

        // The layer keys vial-gui numbers from a base, 32 layers each: MO(1) is QK_MOMENTARY + 1.
        struct LayerRange
        {
            const char* name;
            const char* base;
        };

        constexpr LayerRange c_LayerRanges[] = {
            { "MO", "QK_MOMENTARY" },        { "DF", "QK_DEF_LAYER" },      { "TG", "QK_TOGGLE_LAYER" },
            { "TT", "QK_LAYER_TAP_TOGGLE" }, { "OSL", "QK_ONE_SHOT_LAYER" }, { "TO", "QK_TO" },
            { "PDF", "QK_PERSISTENT_DEF_LAYER" },
        };
        constexpr int c_Layers = 32;

        // vial-gui's v5 TO(n) carries QMK's old ON_PRESS bit, 0x10, as the firmware of the time did.
        constexpr uint16_t c_V5ToOnPress = 0x10;

        // A row's value in one table; none where it is absent -- vial-gui marks a keycode its v5
        // table lacks with a value past 16 bits (QK_PERSISTENT_DEF_LAYER).
        std::optional<uint16_t> ValueOf(const NameRow& row, NameTable table) noexcept
        {
            const int32_t value = table == NameTable::V6 ? row.v6 : row.v5;
            if (value < 0 || value > 0xFFFF)
                return std::nullopt;
            return static_cast<uint16_t>(value);
        }

        const NameRow* FindName(std::string_view name) noexcept
        {
            size_t         count = 0;
            const NameRow* rows  = detail::NameRows(count);
            const NameRow* end   = rows + count;
            const NameRow* found = std::lower_bound(rows, end, name,
                                                    [](const NameRow& row, std::string_view key) { return row.name < key; });
            return found != end && found->name == name ? found : nullptr;
        }

        std::optional<uint16_t> BaseOf(const char* name, NameTable table) noexcept
        {
            const NameRow* row = FindName(name);
            return row != nullptr ? ValueOf(*row, table) : std::nullopt;
        }

        // The first row of `kind` holding `value` in `table`.
        const NameRow* FindValue(uint16_t value, NameKind kind, NameTable table) noexcept
        {
            size_t         count = 0;
            const NameRow* rows  = detail::NameRows(count);
            for (size_t index = 0; index < count; ++index)
                if (rows[index].kind == kind && ValueOf(rows[index], table) == value)
                    return &rows[index];
            return nullptr;
        }

        std::optional<unsigned> ReadNumber(std::string_view text, int base) noexcept
        {
            unsigned number = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number, base);
            if (error != std::errc{} || end != text.data() + text.size() || text.empty())
                return std::nullopt;
            return number;
        }

        std::optional<uint16_t> LayerKeyValue(std::string_view name, unsigned layer, NameTable table) noexcept
        {
            if (layer >= c_Layers)
                return std::nullopt;
            for (const LayerRange& range : c_LayerRanges)
            {
                if (name != range.name)
                    continue;
                const std::optional<uint16_t> base = BaseOf(range.base, table);
                if (!base)
                    return std::nullopt;
                uint16_t value = static_cast<uint16_t>(*base | layer);
                if (table == NameTable::V5 && name == "TO")
                    value |= c_V5ToOnPress;
                return value;
            }
            return std::nullopt;
        }

        std::string Hex(uint16_t value)
        {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%x", value);
            return text;
        }
    }

    NameTable NameTableFor(int32_t vialProtocol) noexcept
    {
        return vialProtocol >= 6 ? NameTable::V6 : NameTable::V5;
    }

    std::string SpellKeycode(uint16_t value, NameTable table)
    {
        // vial-gui's order: a value in a masked range is outer(inner) or hex, never a name of its own.
        if (const NameRow* outer = FindValue(static_cast<uint16_t>(value & 0xFF00), NameKind::Masked, table))
        {
            const NameRow* inner = FindValue(static_cast<uint16_t>(value & 0x00FF), NameKind::Key, table);
            if (inner == nullptr)
                return Hex(value);
            std::string name(outer->name);
            name.replace(name.find("kc"), 2, inner->name);
            return name;
        }

        if (const NameRow* key = FindValue(value, NameKind::Key, table))
        {
            // vial-gui makes USER00 to USER15 unless the board names its own: past them, a number.
            const std::string_view name = key->name;
            if (name.substr(0, 4) != "USER" || name < "USER16")
                return key->name;
        }

        if (const std::optional<uint16_t> macro = BaseOf("QK_MACRO", table); macro && value >= *macro && value - *macro < 256)
            return "M" + std::to_string(value - *macro);
        if (const std::optional<uint16_t> dance = BaseOf("QK_TAP_DANCE", table); dance && value >= *dance && value - *dance < 256)
            return "TD(" + std::to_string(value - *dance) + ")";
        for (const LayerRange& range : c_LayerRanges)
            for (unsigned layer = 0; layer < c_Layers; ++layer)
                if (LayerKeyValue(range.name, layer, table) == value)
                    return std::string(range.name) + "(" + std::to_string(layer) + ")";

        return Hex(value);
    }

    std::optional<uint16_t> ReadKeycodeName(std::string_view text, NameTable table)
    {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ')
            text.remove_suffix(1);

        std::optional<unsigned> number;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            number = ReadNumber(text.substr(2), 16);
        else if (!text.empty() && text[0] >= '0' && text[0] <= '9')
            number = ReadNumber(text, 10);
        if (number)
            return *number <= 0xFFFF ? std::optional<uint16_t>(static_cast<uint16_t>(*number)) : std::nullopt;

        if (const NameRow* row = FindName(text); row != nullptr && row->kind == NameKind::Key)
            return ValueOf(*row, table);

        if (text.size() > 1 && text[0] == 'M')
            if (const std::optional<unsigned> index = ReadNumber(text.substr(1), 10); index && *index < 256)
                if (const std::optional<uint16_t> base = BaseOf("QK_MACRO", table))
                    return static_cast<uint16_t>(*base + *index);

        // NAME(argument): TD(2), MO(1), or a masked key -- LSFT(KC_5), LT1(KC_SPACE).
        const size_t open = text.find('(');
        if (open == std::string_view::npos || text.back() != ')')
            return std::nullopt;
        const std::string_view head     = text.substr(0, open);
        const std::string_view argument = text.substr(open + 1, text.size() - open - 2);

        if (head == "TD")
        {
            const std::optional<unsigned>  index = ReadNumber(argument, 10);
            const std::optional<uint16_t> base  = BaseOf("QK_TAP_DANCE", table);
            if (index && base && *index < 256)
                return static_cast<uint16_t>(*base + *index);
            return std::nullopt;
        }
        if (const std::optional<unsigned> layer = ReadNumber(argument, 10))
            return LayerKeyValue(head, *layer, table);

        const NameRow* outer = FindName(std::string(head) + "(kc)");
        if (outer == nullptr || outer->kind != NameKind::Masked)
            return std::nullopt;
        const std::optional<uint16_t> base  = ValueOf(*outer, table);
        const std::optional<uint16_t> inner = ReadKeycodeName(argument, table);
        if (!base || !inner || *inner > 0xFF)
            return std::nullopt;
        return static_cast<uint16_t>(*base | *inner);
    }
}
