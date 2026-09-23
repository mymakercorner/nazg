// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboard.h"

#include <cassert>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace nazg
{
    namespace
    {
        // How many bits a group occupies in the packed layout options value: enough to
        // hold its largest option index.
        uint32_t BitsForGroup(size_t optionCount) noexcept
        {
            uint32_t bits  = 1;
            size_t   range = 2;

            while (range < optionCount)
            {
                range *= 2;
                ++bits;
            }

            return bits;
        }
    }

    Keymap::Keymap(uint8_t layers, uint8_t rows, uint8_t columns)
        : m_Keycodes(static_cast<size_t>(layers) * rows * columns, Keycode{ NamedKey{ "KC_NO" } }),
          m_Layers(layers),
          m_Rows(rows),
          m_Columns(columns)
    {
    }

    size_t Keymap::IndexOf(uint8_t layer, uint8_t row, uint8_t column) const
    {
        assert(Contains(layer, row, column) && "keymap access outside the matrix");

        return ((static_cast<size_t>(layer) * m_Rows + row) * m_Columns) + column;
    }

    bool Keymap::Contains(uint8_t layer, uint8_t row, uint8_t column) const noexcept
    {
        return layer < m_Layers && row < m_Rows && column < m_Columns;
    }

    const Keycode& Keymap::At(uint8_t layer, uint8_t row, uint8_t column) const
    {
        return m_Keycodes[IndexOf(layer, row, column)];
    }

    void Keymap::Set(uint8_t layer, uint8_t row, uint8_t column, Keycode keycode)
    {
        m_Keycodes[IndexOf(layer, row, column)] = std::move(keycode);
    }

    std::vector<LayoutOptionGroup> ParseLayoutGroups(const std::vector<std::string>& labels)
    {
        std::vector<LayoutOptionGroup> groups;
        groups.reserve(labels.size());

        for (const std::string& label : labels)
        {
            LayoutOptionGroup group;

            // ParseDefinition keeps a non-string label as its JSON text, so an array
            // arrives here as "[\"Enter\",\"ISO Enter\",\"ANSI Enter\"]": the first
            // element names the group, the rest name its options. A plain string is a
            // toggle with no option names.
            if (!label.empty() && label.front() == '[')
            {
                const nlohmann::json parsed = nlohmann::json::parse(label, nullptr, false);

                if (parsed.is_array() && !parsed.empty())
                {
                    group.name = parsed[0].is_string() ? parsed[0].get<std::string>() : std::string();

                    for (size_t i = 1; i < parsed.size(); ++i)
                        if (parsed[i].is_string())
                            group.options.push_back(parsed[i].get<std::string>());
                }
                else
                {
                    group.name = label;
                }
            }
            else
            {
                group.name = label;
            }

            groups.push_back(std::move(group));
        }

        return groups;
    }

    std::vector<uint8_t> DecodeLayoutOptions(uint32_t raw, const std::vector<LayoutOptionGroup>& groups)
    {
        std::vector<uint8_t> selection(groups.size(), 0);

        // The last group occupies the lowest bits, so unpacking runs backwards.
        uint32_t remaining = raw;
        for (size_t i = groups.size(); i-- > 0;)
        {
            const uint32_t bits = BitsForGroup(groups[i].Count());
            const uint32_t mask = (1u << bits) - 1u;

            selection[i] = static_cast<uint8_t>(remaining & mask);
            remaining >>= bits;
        }

        return selection;
    }

    bool Keyboard::IsKeyVisible(const DefinitionKey& key) const noexcept
    {
        if (key.layoutIndex < 0)
            return true;

        const size_t group = static_cast<size_t>(key.layoutIndex);

        // A key belonging to a group the definition never described cannot be resolved,
        // so it is shown rather than silently dropped.
        if (group >= layoutSelection.size())
            return true;

        return layoutSelection[group] == key.layoutOption;
    }

    Keycode Keyboard::KeycodeFor(const DefinitionKey& key, uint8_t layer) const
    {
        if (!keymap.Contains(layer, key.row, key.column))
            return NamedKey{ "KC_NO" };

        return keymap.At(layer, key.row, key.column);
    }

    Keyboard BuildKeyboard(KeyboardDefinition definition,
                           Keymap             keymap,
                           uint32_t           layoutOptions,
                           QmkKeycodeVersion  keycodeVersion)
    {
        if (definition.matrixRows == 0 || definition.matrixColumns == 0)
            throw std::invalid_argument("the definition declares an empty matrix");

        if (keymap.Layers() == 0 || keymap.Rows() != definition.matrixRows ||
            keymap.Columns() != definition.matrixColumns)
            throw std::invalid_argument("the keymap is " + std::to_string(keymap.Rows()) + " x " +
                                        std::to_string(keymap.Columns()) + " but the definition declares " +
                                        std::to_string(definition.matrixRows) + " x " +
                                        std::to_string(definition.matrixColumns));

        Keyboard keyboard;
        keyboard.keymap         = std::move(keymap);
        keyboard.keycodeVersion = keycodeVersion;

        keyboard.layoutOptions   = layoutOptions;
        keyboard.layoutSelection = DecodeLayoutOptions(layoutOptions,
                                                       ParseLayoutGroups(definition.layoutLabels));
        keyboard.definition      = std::move(definition);

        return keyboard;
    }
}
