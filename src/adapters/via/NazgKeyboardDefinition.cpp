// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardDefinition.h"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

namespace nazg
{
    namespace
    {
        std::string Trim(const std::string& text)
        {
            const size_t first = text.find_first_not_of(" \t");
            if (first == std::string::npos)
                return {};
            const size_t last = text.find_last_not_of(" \t");
            return text.substr(first, last - first + 1);
        }

        // VIA definitions write the USB ids as strings, and a string is always hex, as VIA
        // reads it: "0x1209", but also "414B" or "BF00" with no prefix, which real
        // definitions have. Some are plain JSON numbers, also accepted.
        uint16_t ParseUsbId(const nlohmann::json& value, const char* what)
        {
            if (value.is_number_unsigned() && value.get<uint64_t>() <= 0xFFFF)
                return static_cast<uint16_t>(value.get<uint64_t>());

            if (value.is_string())
            {
                const std::string text   = value.get<std::string>();
                std::string       digits = Trim(text);
                if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
                    digits = digits.substr(2);

                const bool hex = !digits.empty() && digits.size() <= 4 &&
                                 digits.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
                if (hex)
                    return static_cast<uint16_t>(std::stoul(digits, nullptr, 16));

                throw ProtocolError(std::string("definition has an unreadable ") + what + ": " + text);
            }

            throw ProtocolError(std::string("definition has no usable ") + what);
        }

        // A KLE key label is newline-separated positions. Position 0 carries the matrix
        // cell as "row,col"; position 3, when present, carries "layoutIndex,option".
        // Anything else -- a decal, a legend-only key -- has no position 0 to parse and
        // is skipped by the caller.
        //
        // Spaces around either number are allowed, as VIA allows them: real definitions
        // have "0,8    " and "1 ,0".
        bool ParseNumber(const std::string& text, int& value)
        {
            const std::string trimmed = Trim(text);
            if (trimmed.empty())
                return false;

            try
            {
                size_t consumed = 0;
                value = std::stoi(trimmed, &consumed);
                return consumed == trimmed.size();
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        bool ParsePair(const std::string& text, int& first, int& second)
        {
            const size_t comma = text.find(',');
            if (comma == std::string::npos)
                return false;

            if (!ParseNumber(text.substr(0, comma), first) || !ParseNumber(text.substr(comma + 1), second))
                return false;

            return first >= 0 && second >= 0;
        }

        std::vector<std::string> SplitLabels(const std::string& label)
        {
            std::vector<std::string> parts;
            size_t                   start = 0;

            for (;;)
            {
                const size_t newline = label.find('\n', start);
                if (newline == std::string::npos)
                {
                    parts.push_back(label.substr(start));
                    break;
                }

                parts.push_back(label.substr(start, newline - start));
                start = newline + 1;
            }

            return parts;
        }

        float ReadFloat(const nlohmann::json& object, const char* key, float fallback)
        {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_number())
                return fallback;

            return found->get<float>();
        }

        bool ReadBool(const nlohmann::json& object, const char* key, bool fallback)
        {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_boolean())
                return fallback;

            return found->get<bool>();
        }

        // Where the next key goes, and the properties it will take. KLE's semantics, as
        // its reference parser kle-serial implements them.
        struct KleCursor
        {
            float x = 0.0f;
            float y = 0.0f;

            // Set by rx/ry: the cursor jumps there, and every later row starts at clusterX
            // rather than 0. It is also the origin keys rotate about. Boards use it
            // without any rotation too, to place a block of keys -- often a layout
            // alternative -- somewhere else.
            float clusterX = 0.0f;
            float clusterY = 0.0f;

            // Set by r, and kept -- across keys and rows -- until the next r.
            float rotation = 0.0f;

            float width        = 1.0f;
            float height       = 1.0f;
            float secondX      = 0.0f;
            float secondY      = 0.0f;
            float secondWidth  = 0.0f;
            float secondHeight = 0.0f;
            bool  decal        = false;

            // A property object changes only what it names: {"w": 2.25}, {"c": "#777"}
            // gives the next key a width of 2.25.
            void Apply(const nlohmann::json& entry)
            {
                rotation = ReadFloat(entry, "r", rotation);

                const bool movesCluster = entry.contains("rx") || entry.contains("ry");
                clusterX = ReadFloat(entry, "rx", clusterX);
                clusterY = ReadFloat(entry, "ry", clusterY);
                if (movesCluster)
                {
                    x = clusterX;
                    y = clusterY;
                }

                x += ReadFloat(entry, "x", 0.0f);
                y += ReadFloat(entry, "y", 0.0f);

                width        = ReadFloat(entry, "w", width);
                height       = ReadFloat(entry, "h", height);
                secondX      = ReadFloat(entry, "x2", secondX);
                secondY      = ReadFloat(entry, "y2", secondY);
                secondWidth  = ReadFloat(entry, "w2", secondWidth);
                secondHeight = ReadFloat(entry, "h2", secondHeight);
                decal        = ReadBool(entry, "d", decal);
            }

            // After each key: step past it, and the per-key properties start over.
            void Advance()
            {
                x += width;
                ResetKeyProperties();
            }

            // A row ending on a property object with no key after it drops those
            // properties, as VIA does -- kle-serial would carry them to the next row's
            // first key, and bm16a's 14x5 decal would swallow key (0,0).
            void NextRow()
            {
                x = clusterX;
                y += 1.0f;
                ResetKeyProperties();
            }

            void ResetKeyProperties()
            {
                width        = 1.0f;
                height       = 1.0f;
                secondX      = 0.0f;
                secondY      = 0.0f;
                secondWidth  = 0.0f;
                secondHeight = 0.0f;
                decal        = false;
            }
        };

        // Walks one KLE row. Property objects apply to the keys that FOLLOW them, and
        // x/y are relative offsets rather than absolute positions -- that is the whole
        // trick of the format, and getting it wrong shifts half a keyboard sideways.
        void ParseKeymapRow(const nlohmann::json& row, KleCursor& cursor, std::vector<DefinitionKey>& keys)
        {
            for (const nlohmann::json& entry : row)
            {
                if (entry.is_object())
                {
                    cursor.Apply(entry);
                    continue;
                }

                if (!entry.is_string())
                    continue;

                const std::vector<std::string> labels = SplitLabels(entry.get<std::string>());

                // A key marked "d" is a decal whatever its label says -- VIA ignores its
                // matrix cell too -- but it keeps its layout option, since lining up that
                // option's choices may depend on it.
                int        row_     = 0;
                int        column   = 0;
                const bool isSwitch = !cursor.decal && !labels.empty() && ParsePair(labels[0], row_, column);
                if (isSwitch || cursor.decal)
                {
                    DefinitionKey key;
                    key.x            = cursor.x;
                    key.y            = cursor.y;
                    key.width        = cursor.width;
                    key.height       = cursor.height;
                    key.secondX      = cursor.secondX;
                    key.secondY      = cursor.secondY;
                    key.secondWidth  = cursor.secondWidth;
                    key.secondHeight = cursor.secondHeight;
                    key.rotation     = cursor.rotation;
                    key.rotationX    = cursor.clusterX;
                    key.rotationY    = cursor.clusterY;
                    key.decal        = cursor.decal;
                    if (isSwitch)
                    {
                        key.row    = static_cast<uint8_t>(row_);
                        key.column = static_cast<uint8_t>(column);
                    }

                    int layoutIndex  = 0;
                    int layoutOption = 0;
                    if (labels.size() > 3 && ParsePair(labels[3], layoutIndex, layoutOption))
                    {
                        key.layoutIndex  = layoutIndex;
                        key.layoutOption = layoutOption;
                    }

                    keys.push_back(key);
                }
                // else: a legend-only key with no matrix cell. It occupies space but is
                // not a switch, so it advances the cursor without being recorded.

                cursor.Advance();
            }
        }

        void ParseSourceKeymap(const nlohmann::json& keymap, std::vector<DefinitionKey>& keys)
        {
            KleCursor cursor;
            for (const nlohmann::json& row : keymap)
            {
                if (!row.is_array())
                    continue;

                ParseKeymapRow(row, cursor, keys);
                cursor.NextRow();
            }
        }

        // One key of the converted form: every field explicit, positions absolute. Only
        // what DefinitionKey holds is read -- colour, encoder and LED indexes are not.
        //
        // A key with no matrix cell (row -1) that is not a decal is an encoder drawn on
        // the board; like the source form's legend-only keys, it is not recorded.
        void ParseConvertedKey(const nlohmann::json& entry, int layoutIndex, int layoutOption,
                               std::vector<DefinitionKey>& keys)
        {
            if (!entry.is_object())
                return;

            DefinitionKey key;
            key.decal = ReadBool(entry, "d", false);

            // Wider than int on purpose: a hostile 2^32 + 5 must not wrap into cell 5.
            auto readCell = [&entry](const char* name, int64_t& value)
            {
                const auto found = entry.find(name);
                if (found == entry.end() || !found->is_number_integer())
                    return false;
                value = found->get<int64_t>();
                return value >= 0 && value <= 255;
            };

            int64_t    row     = 0;
            int64_t    column  = 0;
            const bool hasCell = readCell("row", row) && readCell("col", column);

            if (!key.decal && !hasCell)
                return;

            if (!key.decal)
            {
                key.row    = static_cast<uint8_t>(row);
                key.column = static_cast<uint8_t>(column);
            }

            key.x            = ReadFloat(entry, "x", 0.0f);
            key.y            = ReadFloat(entry, "y", 0.0f);
            key.width        = ReadFloat(entry, "w", 1.0f);
            key.height       = ReadFloat(entry, "h", 1.0f);
            key.secondX      = ReadFloat(entry, "x2", 0.0f);
            key.secondY      = ReadFloat(entry, "y2", 0.0f);
            key.secondWidth  = ReadFloat(entry, "w2", 0.0f);
            key.secondHeight = ReadFloat(entry, "h2", 0.0f);
            key.rotation     = ReadFloat(entry, "r", 0.0f);
            key.rotationX    = ReadFloat(entry, "rx", 0.0f);
            key.rotationY    = ReadFloat(entry, "ry", 0.0f);
            key.layoutIndex  = layoutIndex;
            key.layoutOption = layoutOption;

            keys.push_back(key);
        }

        // optionKeys names its groups and choices by JSON object keys: "0", "1", ...
        bool ParseIndex(const std::string& text, int& index)
        {
            if (text.empty() || text.size() > 3)
                return false;

            index = 0;
            for (const char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                index = index * 10 + (c - '0');
            }
            return true;
        }

        void ParseConvertedLayout(const nlohmann::json& layouts, const nlohmann::json& mainKeys,
                                  std::vector<DefinitionKey>& keys)
        {
            for (const nlohmann::json& entry : mainKeys)
                ParseConvertedKey(entry, -1, -1, keys);

            const auto optionKeys = layouts.find("optionKeys");
            if (optionKeys == layouts.end() || !optionKeys->is_object())
                return;

            for (const auto& [groupName, choices] : optionKeys->items())
            {
                int group = 0;
                if (!ParseIndex(groupName, group) || !choices.is_object())
                    continue;

                for (const auto& [choiceName, choiceKeys] : choices.items())
                {
                    int choice = 0;
                    if (!ParseIndex(choiceName, choice) || !choiceKeys.is_array())
                        continue;

                    for (const nlohmann::json& entry : choiceKeys)
                        ParseConvertedKey(entry, group, choice, keys);
                }
            }
        }

        // A plain string, or a dynamic name: {"options": [...], "content": [...]}, where
        // the board picks the option through a custom value.
        std::string ParseName(const nlohmann::json& document)
        {
            const auto name = document.find("name");
            if (name == document.end())
                return {};

            if (name->is_string())
                return name->get<std::string>();

            if (name->is_object())
            {
                const auto options = name->find("options");
                if (options != name->end() && options->is_array() && !options->empty() &&
                    options->front().is_string())
                    return options->front().get<std::string>();
            }

            return {};
        }

        // ----------------------------------------------------------------------------------
        // Custom menus. A menu the parser cannot make sense of in places is still the board's:
        // what cannot be read is left out -- a control bound to nothing, an option that is not
        // one -- never the whole definition.

        // A label that is not a string is left empty rather than refused: the column names the
        // menu somehow.
        std::string LabelOf(const nlohmann::json& entry)
        {
            const auto label = entry.find("label");
            return label != entry.end() && label->is_string() ? label->get<std::string>() : std::string();
        }

        // `conditions` with the entry's own showIf added.
        std::vector<std::string> ConditionsOf(const nlohmann::json& entry, std::vector<std::string> conditions)
        {
            const auto showIf = entry.find("showIf");
            if (showIf != entry.end() && showIf->is_string())
                conditions.push_back(showIf->get<std::string>());
            return conditions;
        }

        std::optional<uint8_t> ByteOf(const nlohmann::json& value)
        {
            if (value.is_number_integer() && value.get<int64_t>() >= 0 && value.get<int64_t>() <= 255)
                return static_cast<uint8_t>(value.get<int64_t>());
            return std::nullopt;
        }

        // A toggle's state, a number or an array of them.
        std::optional<std::vector<uint8_t>> BytesOf(const nlohmann::json& value)
        {
            if (const std::optional<uint8_t> byte = ByteOf(value))
                return std::vector<uint8_t>{ *byte };
            if (!value.is_array() || value.empty())
                return std::nullopt;
            std::vector<uint8_t> bytes;
            for (const nlohmann::json& each : value)
            {
                const std::optional<uint8_t> byte = ByteOf(each);
                if (!byte)
                    return std::nullopt;
                bytes.push_back(*byte);
            }
            return bytes;
        }

        std::optional<RangeConstraint> ParseConstraint(const nlohmann::json& entry)
        {
            if (!entry.is_object())
                return std::nullopt;
            RangeConstraint constraint;
            const std::string operation = entry.value("operator", std::string());
            if (operation == "<")
                constraint.operation = RangeConstraint::Operation::Less;
            else if (operation == "<=")
                constraint.operation = RangeConstraint::Operation::LessOrEqual;
            else if (operation == ">")
                constraint.operation = RangeConstraint::Operation::Greater;
            else if (operation == ">=")
                constraint.operation = RangeConstraint::Operation::GreaterOrEqual;
            else
                return std::nullopt;

            // The other control by its value's name, or by its whole binding, whose name is first.
            const auto reference = entry.find("reference");
            if (reference != entry.end() && reference->is_string())
                constraint.reference = reference->get<std::string>();
            else if (reference != entry.end() && reference->is_array() && !reference->empty() && reference->front().is_string())
                constraint.reference = reference->front().get<std::string>();
            else
                return std::nullopt;

            if (const auto offset = entry.find("offset"); offset != entry.end() && offset->is_number_integer())
                constraint.offset = offset->get<int>();
            constraint.push = entry.value("onViolation", std::string()) == "push";
            return constraint;
        }

        std::optional<CustomControl> ParseControl(const nlohmann::json& entry, std::vector<std::string> conditions)
        {
            using Type = CustomControl::Type;
            CustomControl control;
            control.label      = LabelOf(entry);
            control.conditions = std::move(conditions);

            const std::string type = entry.value("type", std::string());
            if (type == "range")
                control.type = Type::Range;
            else if (type == "dropdown")
                control.type = Type::Dropdown;
            else if (type == "toggle")
                control.type = Type::Toggle;
            else if (type == "keycode")
                control.type = Type::Keycode;
            else if (type == "color")
                control.type = Type::Colour;
            else if (type == "button")
                control.type = Type::Button;
            else if (type == "label")
                control.type = Type::Label;
            else
                return std::nullopt;   // VIA's color-palette, which no definition uses, or a typo

            // [name, channel, value id, extra...] -- or a fixed label's [text].
            const auto content = entry.find("content");
            if (content == entry.end() || !content->is_array() || content->empty() || !content->front().is_string())
                return std::nullopt;
            if (content->size() == 1)
            {
                if (control.type != Type::Label)
                    return std::nullopt;
                control.text = content->front().get<std::string>();
            }
            else
            {
                if (content->size() < 3)
                    return std::nullopt;
                std::vector<uint8_t> bytes;
                for (size_t index = 1; index < content->size(); ++index)
                {
                    const std::optional<uint8_t> byte = ByteOf((*content)[index]);
                    if (!byte)
                        return std::nullopt;
                    bytes.push_back(*byte);
                }
                control.isBound = true;
                control.name    = content->front().get<std::string>();
                control.channel = bytes[0];
                control.valueId = bytes[1];
                control.extra.assign(bytes.begin() + 2, bytes.end());
            }

            const auto options = entry.find("options");
            const bool hasList = options != entry.end() && options->is_array();
            switch (control.type)
            {
                case Type::Range:
                    if (!hasList || options->size() != 2 || !(*options)[0].is_number_integer() || !(*options)[1].is_number_integer())
                        return std::nullopt;
                    control.minimum = (*options)[0].get<int>();
                    control.maximum = (*options)[1].get<int>();
                    if (control.minimum < 0 || control.maximum > 0xFFFF || control.minimum > control.maximum)
                        return std::nullopt;
                    if (const auto unit = entry.find("unit"); unit != entry.end() && unit->is_string())
                        control.unit = unit->get<std::string>();
                    if (const auto constraints = entry.find("constraints"); constraints != entry.end() && constraints->is_array())
                        for (const nlohmann::json& each : *constraints)
                            if (const std::optional<RangeConstraint> constraint = ParseConstraint(each))
                                control.constraints.push_back(*constraint);
                    break;

                case Type::Dropdown:
                    if (!hasList)
                        return std::nullopt;
                    // A name alone is worth its place; a [name, value] pair its value -- as the VIA
                    // app reads it, a value of 0 its place too.
                    for (size_t index = 0; index < options->size(); ++index)
                    {
                        const nlohmann::json& option = (*options)[index];
                        if (option.is_string())
                            control.options.emplace_back(option.get<std::string>(), static_cast<uint8_t>(index));
                        else if (option.is_array() && option.size() >= 2 && option[0].is_string())
                        {
                            const std::optional<uint8_t> value = ByteOf(option[1]);
                            if (value)
                                control.options.emplace_back(option[0].get<std::string>(),
                                                             *value != 0 ? *value : static_cast<uint8_t>(index));
                        }
                    }
                    break;

                case Type::Toggle:
                    if (hasList && options->size() == 2)
                    {
                        const std::optional<std::vector<uint8_t>> off = BytesOf((*options)[0]);
                        const std::optional<std::vector<uint8_t>> on  = BytesOf((*options)[1]);
                        if (off && on)
                        {
                            control.off = *off;
                            control.on  = *on;
                        }
                    }
                    break;

                case Type::Button:
                    if (hasList && options->size() == 1)
                        if (const std::optional<uint8_t> value = ByteOf((*options)[0]))
                            control.on = { *value };
                    break;

                default:
                    break;
            }
            return control;
        }

        // A submenu's lines, slices flattened: an entry with a type is a control, one with content
        // and no label a slice of them.
        void ParseControls(const nlohmann::json& list, const std::vector<std::string>& conditions,
                           std::vector<CustomControl>& controls)
        {
            if (!list.is_array())
                return;
            for (const nlohmann::json& entry : list)
            {
                if (!entry.is_object())
                    continue;
                std::vector<std::string> held = ConditionsOf(entry, conditions);
                if (entry.contains("type"))
                {
                    if (std::optional<CustomControl> control = ParseControl(entry, std::move(held)))
                        controls.push_back(std::move(*control));
                }
                else if (const auto content = entry.find("content"); content != entry.end())
                {
                    if (content->is_array() && !entry.contains("label"))
                        ParseControls(*content, held, controls);
                    else if (content->is_string())
                    {
                        // A line of text alone: VIA's text item.
                        CustomControl text;
                        text.label      = LabelOf(entry);
                        text.conditions = std::move(held);
                        text.text       = content->get<std::string>();
                        controls.push_back(std::move(text));
                    }
                }
            }
        }

        // A menu's submenus, slices flattened: an entry with a label is a submenu.
        void ParseSubmenus(const nlohmann::json& list, const std::vector<std::string>& conditions,
                           std::vector<CustomSubmenu>& submenus)
        {
            if (!list.is_array())
                return;
            for (const nlohmann::json& entry : list)
            {
                if (!entry.is_object())
                    continue;
                std::vector<std::string> held = ConditionsOf(entry, conditions);
                const auto               content = entry.find("content");
                if (entry.contains("label"))
                {
                    CustomSubmenu submenu{ LabelOf(entry), std::move(held), {} };
                    if (content != entry.end())
                        ParseControls(*content, {}, submenu.controls);
                    submenus.push_back(std::move(submenu));
                }
                else if (content != entry.end())
                    ParseSubmenus(*content, held, submenus);
            }
        }

        CustomMenu ParseCustomMenu(const nlohmann::json& entry)
        {
            CustomMenu menu{ LabelOf(entry), ConditionsOf(entry, {}), {} };
            if (const auto content = entry.find("content"); content != entry.end())
                ParseSubmenus(*content, {}, menu.submenus);
            return menu;
        }
    }

    KeyboardDefinition ParseDefinition(const std::vector<uint8_t>& json)
    {
        nlohmann::json document;

        try
        {
            document = nlohmann::json::parse(json.begin(), json.end());
        }
        catch (const nlohmann::json::exception& failure)
        {
            throw ProtocolError(std::string("the definition is not valid JSON: ") + failure.what());
        }

        if (!document.is_object())
            throw ProtocolError("the definition is not a JSON object");

        KeyboardDefinition definition;

        definition.name = ParseName(document);

        if (document.contains("vendorId"))
            definition.vendorId = ParseUsbId(document["vendorId"], "vendorId");

        if (document.contains("productId"))
            definition.productId = ParseUsbId(document["productId"], "productId");

        // The converted form packs both ids into one number, vendorId * 65536 + productId.
        const auto vendorProductId = document.find("vendorProductId");
        if (vendorProductId != document.end() && vendorProductId->is_number_unsigned() &&
            !document.contains("vendorId") && !document.contains("productId"))
        {
            const uint64_t packed = vendorProductId->get<uint64_t>();
            if (packed > 0xFFFFFFFFu)
                throw ProtocolError("definition has an unreadable vendorProductId");

            definition.vendorId  = static_cast<uint16_t>(packed >> 16);
            definition.productId = static_cast<uint16_t>(packed & 0xFFFFu);
        }

        // A preset's name, or VIA V2's object naming the preset it extends --
        // `{ "extends": "qmk_rgblight" }`, as ten of vial-qmk's boards write it (the Corne).
        if (const auto lighting = document.find("lighting"); lighting != document.end())
        {
            if (lighting->is_string())
                definition.lighting = lighting->get<std::string>();
            else if (lighting->is_object() && lighting->contains("extends") && (*lighting)["extends"].is_string())
                definition.lighting = (*lighting)["extends"].get<std::string>();
        }

        for (const auto& [field, ids] : { std::pair{ "keycodes", &definition.keycodeModules },
                                          std::pair{ "menus", &definition.menuIds } })
        {
            const auto list = document.find(field);
            if (list != document.end() && list->is_array())
                for (const nlohmann::json& id : *list)
                    if (id.is_string())
                        ids->push_back(id.get<std::string>());
        }

        if (const auto menus = document.find("menus"); menus != document.end() && menus->is_array())
            for (const nlohmann::json& entry : *menus)
                if (entry.is_object())
                    definition.customMenus.push_back(ParseCustomMenu(entry));

        // An entry that is not an object still takes its place: the order is the numbering.
        if (const auto custom = document.find("customKeycodes"); custom != document.end() && custom->is_array())
            for (const nlohmann::json& entry : *custom)
            {
                KeyboardDefinition::CustomKeycode keycode;
                if (entry.is_object())
                    for (const auto& [field, text] : { std::pair{ "name", &keycode.name },
                                                       std::pair{ "title", &keycode.title },
                                                       std::pair{ "shortName", &keycode.shortName } })
                        if (const auto value = entry.find(field); value != entry.end() && value->is_string())
                            *text = value->get<std::string>();
                definition.customKeycodes.push_back(std::move(keycode));
            }

        const auto matrix = document.find("matrix");
        if (matrix == document.end() || !matrix->is_object())
            throw ProtocolError("the definition has no matrix section");

        definition.matrixRows    = matrix->value("rows", 0);
        definition.matrixColumns = matrix->value("cols", 0);

        if (definition.matrixRows == 0 || definition.matrixColumns == 0)
            throw ProtocolError("the definition declares an empty matrix");

        const auto layouts = document.find("layouts");
        if (layouts == document.end() || !layouts->is_object())
            throw ProtocolError("the definition has no layouts section");

        const auto labels = layouts->find("labels");
        if (labels != layouts->end() && labels->is_array())
        {
            for (const nlohmann::json& label : *labels)
            {
                if (label.is_string())
                    definition.layoutLabels.push_back(label.get<std::string>());
                else
                    definition.layoutLabels.push_back(label.dump());   // nested option group
            }
        }

        const auto keymap   = layouts->find("keymap");
        const auto mainKeys = layouts->find("keys");
        if (keymap != layouts->end() && keymap->is_array())
            ParseSourceKeymap(*keymap, definition.keys);
        else if (mainKeys != layouts->end() && mainKeys->is_array())
            ParseConvertedLayout(*layouts, *mainKeys, definition.keys);
        else
            throw ProtocolError("the definition has no keymap");

        bool hasSwitch = false;
        for (const DefinitionKey& key : definition.keys)
            hasSwitch = hasSwitch || !key.decal;

        if (!hasSwitch)
            throw ProtocolError("the definition describes no keys");

        return definition;
    }
}
