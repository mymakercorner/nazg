// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMacroSteps.h"

#include <map>
#include <optional>
#include <utility>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "ui/NazgHostTyping.h"

namespace nazg
{
    namespace
    {
        using Kind = MacroAction::Kind;

        constexpr uint32_t c_LeftShift = 0xE1;   // KC_LSFT
        constexpr uint32_t c_RightAlt  = 0xE6;   // KC_RALT

        // The firmware's US table, both ways: the printable character on a key at a level, and the
        // key of a character. Built from US as typing reads it -- a character on two keys is the
        // first's, KC_BSLS's '\' rather than KC_NUBS's, as the firmware's table has it.
        struct UsTable
        {
            std::map<std::pair<std::string_view, uint8_t>, char> byteOf;
            std::map<char, HostStroke>                           strokeOf;

            UsTable()
            {
                for (char c = 0x20; c < 0x7F; ++c)
                {
                    const std::optional<std::vector<HostStroke>> strokes = StrokesOf(static_cast<char32_t>(c), UsHostLayout());
                    if (strokes && strokes->size() == 1)
                    {
                        byteOf[{ (*strokes)[0].key, (*strokes)[0].level }] = c;
                        strokeOf[c]                                       = (*strokes)[0];
                    }
                }
            }
        };

        const UsTable& Us()
        {
            static const UsTable table;
            return table;
        }

        // The control characters the firmware's table types as keys.
        std::optional<std::string_view> ControlKey(uint32_t byte)
        {
            switch (byte)
            {
            case '\b': return "KC_BSPC";
            case '\t': return "KC_TAB";
            case '\n': return "KC_ENT";
            case 0x1B: return "KC_ESC";
            case 0x7F: return "KC_DEL";
            }
            return std::nullopt;
        }

        // A modifier keycode's bit in a ModifiedKey: KC_LCTL is 0xE0, Mod::LeftCtrl 0x01, and so on.
        std::optional<uint8_t> ModOfKeycode(uint32_t value)
        {
            if (value >= 0xE0 && value <= 0xE7)
                return static_cast<uint8_t>(1u << (value - 0xE0));
            return std::nullopt;
        }

        uint8_t ModsOfLevel(uint8_t level)
        {
            // QMK's 16-bit modified keycodes take left or right modifiers, not both: Shift+AltGr is
            // Right Shift with Right Alt.
            switch (level)
            {
            case 1:  return Mod::LeftShift;
            case 2:  return Mod::RightAlt;
            case 3:  return Mod::RightAlt | Mod::RightShift;
            default: return 0;
            }
        }

        std::optional<uint8_t> LevelOfMods(uint8_t mods)
        {
            if (mods == Mod::LeftShift || mods == Mod::RightShift)
                return 1;
            if (mods == Mod::RightAlt)
                return 2;
            if (mods == (Mod::RightAlt | Mod::RightShift) || mods == (Mod::RightAlt | Mod::LeftShift))
                return 3;
            return std::nullopt;
        }

        std::optional<uint16_t> ValueOf(std::string_view name, QmkKeycodeVersion version)
        {
            const QmkKeycode* found = FindQmkKeycodeByName(name, version);
            if (!found)
                return std::nullopt;
            return found->value;
        }

        // Steps being read: text keystrokes gathered until a step of another kind ends them.
        class Reader
        {
        public:
            explicit Reader(const MacroContext& context) : m_Typist(context.host) {}

            void Type(HostStroke stroke)
            {
                m_Typist.Type(stroke);
                m_HasText = true;
            }

            void Add(MacroStep step)
            {
                Flush();
                m_Steps.push_back(std::move(step));
            }

            Macro Finish()
            {
                Flush();
                return std::move(m_Steps);
            }

        private:
            void Flush()
            {
                if (!m_HasText)
                    return;
                std::string text = ToUtf8(m_Typist.Finish());
                if (!text.empty())
                    m_Steps.push_back({ MacroStep::Kind::Text, std::move(text) });
                m_HasText = false;
            }

            HostTypist          m_Typist;
            bool                m_HasText = false;
            Macro               m_Steps;
        };

        // A keystroke written as an action rather than a character -- what reading takes back as
        // text: a key outside the US table, or one at a level the table does not reach.
        std::optional<HostStroke> TextStroke(const Keycode& keycode, const MacroContext& context)
        {
            std::optional<HostStroke> stroke;
            if (const NamedKey* named = std::get_if<NamedKey>(&keycode))
                stroke = HostStroke{ named->name, 0 };
            else if (const ModifiedKey* modified = std::get_if<ModifiedKey>(&keycode))
                if (const std::optional<uint8_t> level = LevelOfMods(modified->mods))
                    stroke = HostStroke{ modified->key, *level };

            if (!stroke || (stroke->level <= 1 && Us().byteOf.count({ stroke->key, stroke->level })))
                return std::nullopt;
            const std::optional<Typed> typed = TypedBy(*stroke, context.host);
            if (!typed)
                return std::nullopt;
            return stroke;
        }

        bool IsAction(const MacroActions& actions, size_t at, Kind kind, uint32_t value)
        {
            return at < actions.size() && actions[at].kind == kind && actions[at].value == value;
        }

        bool IsAction(const MacroActions& actions, size_t at, Kind kind)
        {
            return at < actions.size() && actions[at].kind == kind;
        }

        // Right Alt pressed around taps -- with Shift pressed around some -- the way AltGr text is
        // written where keys are 8-bit. The index after it, or nothing.
        std::optional<size_t> ReadAltGrRun(const MacroActions& actions, size_t at, const MacroContext& context,
                                           std::vector<HostStroke>& strokes)
        {
            if (!IsAction(actions, at, Kind::Press, c_RightAlt))
                return std::nullopt;
            size_t next = at + 1;
            for (;;)
            {
                if (IsAction(actions, next, Kind::Tap))
                {
                    const Keycode key = DecodeQmkKeycode(static_cast<uint16_t>(actions[next].value), context.version);
                    const NamedKey* named = std::get_if<NamedKey>(&key);
                    if (!named || !TypedBy({ named->name, 2 }, context.host))
                        return std::nullopt;
                    strokes.push_back({ named->name, 2 });
                    next += 1;
                }
                else if (IsAction(actions, next, Kind::Press, c_LeftShift) && IsAction(actions, next + 1, Kind::Tap) &&
                         IsAction(actions, next + 2, Kind::Release, c_LeftShift))
                {
                    const Keycode key = DecodeQmkKeycode(static_cast<uint16_t>(actions[next + 1].value), context.version);
                    const NamedKey* named = std::get_if<NamedKey>(&key);
                    if (!named || !TypedBy({ named->name, 3 }, context.host))
                        return std::nullopt;
                    strokes.push_back({ named->name, 3 });
                    next += 3;
                }
                else
                {
                    break;
                }
            }
            if (strokes.empty() || !IsAction(actions, next, Kind::Release, c_RightAlt))
                return std::nullopt;
            return next + 1;
        }

        // Modifiers pressed, one key tapped -- or pressed and released -- the modifiers released in
        // reverse: VIA's chord, and how a key sent with modifiers is written where keys are 8-bit.
        std::optional<size_t> ReadChord(const MacroActions& actions, size_t at, const MacroContext& context,
                                        Keycode& keycode)
        {
            std::vector<uint32_t> held;
            size_t next = at;
            while (IsAction(actions, next, Kind::Press) && ModOfKeycode(actions[next].value))
                held.push_back(actions[next++].value);
            if (held.empty())
                return std::nullopt;

            uint32_t key = 0;
            if (IsAction(actions, next, Kind::Tap))
            {
                key = actions[next].value;
                next += 1;
            }
            else if (IsAction(actions, next, Kind::Press) && IsAction(actions, next + 1, Kind::Release, actions[next].value))
            {
                key = actions[next].value;
                next += 2;
            }
            else
            {
                return std::nullopt;
            }

            uint8_t mods = 0;
            for (size_t index = held.size(); index-- > 0;)
            {
                if (!IsAction(actions, next, Kind::Release, held[index]))
                    return std::nullopt;
                mods = static_cast<uint8_t>(mods | *ModOfKeycode(held[index]));
                ++next;
            }

            const Keycode decoded = DecodeQmkKeycode(static_cast<uint16_t>(key), context.version);
            const NamedKey* named = std::get_if<NamedKey>(&decoded);
            if (!named || key > 0xFF)
                return std::nullopt;
            keycode = ModifiedKey{ mods, named->name };
            return next;
        }

        // Writing: one keystroke of text.
        bool WriteStroke(HostStroke stroke, const MacroContext& context, MacroActions& actions)
        {
            if (stroke.level <= 1)
            {
                const auto byte = Us().byteOf.find({ stroke.key, stroke.level });
                if (byte != Us().byteOf.end())
                {
                    actions.push_back({ Kind::Character, static_cast<uint8_t>(byte->second) });
                    return true;
                }
            }

            const std::optional<uint16_t> key = ValueOf(stroke.key, context.version);
            if (!key)
                return false;
            if (stroke.level == 0)
            {
                actions.push_back({ Kind::Tap, *key });
                return true;
            }
            if (HoldsAnyKeycode(context.format))
            {
                const std::optional<uint16_t> value = EncodeQmkKeycode(ModifiedKey{ ModsOfLevel(stroke.level), stroke.key },
                                                                       context.version);
                if (!value)
                    return false;
                actions.push_back({ Kind::Tap, *value });
                return true;
            }
            if (stroke.level & c_AltGrLevel)
                actions.push_back({ Kind::Press, c_RightAlt });
            if (stroke.level & c_ShiftLevel)
                actions.push_back({ Kind::Press, c_LeftShift });
            actions.push_back({ Kind::Tap, *key });
            if (stroke.level & c_ShiftLevel)
                actions.push_back({ Kind::Release, c_LeftShift });
            if (stroke.level & c_AltGrLevel)
                actions.push_back({ Kind::Release, c_RightAlt });
            return true;
        }

        std::string Named(const Keycode& keycode)
        {
            return FormatKeycode(keycode);
        }
    }

    Macro StepsOf(const MacroActions& actions, const MacroContext& context)
    {
        Reader reader(context);
        for (size_t at = 0; at < actions.size();)
        {
            const MacroAction& action = actions[at];
            if (action.kind == Kind::Character)
            {
                if (const std::optional<std::string_view> control = ControlKey(action.value))
                    reader.Add({ MacroStep::Kind::Key, {}, NamedKey{ *control } });
                else if (const auto stroke = Us().strokeOf.find(static_cast<char>(action.value)); stroke != Us().strokeOf.end())
                    reader.Type(stroke->second);
                ++at;
                continue;
            }
            if (action.kind == Kind::Wait)
            {
                reader.Add({ MacroStep::Kind::Wait, {}, NamedKey{ "KC_NO" }, action.value });
                ++at;
                continue;
            }

            const Keycode keycode = DecodeQmkKeycode(static_cast<uint16_t>(action.value), context.version);
            if (action.kind == Kind::Tap)
            {
                if (const std::optional<HostStroke> stroke = TextStroke(keycode, context))
                    reader.Type(*stroke);
                else
                    reader.Add({ MacroStep::Kind::Key, {}, keycode });
                ++at;
                continue;
            }
            if (action.kind == Kind::Press)
            {
                std::vector<HostStroke> strokes;
                if (const std::optional<size_t> next = ReadAltGrRun(actions, at, context, strokes))
                {
                    for (HostStroke stroke : strokes)
                        reader.Type(stroke);
                    at = *next;
                    continue;
                }
                // Shift around one key the US table does not reach: a Shift keystroke of text.
                if (action.value == c_LeftShift && IsAction(actions, at + 1, Kind::Tap) &&
                    IsAction(actions, at + 2, Kind::Release, c_LeftShift))
                {
                    const Keycode key = DecodeQmkKeycode(static_cast<uint16_t>(actions[at + 1].value), context.version);
                    if (const std::optional<HostStroke> stroke =
                            TextStroke(std::holds_alternative<NamedKey>(key)
                                           ? Keycode{ ModifiedKey{ Mod::LeftShift, std::get<NamedKey>(key).name } }
                                           : key,
                                       context))
                    {
                        reader.Type(*stroke);
                        at += 3;
                        continue;
                    }
                }
                Keycode chord;
                if (const std::optional<size_t> next = ReadChord(actions, at, context, chord))
                {
                    reader.Add({ MacroStep::Kind::Key, {}, chord });
                    at = *next;
                    continue;
                }
                reader.Add({ MacroStep::Kind::Press, {}, keycode });
                ++at;
                continue;
            }
            reader.Add({ MacroStep::Kind::Release, {}, keycode });
            ++at;
        }
        return reader.Finish();
    }

    MacroWriting ActionsOf(const Macro& macro, const MacroContext& context)
    {
        MacroWriting writing;
        const bool anyKeycode = HoldsAnyKeycode(context.format);
        for (size_t index = 0; index < macro.size(); ++index)
        {
            const MacroStep& step = macro[index];
            const auto problem = [&](std::string what) { writing.problems.push_back({ index, std::move(what) }); };

            switch (step.kind)
            {
            case MacroStep::Kind::Text:
            {
                std::u32string missing;
                for (char32_t c : FromUtf8(step.text))
                {
                    const std::optional<std::vector<HostStroke>> strokes = StrokesOf(c, context.host);
                    bool written = strokes.has_value();
                    if (strokes)
                        for (HostStroke stroke : *strokes)
                            written = WriteStroke(stroke, context, writing.actions) && written;
                    if (!written && missing.find(c) == std::u32string::npos)
                        missing += c;
                }
                if (!missing.empty())
                    problem(ToUtf8(missing) + " cannot be typed with the " + std::string(context.host.name) + " layout");
                break;
            }

            case MacroStep::Kind::Key:
            {
                const std::optional<uint16_t> value = EncodeQmkKeycode(step.key, context.version);
                if (value && (*value <= 0xFF || anyKeycode))
                {
                    writing.actions.push_back({ Kind::Tap, *value });
                    break;
                }
                // Sent with modifiers, where keys are 8-bit: the chord.
                const ModifiedKey* modified = std::get_if<ModifiedKey>(&step.key);
                const std::optional<uint16_t> key = modified ? ValueOf(modified->key, context.version) : std::nullopt;
                if (!modified || !key || *key > 0xFF)
                {
                    problem(Named(step.key) + " is not a basic key: this board's macros hold basic keys only");
                    break;
                }
                std::vector<uint32_t> held;
                for (uint32_t mod = 0; mod < 8; ++mod)
                    if (modified->mods & (1u << mod))
                        held.push_back(0xE0 + mod);
                for (uint32_t mod : held)
                    writing.actions.push_back({ Kind::Press, mod });
                writing.actions.push_back({ Kind::Tap, *key });
                for (size_t at = held.size(); at-- > 0;)
                    writing.actions.push_back({ Kind::Release, held[at] });
                break;
            }

            case MacroStep::Kind::Press:
            case MacroStep::Kind::Release:
            {
                const std::optional<uint16_t> value = EncodeQmkKeycode(step.key, context.version);
                if (!value || (*value > 0xFF && !anyKeycode))
                {
                    problem(Named(step.key) + " is not a basic key: this board's macros hold basic keys only");
                    break;
                }
                writing.actions.push_back({ step.kind == MacroStep::Kind::Press ? Kind::Press : Kind::Release, *value });
                break;
            }

            case MacroStep::Kind::Wait:
                if (!HasWaits(context.format))
                    problem("this board's firmware has no waits");
                else if (step.milliseconds > LongestWait(context.format))
                    problem("waits go up to " + std::to_string(LongestWait(context.format)) + " ms on this board");
                else
                    writing.actions.push_back({ Kind::Wait, step.milliseconds });
                break;
            }
        }
        return writing;
    }

    std::string ViaScriptOf(const Macro& macro)
    {
        static constexpr const char* c_ModNames[] = { "KC_LCTL", "KC_LSFT", "KC_LALT", "KC_LGUI",
                                                      "KC_RCTL", "KC_RSFT", "KC_RALT", "KC_RGUI" };
        std::string script;
        for (const MacroStep& step : macro)
        {
            switch (step.kind)
            {
            case MacroStep::Kind::Text:
                for (char c : step.text)
                    script += c == '{' ? std::string("\\{") : std::string(1, c);
                break;
            case MacroStep::Kind::Key:
                script += '{';
                if (const ModifiedKey* modified = std::get_if<ModifiedKey>(&step.key))
                {
                    for (uint8_t mod = 0; mod < 8; ++mod)
                        if (modified->mods & (1u << mod))
                            script += std::string(c_ModNames[mod]) + ",";
                    script += std::string(modified->key);
                }
                else
                {
                    script += FormatKeycode(step.key);
                }
                script += '}';
                break;
            case MacroStep::Kind::Press:
            case MacroStep::Kind::Release:
                script += std::string("{") + (step.kind == MacroStep::Kind::Press ? "+" : "-") + FormatKeycode(step.key) + "}";
                break;
            case MacroStep::Kind::Wait:
                script += "{" + std::to_string(step.milliseconds) + "}";
                break;
            }
        }
        return script;
    }

    size_t BytesOf(const MacroActions& actions, MacroFormat format) noexcept
    {
        size_t bytes = 1;
        for (const MacroAction& action : actions)
            bytes += ActionSize(action, format).value_or(0);
        return bytes;
    }
}
