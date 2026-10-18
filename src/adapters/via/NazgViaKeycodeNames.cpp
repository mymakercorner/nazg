// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaKeycodeNames.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace nazg::via
{
    namespace
    {
        // VIA's modifier functions -- S(KC_1), LCTL(LSFT(KC_A)), HYPR(KC_A) -- by the bits they set,
        // the same in every table (advanced-keys.ts).
        struct NamedValue
        {
            const char* name;
            uint16_t    value;
        };

        constexpr NamedValue c_Modifiers[] = {
            { "A", 0x0400 },    { "ALGR", 0x1400 }, { "C", 0x0100 },    { "G", 0x0800 },    { "HYPR", 0x0F00 },
            { "LAG", 0x0C00 },  { "LALT", 0x0400 }, { "LCA", 0x0500 },  { "LCAG", 0x0D00 }, { "LCMD", 0x0800 },
            { "LCTL", 0x0100 }, { "LGUI", 0x0800 }, { "LSA", 0x0600 },  { "LSFT", 0x0200 }, { "LSG", 0x0A00 },
            { "LWIN", 0x0800 }, { "MEH", 0x0700 },  { "RAG", 0x1C00 },  { "RALT", 0x1400 }, { "RCMD", 0x1800 },
            { "RCS", 0x1300 },  { "RCTL", 0x1100 }, { "RGUI", 0x1800 }, { "RSA", 0x1600 },  { "RSFT", 0x1200 },
            { "RSG", 0x1A00 },  { "RWIN", 0x1800 }, { "S", 0x0200 },    { "SAGR", 0x1600 }, { "SCMD", 0x0A00 },
            { "SGUI", 0x0A00 }, { "SWIN", 0x0A00 },
        };

        // The masks MT, OSM and LM take, joined by |.
        constexpr NamedValue c_ModMasks[] = {
            { "MOD_HYPR", 0x0F }, { "MOD_LALT", 0x04 }, { "MOD_LCTL", 0x01 }, { "MOD_LGUI", 0x08 }, { "MOD_LSFT", 0x02 },
            { "MOD_MEH", 0x07 },  { "MOD_RALT", 0x14 }, { "MOD_RCTL", 0x11 }, { "MOD_RGUI", 0x18 }, { "MOD_RSFT", 0x12 },
        };

        // Names VIA has renamed, read as their new spelling first (deprecated-keycodes.ts); MACRO00,
        // USER00 and SPC_FN1 follow a pattern, in Renamed().
        constexpr const char* c_Shifted[][2] = {
            { "KC_AMPR", "S(KC_7)" },    { "KC_ASTR", "S(KC_8)" },    { "KC_AT", "S(KC_2)" },      { "KC_CIRC", "S(KC_6)" },
            { "KC_COLN", "S(KC_SCLN)" }, { "KC_DLR", "S(KC_4)" },     { "KC_DQUO", "S(KC_QUOT)" }, { "KC_EXLM", "S(KC_1)" },
            { "KC_GT", "S(KC_DOT)" },    { "KC_HASH", "S(KC_3)" },    { "KC_LCBR", "S(KC_LBRC)" }, { "KC_LPRN", "S(KC_9)" },
            { "KC_LT", "S(KC_COMM)" },   { "KC_PERC", "S(KC_5)" },    { "KC_PIPE", "S(KC_BSLS)" }, { "KC_PLUS", "S(KC_EQL)" },
            { "KC_QUES", "S(KC_SLSH)" }, { "KC_RCBR", "S(KC_RBRC)" }, { "KC_RPRN", "S(KC_0)" },    { "KC_TILD", "S(KC_GRV)" },
            { "KC_UNDS", "S(KC_MINS)" },
        };

        std::optional<uint16_t> Find(const NamedValue* begin, const NamedValue* end, std::string_view name)
        {
            for (const NamedValue* entry = begin; entry != end; ++entry)
                if (name == entry->name)
                    return entry->value;
            return std::nullopt;
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }

        std::string Upper(std::string_view text)
        {
            std::string upper(text);
            for (char& character : upper)
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            return upper;
        }

        // Digits only, up to `largest`.
        std::optional<uint32_t> Number(std::string_view text, uint32_t largest)
        {
            text = Trim(text);
            if (text.empty() || text.size() > 5)
                return std::nullopt;
            uint32_t value = 0;
            for (const char character : text)
            {
                if (character < '0' || character > '9')
                    return std::nullopt;
                value = value * 10 + static_cast<uint32_t>(character - '0');
            }
            return value <= largest ? std::optional<uint32_t>(value) : std::nullopt;
        }

        std::optional<uint16_t> Hex(std::string_view text)
        {
            if (text.size() < 3 || text.size() > 6 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
                return std::nullopt;
            uint32_t value = 0;
            for (const char character : text.substr(2))
            {
                const int digit = std::isdigit(static_cast<unsigned char>(character))
                                      ? character - '0'
                                      : std::isxdigit(static_cast<unsigned char>(character))
                                            ? std::tolower(static_cast<unsigned char>(character)) - 'a' + 10
                                            : -1;
                if (digit < 0)
                    return std::nullopt;
                value = value * 16 + static_cast<uint32_t>(digit);
            }
            return static_cast<uint16_t>(value);
        }

        // MOD_LCTL | MOD_LSFT; none for an empty or unknown mask, which VIA reads as 0.
        std::optional<uint16_t> Mods(std::string_view text)
        {
            uint16_t mods = 0;
            while (true)
            {
                const size_t                  bar  = text.find('|');
                const std::optional<uint16_t> mask = Find(std::begin(c_ModMasks), std::end(c_ModMasks),
                                                          Trim(text.substr(0, bar)));
                if (!mask)
                    return std::nullopt;
                mods |= *mask;
                if (bar == std::string_view::npos)
                    break;
                text.remove_prefix(bar + 1);
            }
            return mods != 0 ? std::optional<uint16_t>(mods) : std::nullopt;
        }

        // A basic keycode inside LT, MT or a modifier function: a name of the table, one byte.
        std::optional<uint16_t> Basic(std::string_view name, NameTable table)
        {
            const std::optional<uint16_t> value = ValueOfName(Trim(name), table);
            return value && *value <= 0xFF ? value : std::nullopt;
        }

        // The start and end of one of VIA's numbered ranges in `table`: "_QK_MOMENTARY".
        std::optional<std::pair<uint16_t, uint16_t>> Range(const std::string& base, NameTable table)
        {
            const std::optional<uint16_t> first = ValueOfName(base, table);
            const std::optional<uint16_t> last  = ValueOfName(base + "_MAX", table);
            if (!first || !last)
                return std::nullopt;
            return std::make_pair(*first, *last);
        }

        // MO(1), MACRO(3), CUSTOM(0): the range's start plus n, within it.
        std::optional<uint16_t> Numbered(std::string_view function, std::string_view argument, NameTable table)
        {
            static constexpr const char* c_Bases[][2] = {
                { "TO", "_QK_TO" },
                { "MO", "_QK_MOMENTARY" },
                { "DF", "_QK_DEF_LAYER" },
                { "TG", "_QK_TOGGLE_LAYER" },
                { "OSL", "_QK_ONE_SHOT_LAYER" },
                { "TT", "_QK_LAYER_TAP_TOGGLE" },
                { "CUSTOM", "_QK_KB" },
                { "MACRO", "_QK_MACRO" },
            };
            for (const auto& [name, base] : c_Bases)
            {
                if (function != name)
                    continue;
                const auto range = Range(base, table);
                const auto n     = Number(argument, 0xFFFF);
                if (!range || !n || range->first + *n > range->second)
                    return std::nullopt;
                return static_cast<uint16_t>(range->first + *n);
            }
            return std::nullopt;
        }

        // MACRO00, USER00, SPC_FN1, KC_TILD...: their spelling now.
        std::string Renamed(std::string_view text)
        {
            for (const auto& [old, now] : c_Shifted)
                if (text == old)
                    return now;
            const auto twoDigits = [](std::string_view digits)
            {
                return digits.size() == 2 && std::isdigit(static_cast<unsigned char>(digits[0])) &&
                       std::isdigit(static_cast<unsigned char>(digits[1]));
            };
            if (text.size() == 7 && text.substr(0, 5) == "MACRO" && twoDigits(text.substr(5)) && text.substr(5) <= "15")
                return "MACRO(" + std::to_string(std::stoi(std::string(text.substr(5)))) + ")";
            if (text.size() == 6 && text.substr(0, 4) == "USER" && twoDigits(text.substr(4)) && text.substr(4) <= "15")
                return "CUSTOM(" + std::to_string(std::stoi(std::string(text.substr(4)))) + ")";
            if (text == "SPC_FN1" || text == "SPC_FN2" || text == "SPC_FN3")
                return "LT(" + std::string(1, text.back()) + ",KC_SPC)";
            return std::string(text);
        }

        // The function forms, upper-cased as VIA reads them (advancedStringToKeycode()).
        std::optional<uint16_t> Function(const std::string& text, NameTable table)
        {
            const size_t open = text.find('(');
            if (open == std::string::npos || text.back() != ')')
                return std::nullopt;
            const std::string_view function = Trim(std::string_view(text).substr(0, open));
            const std::string_view argument = std::string_view(text).substr(open + 1, text.size() - open - 2);

            if (const std::optional<uint16_t> numbered = Numbered(function, argument, table))
                return numbered;

            const size_t           comma  = argument.find(',');
            const std::string_view first  = argument.substr(0, comma);
            const std::string_view second = comma == std::string_view::npos ? std::string_view() : argument.substr(comma + 1);

            if (function == "LT" && comma != std::string_view::npos)
            {
                const auto base  = ValueOfName("_QK_LAYER_TAP", table);
                const auto layer = Number(first, 15);
                const auto key   = Basic(second, table);
                if (base && layer && key)
                    return static_cast<uint16_t>(*base | (*layer << 8) | *key);
                return std::nullopt;
            }
            if (function == "MT" && comma != std::string_view::npos)
            {
                const auto base = ValueOfName("_QK_MOD_TAP", table);
                const auto mods = Mods(first);
                const auto key  = Basic(second, table);
                if (base && mods && key)
                    return static_cast<uint16_t>(*base | ((*mods & 0x1F) << 8) | *key);
                return std::nullopt;
            }
            if (function == "OSM")
            {
                const auto base = ValueOfName("_QK_ONE_SHOT_MOD", table);
                const auto mods = Mods(argument);
                if (base && mods)
                    return static_cast<uint16_t>(*base | (*mods & 0xFF));
                return std::nullopt;
            }
            if (function == "LM" && comma != std::string_view::npos)
            {
                // The layer above the modifiers, as wide as the table's mask: 4 bits, then 5.
                const auto base  = ValueOfName("_QK_LAYER_MOD", table);
                const auto mask  = ValueOfName("_QK_LAYER_MOD_MASK", table);
                const auto layer = Number(first, 15);
                const auto mods  = Mods(second);
                if (!base || !mask || !layer || !mods)
                    return std::nullopt;
                int shift = 0;
                while ((1u << shift) <= *mask)
                    ++shift;
                return static_cast<uint16_t>(*base | (*layer << shift) | (*mods & *mask));
            }

            // Modifier functions, nested: every name but the innermost a modifier.
            uint16_t         value = 0;
            std::string_view rest  = text;
            while (true)
            {
                const size_t paren = rest.find('(');
                if (paren == std::string_view::npos)
                    break;
                const std::optional<uint16_t> modifier =
                    Find(std::begin(c_Modifiers), std::end(c_Modifiers), Trim(rest.substr(0, paren)));
                if (!modifier || rest.back() != ')')
                    return std::nullopt;
                value |= *modifier;
                rest = rest.substr(paren + 1, rest.size() - paren - 2);
            }
            if (value == 0)
                return std::nullopt;
            const std::optional<uint16_t> key = Basic(rest, table);
            return key ? std::optional<uint16_t>(static_cast<uint16_t>(value | *key)) : std::nullopt;
        }
    }

    NameTable NameTableFor(uint16_t protocol) noexcept
    {
        if (protocol <= 9)
            return NameTable::V9;
        if (protocol == 10)
            return NameTable::V10;
        if (protocol == 11)
            return NameTable::V11;
        return NameTable::V12;
    }

    NameTable NameTableFor(qmk::KeycodeVersion version) noexcept
    {
        switch (version)
        {
        case qmk::KeycodeVersion::Legacy:      return NameTable::V9;
        case qmk::KeycodeVersion::LegacyVia10: return NameTable::V10;
        default:                               return NameTable::V12;
        }
    }

    std::optional<uint16_t> ValueOfName(std::string_view name, NameTable table)
    {
        size_t                   count = 0;
        const detail::NameRow*   rows  = detail::NameRows(count);
        const detail::NameRow*   end   = rows + count;
        const detail::NameRow*   row   = std::lower_bound(rows, end, name, [](const detail::NameRow& entry, std::string_view key)
                                                          { return std::string_view(entry.name) < key; });
        if (row == end || name != row->name)
            return std::nullopt;
        const int32_t value = row->values[static_cast<size_t>(table)];
        return value >= 0 ? std::optional<uint16_t>(static_cast<uint16_t>(value)) : std::nullopt;
    }

    // In VIA's order (getByteForCode()): the renamed spellings, the table, the layer keys, the
    // function forms -- and the hex VIA writes and cannot read.
    std::optional<uint16_t> ReadKeycodeName(std::string_view text, NameTable table)
    {
        const std::string spelled = Renamed(Trim(text));
        if (spelled.empty())
            return std::nullopt;
        if (const std::optional<uint16_t> hex = Hex(spelled))
            return hex;
        if (const std::optional<uint16_t> named = ValueOfName(spelled, table))
            return named;
        return Function(Upper(spelled), table);
    }
}
