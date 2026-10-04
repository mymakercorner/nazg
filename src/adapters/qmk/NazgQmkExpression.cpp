// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgQmkExpression.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <variant>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"

namespace nazg
{
    namespace
    {
        std::string Upper(std::string_view text)
        {
            std::string upper(text);
            for (char& c : upper)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return upper;
        }

        // A name of the version's table, in any case; the NamedKey views the table's own name.
        std::optional<std::string_view> FindName(std::string_view word, QmkKeycodeVersion version)
        {
            // The keymaps' spellings of the two keys that are not keys.
            const std::string upper = Upper(word);
            if (upper == "_______" || upper == "KC_TRANSPARENT")
                return FindName("KC_TRNS", version);
            if (upper == "XXXXXXX")
                return FindName("KC_NO", version);

            for (const QmkKeycode& row : QmkKeycodeTable())
                if (row.ExistsIn(version) && Upper(row.name) == upper)
                    return std::string_view(row.name);
            return std::nullopt;
        }

        // QMK's modifier wrappers, their short forms and their combinations: LCTL(kc), C(kc), HYPR(kc).
        constexpr uint8_t c_Hyper = Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt | Mod::LeftGui;
        constexpr uint8_t c_Meh   = Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt;

        std::optional<uint8_t> WrapperMods(std::string_view name)
        {
            const struct
            {
                std::string_view name;
                uint8_t          mods;
            } c_Wrappers[] = {
                { "LCTL", Mod::LeftCtrl },  { "C", Mod::LeftCtrl },     { "RCTL", Mod::RightCtrl },
                { "LSFT", Mod::LeftShift }, { "S", Mod::LeftShift },    { "RSFT", Mod::RightShift },
                { "LALT", Mod::LeftAlt },   { "A", Mod::LeftAlt },      { "LOPT", Mod::LeftAlt },
                { "RALT", Mod::RightAlt },  { "ALGR", Mod::RightAlt },  { "ROPT", Mod::RightAlt },
                { "LGUI", Mod::LeftGui },   { "G", Mod::LeftGui },      { "LCMD", Mod::LeftGui },
                { "LWIN", Mod::LeftGui },   { "RGUI", Mod::RightGui },  { "RCMD", Mod::RightGui },
                { "RWIN", Mod::RightGui },  { "HYPR", c_Hyper },        { "MEH", c_Meh },
                { "SGUI", Mod::LeftShift | Mod::LeftGui },
            };
            for (const auto& wrapper : c_Wrappers)
                if (wrapper.name == name)
                    return wrapper.mods;
            return std::nullopt;
        }

        // The mod-tap shorthands: LCTL_T(kc) is MT(MOD_LCTL, kc).
        std::optional<uint8_t> ModTapMods(std::string_view name)
        {
            if (name.size() < 3 || name.substr(name.size() - 2) != "_T")
                return std::nullopt;

            const std::string_view base = name.substr(0, name.size() - 2);
            if (base == "CTL")
                return Mod::LeftCtrl;
            if (base == "SFT")
                return Mod::LeftShift;
            if (base == "ALT" || base == "OPT")
                return Mod::LeftAlt;
            if (base == "GUI" || base == "CMD" || base == "WIN")
                return Mod::LeftGui;
            if (base == "ALL")
                return c_Hyper;
            if (base == "SH")
                return std::nullopt;   // SH_T is swap hands, not a modifier
            return WrapperMods(base);
        }

        class Parser
        {
        public:
            Parser(std::string_view text, QmkKeycodeVersion version) : m_Version(version)
            {
                for (char c : text)
                    if (!std::isspace(static_cast<unsigned char>(c)))
                        m_Text += c;
            }

            std::optional<Keycode> Whole()
            {
                std::optional<Keycode> keycode = Expression();
                return keycode && m_At == m_Text.size() ? keycode : std::nullopt;
            }

        private:
            bool Eat(char c)
            {
                if (m_At < m_Text.size() && m_Text[m_At] == c)
                {
                    ++m_At;
                    return true;
                }
                return false;
            }

            std::string_view Word()
            {
                const size_t start = m_At;
                while (m_At < m_Text.size() &&
                       (std::isalnum(static_cast<unsigned char>(m_Text[m_At])) || m_Text[m_At] == '_'))
                    ++m_At;
                return std::string_view(m_Text).substr(start, m_At - start);
            }

            // A decimal number, or 0x hex: a layer, an index, a raw value.
            static std::optional<uint32_t> Number(std::string_view word)
            {
                if (word.empty())
                    return std::nullopt;

                const bool hex  = word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X');
                uint32_t   value = 0;
                for (char c : hex ? word.substr(2) : word)
                {
                    const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                    : hex && std::isxdigit(static_cast<unsigned char>(c))
                                        ? std::toupper(static_cast<unsigned char>(c)) - 'A' + 10
                                        : -1;
                    if (digit < 0)
                        return std::nullopt;
                    value = value * (hex ? 16u : 10u) + static_cast<uint32_t>(digit);
                    if (value > 0xFFFF)
                        return std::nullopt;
                }
                return value;
            }

            std::optional<uint8_t> Small()
            {
                const std::optional<uint32_t> value = Number(Word());
                if (!value || *value > 0xFF)
                    return std::nullopt;
                return static_cast<uint8_t>(*value);
            }

            // MOD_LCTL|MOD_LSFT, or MOD_HYPR, MOD_MEH.
            std::optional<uint8_t> ModFlags()
            {
                uint8_t mods = 0;
                do
                {
                    const std::string flag = Upper(Word());
                    if (flag.substr(0, 4) != "MOD_")
                        return std::nullopt;

                    const std::string_view name = std::string_view(flag).substr(4);
                    if (name == "HYPR")
                        mods |= c_Hyper;
                    else if (name == "MEH")
                        mods |= c_Meh;
                    else if (const std::optional<uint8_t> one = WrapperMods(name); one && name.size() == 4)
                        mods |= *one;
                    else
                        return std::nullopt;
                } while (Eat('|'));
                return mods == 0 ? std::nullopt : std::optional<uint8_t>(mods);
            }

            // An argument that must be a plain key: the tap of LT and MT, what a wrapper modifies.
            std::optional<std::string_view> Key()
            {
                const std::optional<Keycode> keycode = Expression();
                if (!keycode)
                    return std::nullopt;
                if (const auto* named = std::get_if<NamedKey>(&*keycode))
                    return named->name;
                return std::nullopt;
            }

            std::optional<Keycode> Expression()
            {
                const std::string_view word = Word();
                if (word.empty())
                    return std::nullopt;

                if (!Eat('('))
                    return Plain(word);

                std::optional<Keycode> keycode = Call(Upper(word));
                if (!keycode || !Eat(')'))
                    return std::nullopt;
                return keycode;
            }

            // A name, a macro, or a raw value.
            std::optional<Keycode> Plain(std::string_view word)
            {
                // Before the names: the table names the macro rows too, but a macro is a MacroKey,
                // as the decoder makes it -- its index, whatever the numbering.
                const std::string upper = Upper(word);
                if (upper.substr(0, 3) == "MC_")
                    if (const std::optional<uint32_t> index = Number(std::string_view(upper).substr(3)); index && *index <= 0xFF)
                        return MacroKey{ static_cast<uint8_t>(*index) };

                if (const std::optional<std::string_view> name = FindName(word, m_Version))
                    return NamedKey{ *name };
                if (upper.substr(0, 2) == "0X")
                    if (const std::optional<uint32_t> value = Number(word))
                        return DecodeQmkKeycode(static_cast<uint16_t>(*value), m_Version);
                return std::nullopt;
            }

            // What follows "name(" up to, not including, the ")".
            std::optional<Keycode> Call(const std::string& name)
            {
                const struct
                {
                    std::string_view name;
                    LayerOp          op;
                } c_LayerOps[] = {
                    { "MO", LayerOp::Momentary }, { "TG", LayerOp::Toggle },     { "TO", LayerOp::To },
                    { "DF", LayerOp::Default },   { "PDF", LayerOp::PersistentDefault }, { "OSL", LayerOp::OneShot },
                    { "TT", LayerOp::TapToggle },
                };
                for (const auto& layerOp : c_LayerOps)
                    if (layerOp.name == name)
                    {
                        const std::optional<uint8_t> layer = Small();
                        return layer ? std::optional<Keycode>(LayerKey{ layerOp.op, *layer }) : std::nullopt;
                    }

                if (name == "LT")
                {
                    const std::optional<uint8_t> layer = Small();
                    if (!layer || !Eat(','))
                        return std::nullopt;
                    const std::optional<std::string_view> key = Key();
                    return key ? std::optional<Keycode>(LayerTapKey{ *layer, *key }) : std::nullopt;
                }
                if (name == "MT")
                {
                    const std::optional<uint8_t> mods = ModFlags();
                    if (!mods || !Eat(','))
                        return std::nullopt;
                    const std::optional<std::string_view> key = Key();
                    return key ? std::optional<Keycode>(ModTapKey{ *mods, *key }) : std::nullopt;
                }
                if (name == "LM")
                {
                    const std::optional<uint8_t> layer = Small();
                    if (!layer || !Eat(','))
                        return std::nullopt;
                    const std::optional<uint8_t> mods = ModFlags();
                    return mods ? std::optional<Keycode>(LayerModKey{ *layer, *mods }) : std::nullopt;
                }
                if (name == "OSM")
                {
                    const std::optional<uint8_t> mods = ModFlags();
                    return mods ? std::optional<Keycode>(OneShotModKey{ *mods }) : std::nullopt;
                }
                if (name == "TD")
                {
                    const std::optional<uint8_t> index = Small();
                    return index ? std::optional<Keycode>(TapDanceKey{ *index }) : std::nullopt;
                }
                if (name == "SH_T")
                {
                    const std::optional<std::string_view> key = Key();
                    return key ? std::optional<Keycode>(SwapHandsTapKey{ *key }) : std::nullopt;
                }
                if (const std::optional<uint8_t> mods = ModTapMods(name))
                {
                    const std::optional<std::string_view> key = Key();
                    return key ? std::optional<Keycode>(ModTapKey{ *mods, *key }) : std::nullopt;
                }

                // A wrapper, nested as deep as QMK's: LCTL(LSFT(KC_A)) gathers both.
                if (const std::optional<uint8_t> mods = WrapperMods(name))
                {
                    const std::optional<Keycode> inner = Expression();
                    if (!inner)
                        return std::nullopt;
                    if (const auto* named = std::get_if<NamedKey>(&*inner))
                        return ModifiedKey{ *mods, named->name };
                    if (const auto* modified = std::get_if<ModifiedKey>(&*inner))
                        return ModifiedKey{ static_cast<uint8_t>(*mods | modified->mods), modified->key };
                    return std::nullopt;
                }

                return std::nullopt;
            }

            std::string       m_Text;
            size_t            m_At = 0;
            QmkKeycodeVersion m_Version;
        };
    }

    std::optional<Keycode> ParseQmkExpression(std::string_view text, QmkKeycodeVersion version)
    {
        return Parser(text, version).Whole();
    }
}
