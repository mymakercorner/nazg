// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialSavedLayout.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "adapters/qmk/NazgQmkExpression.h"
#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaLoader.h"
#include "adapters/vial/NazgVialKeycodeNames.h"
#include "adapters/vial/NazgVialLoader.h"

namespace nazg::vial
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        constexpr qmk::KeycodeVersion c_Versions[] = {
            qmk::KeycodeVersion::Legacy, qmk::KeycodeVersion::LegacyVia10, qmk::KeycodeVersion::V0_0_1,
            qmk::KeycodeVersion::V0_0_2, qmk::KeycodeVersion::V0_0_3,      qmk::KeycodeVersion::V0_0_4,
            qmk::KeycodeVersion::V0_0_5, qmk::KeycodeVersion::V0_0_6,      qmk::KeycodeVersion::V0_0_7,
            qmk::KeycodeVersion::V0_0_8, qmk::KeycodeVersion::V0_0_9,
        };

        std::optional<qmk::KeycodeVersion> VersionNamed(std::string_view name)
        {
            for (qmk::KeycodeVersion version : c_Versions)
                if (name == qmk::KeycodeVersionName(version))
                    return version;
            return std::nullopt;
        }

        std::string Hex(uint16_t value)
        {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%x", value);
            return text;
        }

        std::optional<int64_t> Integer(const Json& value)
        {
            if (value.is_number_integer())
                return value.get<int64_t>();
            return std::nullopt;
        }

        // --- Reading ------------------------------------------------------------------------------

        class Reader
        {
        public:
            Reader(NameTable table, qmk::KeycodeVersion rawVersion, qmk::KeycodeVersion boardVersion)
                : m_Table(table), m_RawVersion(rawVersion), m_BoardVersion(boardVersion)
            {
            }

            // A keycode; none with `unreadable` empty for -1, the file's "no value", and none with it
            // set for what cannot be read.
            std::optional<Keycode> Key(const Json& value, std::string& unreadable) const
            {
                unreadable.clear();
                if (const std::optional<int64_t> number = Integer(value))
                {
                    if (*number == -1)
                        return std::nullopt;
                    if (*number >= 0 && *number <= 0xFFFF)
                        return Decode(static_cast<uint16_t>(*number), unreadable);
                    unreadable = value.dump();
                    return std::nullopt;
                }
                if (!value.is_string())
                {
                    unreadable = value.dump();
                    return std::nullopt;
                }

                const std::string text = value.get<std::string>();
                if (const std::optional<uint16_t> raw = ReadKeycodeName(text, m_Table))
                    return Decode(*raw, unreadable);
                // Written by hand, in QMK's words.
                if (std::optional<Keycode> parsed = qmk::ParseExpression(text, m_RawVersion))
                    return Convert(*parsed, unreadable, text);
                unreadable = text;
                return std::nullopt;
            }

            // A slot's keycode: KC_NO is the slot's "nothing"; what cannot be read spoils the slot.
            bool SlotKey(const Json& value, std::optional<Keycode>& key) const
            {
                std::string unreadable;
                key = Key(value, unreadable);
                if (!unreadable.empty())
                    return false;
                if (key && IsNamed(*key, "KC_NO"))
                    key.reset();
                return true;
            }

            // A keycode as the board numbers it -- macros hold values, not keycodes.
            std::optional<uint16_t> BoardValue(const Json& value) const
            {
                std::string                  unreadable;
                const std::optional<Keycode> key = Key(value, unreadable);
                if (!key)
                    return unreadable.empty() ? std::optional<uint16_t>(0) : std::nullopt;
                return qmk::EncodeKeycode(*key, m_BoardVersion);
            }

        private:
            std::optional<Keycode> Decode(uint16_t raw, std::string& unreadable) const
            {
                return Convert(qmk::DecodeKeycode(raw, m_RawVersion), unreadable, Hex(raw));
            }

            // A value of another numbering than the board's that the codec could not place means
            // nothing on this board.
            std::optional<Keycode> Convert(Keycode key, std::string& unreadable, std::string text) const
            {
                if (m_RawVersion != m_BoardVersion && std::holds_alternative<UnknownKey>(key))
                {
                    unreadable = std::move(text);
                    return std::nullopt;
                }
                return key;
            }

            NameTable           m_Table;
            qmk::KeycodeVersion m_RawVersion;
            qmk::KeycodeVersion m_BoardVersion;
        };

        std::optional<via::MacroActions> ReadMacro(const Json& macro, const Reader& reader)
        {
            if (!macro.is_array())
                return std::nullopt;

            via::MacroActions actions;
            for (const Json& action : macro)
            {
                if (!action.is_array() || action.empty() || !action[0].is_string())
                    return std::nullopt;
                const std::string tag = action[0].get<std::string>();

                if (tag == "text")
                {
                    if (action.size() != 2 || !action[1].is_string())
                        return std::nullopt;
                    for (const char character : action[1].get<std::string>())
                        actions.push_back({ via::MacroAction::Kind::Character, static_cast<uint8_t>(character) });
                }
                else if (tag == "delay")
                {
                    const std::optional<int64_t> milliseconds = action.size() == 2 ? Integer(action[1]) : std::nullopt;
                    if (!milliseconds || *milliseconds < 0)
                        return std::nullopt;
                    actions.push_back({ via::MacroAction::Kind::Wait, static_cast<uint32_t>(*milliseconds) });
                }
                else if (tag == "tap" || tag == "down" || tag == "up")
                {
                    const via::MacroAction::Kind kind = tag == "tap"    ? via::MacroAction::Kind::Tap
                                                        : tag == "down" ? via::MacroAction::Kind::Press
                                                                        : via::MacroAction::Kind::Release;
                    for (size_t index = 1; index < action.size(); ++index)
                    {
                        const std::optional<uint16_t> value = reader.BoardValue(action[index]);
                        if (!value)
                            return std::nullopt;
                        actions.push_back({ kind, *value });
                    }
                }
                // Another tag: skipped, as vial-gui skips it.
            }
            return actions;
        }

        std::optional<TapDance> ReadTapDance(const Json& entry, const Reader& reader)
        {
            if (!entry.is_array() || entry.size() != 5)
                return std::nullopt;
            TapDance dance;
            for (size_t action = 0; action < 4; ++action)
                if (!reader.SlotKey(entry[action], dance.actions[action]))
                    return std::nullopt;
            const std::optional<int64_t> term = Integer(entry[4]);
            if (!term || *term < 0 || *term > 0xFFFF)
                return std::nullopt;
            dance.tappingTerm = static_cast<uint16_t>(*term);
            return dance;
        }

        std::optional<Combo> ReadCombo(const Json& entry, const Reader& reader)
        {
            if (!entry.is_array() || entry.size() != 5)
                return std::nullopt;
            Combo combo;
            for (size_t input = 0; input < 4; ++input)
                if (!reader.SlotKey(entry[input], combo.inputs[input]))
                    return std::nullopt;
            if (!reader.SlotKey(entry[4], combo.output))
                return std::nullopt;
            return combo;
        }

        template <typename T>
        std::optional<T> Field(const Json& entry, const char* name, int64_t largest)
        {
            if (!entry.contains(name))
                return std::nullopt;
            const std::optional<int64_t> value = Integer(entry[name]);
            if (!value || *value < 0 || *value > largest)
                return std::nullopt;
            return static_cast<T>(*value);
        }

        std::optional<KeyOverride> ReadKeyOverride(const Json& entry, const Reader& reader)
        {
            if (!entry.is_object() || !entry.contains("trigger") || !entry.contains("replacement"))
                return std::nullopt;
            KeyOverride rule;
            if (!reader.SlotKey(entry["trigger"], rule.trigger) || !reader.SlotKey(entry["replacement"], rule.replacement))
                return std::nullopt;
            const auto layers  = Field<uint16_t>(entry, "layers", 0xFFFF);
            const auto held    = Field<uint8_t>(entry, "trigger_mods", 0xFF);
            const auto notHeld = Field<uint8_t>(entry, "negative_mod_mask", 0xFF);
            const auto hidden  = Field<uint8_t>(entry, "suppressed_mods", 0xFF);
            const auto options = Field<uint8_t>(entry, "options", 0xFF);
            if (!layers || !held || !notHeld || !hidden || !options)
                return std::nullopt;
            rule.layers  = *layers;
            rule.held    = *held;
            rule.notHeld = *notHeld;
            rule.hidden  = *hidden;
            rule.options = *options;
            return rule;
        }

        std::optional<AltRepeatKey> ReadAltRepeatKey(const Json& entry, const Reader& reader)
        {
            if (!entry.is_object() || !entry.contains("keycode") || !entry.contains("alt_keycode"))
                return std::nullopt;
            AltRepeatKey rule;
            if (!reader.SlotKey(entry["keycode"], rule.lastKey) || !reader.SlotKey(entry["alt_keycode"], rule.altKey))
                return std::nullopt;
            const auto allowed = Field<uint8_t>(entry, "allowed_mods", 0xFF);
            const auto options = Field<uint8_t>(entry, "options", 0xFF);
            if (!allowed || !options)
                return std::nullopt;
            rule.allowed = *allowed;
            rule.options = *options;
            return rule;
        }

        // Each slot of a list, read by `read`; one that cannot be read is said by `what` and its index.
        template <typename T, typename ReadOne>
        std::vector<std::optional<T>> ReadSlots(const Json& file, const char* key, const char* what, const Reader& reader,
                                                ReadOne read, std::vector<std::string>& unreadable)
        {
            std::vector<std::optional<T>> slots;
            if (!file.contains(key) || !file[key].is_array())
                return slots;
            for (const Json& entry : file[key])
            {
                slots.push_back(read(entry, reader));
                if (!slots.back())
                    unreadable.push_back(std::string(what) + " " + std::to_string(slots.size() - 1));
            }
            return slots;
        }

        // --- Writing ------------------------------------------------------------------------------

        class Writer
        {
        public:
            explicit Writer(const SavedLayout& layout)
                : m_Version(layout.keycodeVersion), m_Table(NameTableFor(layout.vialProtocol)),
                  m_Names(layout.vialProtocol >= 0)
            {
            }

            Json Value(uint16_t value) const { return m_Names ? SpellKeycode(value, m_Table) : Hex(value); }

            // -1 where there is no value, as vial-gui writes it.
            Json Key(const std::optional<Keycode>& key) const
            {
                if (!key)
                    return -1;
                const std::optional<uint16_t> value = qmk::EncodeKeycode(*key, m_Version);
                return value ? Value(*value) : Json(-1);
            }

            // A slot's: nothing is KC_NO.
            Json SlotKey(const std::optional<Keycode>& key) const { return key ? Key(key) : Value(0); }

        private:
            qmk::KeycodeVersion m_Version;
            NameTable           m_Table;
            bool                m_Names;
        };

        Json WriteMacro(const via::MacroActions& actions, const Writer& writer)
        {
            Json macro = Json::array();
            for (size_t index = 0; index < actions.size();)
            {
                const via::MacroAction::Kind kind = actions[index].kind;
                if (kind == via::MacroAction::Kind::Wait)
                {
                    macro.push_back(Json::array({ "delay", actions[index].value }));
                    ++index;
                    continue;
                }

                // A run of one kind: the text typed, or the keys tapped, pressed or released in a row.
                Json action = Json::array();
                std::string text;
                action.push_back(kind == via::MacroAction::Kind::Character ? "text"
                                 : kind == via::MacroAction::Kind::Tap     ? "tap"
                                 : kind == via::MacroAction::Kind::Press   ? "down"
                                                                           : "up");
                for (; index < actions.size() && actions[index].kind == kind; ++index)
                {
                    if (kind == via::MacroAction::Kind::Character)
                        text += static_cast<char>(actions[index].value);
                    else
                        action.push_back(writer.Value(static_cast<uint16_t>(actions[index].value)));
                }
                if (kind == via::MacroAction::Kind::Character)
                    action.push_back(text);
                macro.push_back(std::move(action));
            }
            return macro;
        }
    }

    size_t SavedLayout::Rows() const noexcept
    {
        return keymap.empty() ? 0 : keymap.front().size();
    }

    size_t SavedLayout::Columns() const noexcept
    {
        size_t columns = 0;
        if (!keymap.empty())
            for (const auto& row : keymap.front())
                columns = std::max(columns, row.size());
        return columns;
    }

    SavedLayout ReadSavedLayout(std::string_view text, qmk::KeycodeVersion boardVersion)
    {
        const Json file = Json::parse(text.begin(), text.end(), nullptr, false);
        if (file.is_discarded() || !file.is_object())
            throw std::runtime_error("not a Vial saved layout: it is not JSON");
        if (!file.contains("layout") || !file["layout"].is_array())
            throw std::runtime_error("not a Vial saved layout: it has no keymap");

        SavedLayout layout;
        layout.keycodeVersion = boardVersion;

        if (file.contains("uid"))
            if (const Json& uid = file["uid"]; uid.is_number_unsigned() || (uid.is_number_integer() && uid.get<int64_t>() >= 0))
                layout.uid = uid.get<uint64_t>();
        if (file.contains("vial_protocol"))
            layout.vialProtocol = static_cast<int32_t>(Integer(file["vial_protocol"]).value_or(-1));
        if (file.contains("via_protocol"))
            layout.viaProtocol = static_cast<int32_t>(Integer(file["via_protocol"]).value_or(-1));

        // What the file's raw values are numbered in: Nazg's word, else the keycodes of the Vial
        // protocol, else of the VIA protocol -- vial-gui opens only VIA 9 boards, before the
        // renumbering -- else the board's.
        std::optional<qmk::KeycodeVersion> rawVersion;
        if (file.contains("nazg") && file["nazg"].is_object())
        {
            const Json& nazg = file["nazg"];
            layout.vendorId  = Field<uint16_t>(nazg, "vendor_id", 0xFFFF);
            layout.productId = Field<uint16_t>(nazg, "product_id", 0xFFFF);
            if (nazg.contains("keycode_version") && nazg["keycode_version"].is_string())
                rawVersion = VersionNamed(nazg["keycode_version"].get<std::string>());
        }
        if (!rawVersion && layout.vialProtocol >= 0)
            rawVersion = KeycodeVersionFor(static_cast<uint32_t>(layout.vialProtocol));
        if (!rawVersion && layout.viaProtocol >= 0)
            rawVersion = via::KeycodeVersionFor(static_cast<uint16_t>(layout.viaProtocol));
        if (!rawVersion)
            rawVersion = boardVersion;
        const Reader reader(NameTableFor(layout.vialProtocol), *rawVersion, boardVersion);

        std::string unreadable;
        const Json& keymap = file["layout"];
        for (size_t layer = 0; layer < keymap.size() && keymap[layer].is_array(); ++layer)
        {
            auto& rows = layout.keymap.emplace_back();
            for (size_t row = 0; row < keymap[layer].size() && keymap[layer][row].is_array(); ++row)
            {
                auto& cells = rows.emplace_back();
                for (size_t column = 0; column < keymap[layer][row].size(); ++column)
                {
                    cells.push_back(reader.Key(keymap[layer][row][column], unreadable));
                    if (!unreadable.empty() && layer <= 0xFF && row <= 0xFF && column <= 0xFF)
                        layout.unreadableKeys.push_back({ static_cast<uint8_t>(layer), static_cast<uint8_t>(row),
                                                          static_cast<uint8_t>(column), unreadable });
                }
            }
        }

        if (file.contains("encoder_layout") && file["encoder_layout"].is_array())
            for (const Json& layer : file["encoder_layout"])
            {
                auto& knobs = layout.knobs.emplace_back();
                if (!layer.is_array())
                    continue;
                for (const Json& turns : layer)
                {
                    SavedKnob& knob = knobs.emplace_back();
                    if (!turns.is_array() || turns.size() != 2)
                        continue;
                    // Direction 0 first -- counter-clockwise, though vial-gui's code calls it cw.
                    knob.counterClockwise = reader.Key(turns[0], unreadable);
                    if (!unreadable.empty())
                        layout.unreadable.push_back("knob " + std::to_string(knobs.size() - 1) + ": " + unreadable);
                    knob.clockwise = reader.Key(turns[1], unreadable);
                    if (!unreadable.empty())
                        layout.unreadable.push_back("knob " + std::to_string(knobs.size() - 1) + ": " + unreadable);
                }
            }

        if (file.contains("layout_options"))
            if (const std::optional<int64_t> options = Integer(file["layout_options"]); options && *options >= 0 &&
                                                                                        *options <= 0xFFFFFFFF)
                layout.layoutOptions = static_cast<uint32_t>(*options);

        layout.macros        = ReadSlots<via::MacroActions>(file, "macro", "macro", reader, ReadMacro, layout.unreadable);
        layout.tapDances     = ReadSlots<TapDance>(file, "tap_dance", "tap dance", reader, ReadTapDance, layout.unreadable);
        layout.combos        = ReadSlots<Combo>(file, "combo", "combo", reader, ReadCombo, layout.unreadable);
        layout.keyOverrides  = ReadSlots<KeyOverride>(file, "key_override", "key override", reader, ReadKeyOverride,
                                                      layout.unreadable);
        layout.altRepeatKeys = ReadSlots<AltRepeatKey>(file, "alt_repeat_key", "alt repeat key", reader,
                                                       ReadAltRepeatKey, layout.unreadable);

        if (file.contains("settings") && file["settings"].is_object())
            for (const auto& [key, value] : file["settings"].items())
            {
                const std::optional<int64_t> qsid   = key.empty() ? std::nullopt : std::optional<int64_t>(std::strtoll(key.c_str(), nullptr, 10));
                const std::optional<int64_t> number = Integer(value);
                if (qsid && *qsid > 0 && *qsid <= 0xFFFF && number && *number >= 0 && *number <= 0xFFFFFFFF)
                    layout.qmkSettings[static_cast<uint16_t>(*qsid)] = static_cast<uint32_t>(*number);
                else
                    layout.unreadable.push_back("QMK setting " + key);
            }

        return layout;
    }

    std::string WriteSavedLayout(const SavedLayout& layout)
    {
        const Writer writer(layout);
        Json         file;

        file["version"] = 1;
        file["uid"]     = layout.uid ? Json(*layout.uid) : Json(-1);

        Json keymap = Json::array();
        for (const auto& rows : layout.keymap)
        {
            Json layer = Json::array();
            for (const auto& cells : rows)
            {
                Json row = Json::array();
                for (const auto& cell : cells)
                    row.push_back(writer.Key(cell));
                layer.push_back(std::move(row));
            }
            keymap.push_back(std::move(layer));
        }
        file["layout"] = std::move(keymap);

        Json knobs = Json::array();
        for (const auto& layer : layout.knobs)
        {
            Json turns = Json::array();
            for (const SavedKnob& knob : layer)
                turns.push_back(Json::array({ writer.Key(knob.counterClockwise), writer.Key(knob.clockwise) }));
            knobs.push_back(std::move(turns));
        }
        file["encoder_layout"] = std::move(knobs);
        file["layout_options"] = layout.layoutOptions ? Json(*layout.layoutOptions) : Json(-1);

        Json macros = Json::array();
        for (const auto& macro : layout.macros)
            macros.push_back(macro ? WriteMacro(*macro, writer) : Json::array());
        file["macro"] = std::move(macros);

        file["vial_protocol"] = layout.vialProtocol;
        file["via_protocol"]  = layout.viaProtocol;

        Json dances = Json::array();
        for (const auto& dance : layout.tapDances)
        {
            const TapDance entry = dance.value_or(TapDance{});
            dances.push_back(Json::array({ writer.SlotKey(entry.actions[0]), writer.SlotKey(entry.actions[1]),
                                           writer.SlotKey(entry.actions[2]), writer.SlotKey(entry.actions[3]),
                                           entry.tappingTerm }));
        }
        file["tap_dance"] = std::move(dances);

        Json combos = Json::array();
        for (const auto& combo : layout.combos)
        {
            const Combo entry = combo.value_or(Combo{});
            combos.push_back(Json::array({ writer.SlotKey(entry.inputs[0]), writer.SlotKey(entry.inputs[1]),
                                           writer.SlotKey(entry.inputs[2]), writer.SlotKey(entry.inputs[3]),
                                           writer.SlotKey(entry.output) }));
        }
        file["combo"] = std::move(combos);

        Json overrides = Json::array();
        for (const auto& rule : layout.keyOverrides)
        {
            const KeyOverride entry = rule.value_or(KeyOverride{});
            Json              json;
            json["trigger"]           = writer.SlotKey(entry.trigger);
            json["replacement"]       = writer.SlotKey(entry.replacement);
            json["layers"]            = entry.layers;
            json["trigger_mods"]      = entry.held;
            json["negative_mod_mask"] = entry.notHeld;
            json["suppressed_mods"]   = entry.hidden;
            json["options"]           = entry.options;
            overrides.push_back(std::move(json));
        }
        file["key_override"] = std::move(overrides);

        Json repeats = Json::array();
        for (const auto& rule : layout.altRepeatKeys)
        {
            const AltRepeatKey entry = rule.value_or(AltRepeatKey{});
            Json               json;
            json["keycode"]      = writer.SlotKey(entry.lastKey);
            json["alt_keycode"]  = writer.SlotKey(entry.altKey);
            json["allowed_mods"] = entry.allowed;
            json["options"]      = entry.options;
            repeats.push_back(std::move(json));
        }
        file["alt_repeat_key"] = std::move(repeats);

        Json settings = Json::object();
        for (const auto& [qsid, value] : layout.qmkSettings)
            settings[std::to_string(qsid)] = value;
        file["settings"] = std::move(settings);

        // Nazg's own: vial-gui reads only the keys it knows.
        Json nazg;
        if (layout.vendorId)
            nazg["vendor_id"] = *layout.vendorId;
        if (layout.productId)
            nazg["product_id"] = *layout.productId;
        nazg["keycode_version"] = qmk::KeycodeVersionName(layout.keycodeVersion);
        file["nazg"]            = std::move(nazg);

        return file.dump();
    }
}
