// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaSavedLayout.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"

namespace nazg::via
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        Json Parse(std::string_view text)
        {
            return Json::parse(text.begin(), text.end(), nullptr, false);
        }

        bool Looks(const Json& file)
        {
            return file.is_object() && file.contains("layers") && file["layers"].is_array() &&
                   file.contains("vendorProductId") && file["vendorProductId"].is_number_integer();
        }

        class Reader
        {
        public:
            Reader(NameTable table, qmk::KeycodeVersion boardVersion) : m_Table(table), m_BoardVersion(boardVersion) {}

            // A keycode; none with `unreadable` set for what cannot be read.
            std::optional<Keycode> Key(const Json& value, std::string& unreadable) const
            {
                unreadable.clear();
                if (value.is_string())
                    if (const std::optional<uint16_t> raw = ReadKeycodeName(value.get<std::string>(), m_Table))
                        return qmk::DecodeKeycode(*raw, m_BoardVersion);
                unreadable = value.is_string() ? value.get<std::string>() : value.dump();
                if (unreadable.empty())
                    unreadable = "\"\"";
                return std::nullopt;
            }

            // A macro, from VIA's expression (expressionToSequence()): none when a part cannot be read.
            std::optional<MacroActions> Macro(const Json& value) const
            {
                if (!value.is_string())
                    return std::nullopt;
                const std::string expression = value.get<std::string>();

                MacroActions actions;
                for (size_t index = 0; index < expression.size();)
                {
                    const char character = expression[index];
                    if (character == '\\' && index + 1 < expression.size() && expression[index + 1] == '{')
                    {
                        actions.push_back({ MacroAction::Kind::Character, '{' });
                        index += 2;
                        continue;
                    }
                    if (character == '{')
                    {
                        // Unclosed, VIA types it as it is.
                        const size_t close = expression.find('}', index + 1);
                        if (close != std::string::npos)
                        {
                            if (!Block(std::string_view(expression).substr(index + 1, close - index - 1), actions))
                                return std::nullopt;
                            index = close + 1;
                            continue;
                        }
                    }
                    // Typed through the firmware's US table: ASCII only.
                    if (static_cast<unsigned char>(character) >= 0x80)
                        return std::nullopt;
                    actions.push_back({ MacroAction::Kind::Character, static_cast<uint8_t>(character) });
                    ++index;
                }
                return actions;
            }

        private:
            // {100} a wait; {KC_A} a tap; {KC_LCTL,KC_C} a chord, pressed in order and released in
            // reverse; {+KC_LSFT} a press, {-KC_LSFT} a release. Names of the table alone.
            bool Block(std::string_view block, MacroActions& actions) const
            {
                if (!block.empty() && block.find_first_not_of("0123456789") == std::string_view::npos)
                {
                    if (block.size() > 9)
                        return false;
                    actions.push_back({ MacroAction::Kind::Wait, static_cast<uint32_t>(std::stoul(std::string(block))) });
                    return true;
                }

                std::optional<MacroAction::Kind> direction;
                if (!block.empty() && (block.front() == '+' || block.front() == '-'))
                {
                    direction = block.front() == '+' ? MacroAction::Kind::Press : MacroAction::Kind::Release;
                    block.remove_prefix(1);
                }

                std::vector<uint16_t> keys;
                while (true)
                {
                    const size_t comma = block.find(',');
                    std::string  name;
                    for (const char character : block.substr(0, comma))
                        if (!std::isspace(static_cast<unsigned char>(character)))
                            name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
                    if (!name.empty())
                    {
                        const std::optional<uint16_t> value = ValueOfName(name, m_Table);
                        if (!value)
                            return false;
                        keys.push_back(*value);
                    }
                    if (comma == std::string_view::npos)
                        break;
                    block.remove_prefix(comma + 1);
                }
                if (keys.empty())
                    return true;   // {} -- VIA keeps nothing of it

                if (direction)
                    actions.push_back({ *direction, keys.front() });
                else if (keys.size() == 1)
                    actions.push_back({ MacroAction::Kind::Tap, keys.front() });
                else
                {
                    for (const uint16_t key : keys)
                        actions.push_back({ MacroAction::Kind::Press, key });
                    for (auto key = keys.rbegin(); key != keys.rend(); ++key)
                        actions.push_back({ MacroAction::Kind::Release, *key });
                }
                return true;
            }

            NameTable           m_Table;
            qmk::KeycodeVersion m_BoardVersion;
        };
    }

    bool IsSavedLayout(std::string_view text)
    {
        return Looks(Parse(text));
    }

    SavedLayout ReadSavedLayout(std::string_view text, NameTable table, qmk::KeycodeVersion boardVersion, uint8_t rows,
                                uint8_t columns)
    {
        const Json file = Parse(text);
        if (file.is_discarded() || !file.is_object())
            throw std::runtime_error("not a VIA saved layout: it is not JSON");
        if (!Looks(file))
            throw std::runtime_error("not a VIA saved layout: it has no layers or no vendorProductId");

        SavedLayout layout;
        layout.format         = SavedLayoutFormat::Via;
        layout.keycodeVersion = boardVersion;

        if (const int64_t id = file["vendorProductId"].get<int64_t>(); id >= 0 && id <= 0xFFFFFFFF)
        {
            layout.vendorId  = static_cast<uint16_t>(id >> 16);
            layout.productId = static_cast<uint16_t>(id & 0xFFFF);
        }

        const Reader reader(table, boardVersion);
        std::string  unreadable;

        // Every matrix cell of a layer in one list, row after row: placed only with the board's matrix.
        const Json&  layers = file["layers"];
        const size_t cells  = static_cast<size_t>(rows) * columns;
        for (size_t layer = 0; layer < layers.size() && layer <= 0xFF; ++layer)
        {
            const Json& keys = layers[layer];
            if (!keys.is_array() || keys.size() != cells || cells == 0)
            {
                layout.keymap.clear();
                layout.unreadableKeys.clear();
                layout.unreadable.push_back("the keymap: its layer " + std::to_string(layer) + " has " +
                                            std::to_string(keys.is_array() ? keys.size() : 0) + " keys, this board's matrix " +
                                            std::to_string(cells));
                break;
            }

            auto& grid = layout.keymap.emplace_back(rows, std::vector<std::optional<Keycode>>(columns));
            for (size_t index = 0; index < cells; ++index)
            {
                const uint8_t row    = static_cast<uint8_t>(index / columns);
                const uint8_t column = static_cast<uint8_t>(index % columns);
                grid[row][column]    = reader.Key(keys[index], unreadable);
                if (!unreadable.empty())
                    layout.unreadableKeys.push_back({ static_cast<uint8_t>(layer), row, column, unreadable });
            }
        }

        // [encoder][layer] -> [ccw, cw], into the model's [layer][knob].
        if (file.contains("encoders") && file["encoders"].is_array())
        {
            const Json& encoders = file["encoders"];
            for (size_t knob = 0; knob < encoders.size(); ++knob)
            {
                if (!encoders[knob].is_array())
                    continue;
                for (size_t layer = 0; layer < encoders[knob].size(); ++layer)
                {
                    if (layout.knobs.size() <= layer)
                        layout.knobs.resize(layer + 1);
                    if (layout.knobs[layer].size() <= knob)
                        layout.knobs[layer].resize(knob + 1);

                    const Json& turns = encoders[knob][layer];
                    if (!turns.is_array() || turns.size() != 2)
                        continue;
                    SavedKnob& saved       = layout.knobs[layer][knob];
                    saved.counterClockwise = reader.Key(turns[0], unreadable);
                    if (!unreadable.empty())
                        layout.unreadable.push_back("knob " + std::to_string(knob) + ": " + unreadable);
                    saved.clockwise = reader.Key(turns[1], unreadable);
                    if (!unreadable.empty())
                        layout.unreadable.push_back("knob " + std::to_string(knob) + ": " + unreadable);
                }
            }
        }

        if (file.contains("macros") && file["macros"].is_array())
            for (const Json& macro : file["macros"])
            {
                layout.macros.push_back(reader.Macro(macro));
                if (!layout.macros.back())
                    layout.unreadable.push_back("macro " + std::to_string(layout.macros.size() - 1));
            }

        return layout;
    }
}
