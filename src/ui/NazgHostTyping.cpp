// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgHostTyping.h"

#include <array>

namespace nazg
{
    namespace
    {
        // The basic keycodes a host layout can describe, in the order StrokesOf() tries them: a
        // character on two keys is typed with the first -- KC_BSLS before KC_NUHS, the same key to
        // the OS, and before KC_NUBS; the Brazilian and Japanese extra keys last.
        constexpr std::string_view c_Keys[] = {
            "KC_A", "KC_B", "KC_C", "KC_D", "KC_E", "KC_F", "KC_G", "KC_H", "KC_I", "KC_J", "KC_K", "KC_L", "KC_M",
            "KC_N", "KC_O", "KC_P", "KC_Q", "KC_R", "KC_S", "KC_T", "KC_U", "KC_V", "KC_W", "KC_X", "KC_Y", "KC_Z",
            "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8", "KC_9", "KC_0",
            "KC_SPC", "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_SCLN", "KC_QUOT", "KC_GRV",
            "KC_COMM", "KC_DOT", "KC_SLSH", "KC_NUHS", "KC_NUBS", "KC_INT1", "KC_INT3",
        };

        // One code point, or nothing: legends that are words -- "Spacebar" -- type no character.
        std::optional<char32_t> Single(std::string_view legend)
        {
            const std::u32string text = FromUtf8(legend);
            if (text.size() != 1)
                return std::nullopt;
            return text[0];
        }

        bool IsCased(char32_t c) noexcept { return LowerOf(c) != c; }

        // What a key types at a level, a keycap's capital turned into the case typed there.
        std::optional<char32_t> CharacterAt(const HostLegend* legend, std::string_view key, uint8_t level)
        {
            if (key == "KC_SPC" && level == 0)
                return U' ';

            // A key the layout does not describe types as on US -- the letters, which US lists
            // nowhere since they print the same.
            if (!legend)
            {
                if (key.size() == 4 && key[3] >= 'A' && key[3] <= 'Z' && level <= c_ShiftLevel)
                    return static_cast<char32_t>(level == 0 ? key[3] - 'A' + 'a' : key[3]);
                return std::nullopt;
            }

            const std::array<std::string_view, 4> levels = { legend->plain, legend->shifted, legend->altgr,
                                                             legend->shiftAltgr };
            const std::optional<char32_t> here = Single(levels[level]);

            // A lone capital at an unshifted level: the lower case here, the capital with Shift.
            if ((level & c_ShiftLevel) == 0)
            {
                if (here && IsCased(*here) && levels[level + 1].empty())
                    return LowerOf(*here);
                return here;
            }
            if (levels[level].empty())
            {
                const std::optional<char32_t> below = Single(levels[level - 1]);
                if (below && IsCased(*below))
                    return *below;
            }
            return here;
        }

        bool IsDead(const HostLegend* legend, uint8_t level) noexcept
        {
            return legend && ((legend->dead >> level) & 1u) != 0;
        }

        // A dead key typing `mark`, if the layout has one.
        std::optional<HostStroke> DeadKeyAdding(char32_t mark, const HostLayout& layout)
        {
            for (std::string_view key : c_Keys)
                for (uint8_t level = 0; level < 4; ++level)
                {
                    const HostLegend* legend = layout.Find(key);
                    if (!IsDead(legend, level))
                        continue;
                    const std::optional<char32_t> c = CharacterAt(legend, key, level);
                    if (c && MarkOf(*c) == mark)
                        return HostStroke{ key, level };
                }
            return std::nullopt;
        }

        // The key typing `character` directly, not through a dead key.
        std::optional<HostStroke> DirectStroke(char32_t character, const HostLayout& layout)
        {
            for (uint8_t level = 0; level < 4; ++level)
                for (std::string_view key : c_Keys)
                {
                    const HostLegend* legend = layout.Find(key);
                    if (IsDead(legend, level))
                        continue;
                    const std::optional<char32_t> c = CharacterAt(legend, key, level);
                    if (c && *c == character)
                        return HostStroke{ key, level };
                }
            return std::nullopt;
        }
    }

    std::optional<Typed> TypedBy(HostStroke stroke, const HostLayout& layout)
    {
        const HostLegend* legend = layout.Find(stroke.key);
        const std::optional<char32_t> c = CharacterAt(legend, stroke.key, stroke.level);
        if (!c)
            return std::nullopt;
        return Typed{ *c, IsDead(legend, stroke.level) };
    }

    std::optional<std::vector<HostStroke>> StrokesOf(char32_t character, const HostLayout& layout)
    {
        if (const std::optional<HostStroke> direct = DirectStroke(character, layout))
            return std::vector<HostStroke>{ *direct };

        for (const Composition& composition : Compositions())
            if (composition.composed == character)
            {
                const std::optional<HostStroke> dead = DeadKeyAdding(composition.mark, layout);
                const std::optional<HostStroke> base = DirectStroke(composition.base, layout);
                if (dead && base)
                    return std::vector<HostStroke>{ *dead, *base };
                break;
            }

        for (std::string_view key : c_Keys)
            for (uint8_t level = 0; level < 4; ++level)
            {
                const HostLegend* legend = layout.Find(key);
                if (!IsDead(legend, level))
                    continue;
                const std::optional<char32_t> c = CharacterAt(legend, key, level);
                if (c && AloneOf(*c, layout) == character)
                    return std::vector<HostStroke>{ { key, level }, { "KC_SPC", 0 } };
            }
        return std::nullopt;
    }

    void HostTypist::Type(HostStroke stroke)
    {
        const std::optional<Typed> typed = TypedBy(stroke, m_Layout);
        if (!typed)
            return;

        if (!m_Pending)
        {
            if (typed->dead)
                m_Pending = typed->character;
            else
                m_Text += typed->character;
            return;
        }

        const char32_t accent = *m_Pending;
        m_Pending.reset();
        if (typed->character == U' ')
        {
            Add(AloneOf(accent, m_Layout));
            return;
        }
        for (const Composition& composition : Compositions())
            if (composition.base == typed->character && composition.mark == MarkOf(accent))
            {
                m_Text += composition.composed;
                return;
            }
        // No composition: the accent, then the key -- itself pending again when dead.
        Add(AloneOf(accent, m_Layout));
        if (typed->dead)
            m_Pending = typed->character;
        else
            m_Text += typed->character;
    }

    std::u32string HostTypist::Finish()
    {
        if (m_Pending)
            Add(AloneOf(*m_Pending, m_Layout));
        m_Pending.reset();
        return std::move(m_Text);
    }

    char32_t AloneOf(char32_t deadCharacter, const HostLayout& layout) noexcept
    {
        // Written by hand: QMK labels these two keys by the accent they add.
        if (layout.id == "us_international" || layout.id == "us_international_linux")
        {
            if (deadCharacter == U'´')
                return U'\'';
            if (deadCharacter == U'¨')
                return U'"';
        }
        // A combining mark has no character of its own to type.
        if (deadCharacter >= 0x0300 && deadCharacter <= 0x036F)
            return 0;
        return deadCharacter;
    }

    char32_t MarkOf(char32_t deadCharacter) noexcept
    {
        switch (deadCharacter)
        {
        case U'`':      return 0x0300;   // grave
        case U'´': return 0x0301;   // acute
        case U'^':      return 0x0302;   // circumflex
        case U'~':      return 0x0303;   // tilde
        case U'¯': return 0x0304;   // macron
        case U'˘': return 0x0306;   // breve
        case U'˙': return 0x0307;   // dot above
        case U'¨': return 0x0308;   // diaeresis
        case U'°':
        case U'˚': return 0x030A;   // ring above
        case U'˝': return 0x030B;   // double acute
        case U'ˇ': return 0x030C;   // caron
        case U'¸': return 0x0327;   // cedilla
        case U'˛': return 0x0328;   // ogonek
        case U',':      return 0x0326;   // comma below, bépo's dead comma
        }
        // A combining mark given as itself: bépo's horn, dot below, hook above.
        if (deadCharacter >= 0x0300 && deadCharacter <= 0x036F)
            return deadCharacter;
        return 0;
    }

    char32_t LowerOf(char32_t c) noexcept
    {
        if (c >= U'A' && c <= U'Z')
            return c + 0x20;
        if ((c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) || (c >= 0x0391 && c <= 0x03AB && c != 0x03A2) ||
            (c >= 0x0410 && c <= 0x042F))
            return c + 0x20;
        if (c >= 0x0400 && c <= 0x040F)
            return c + 0x50;
        if (c == 0x0178)
            return 0x00FF;
        if (c == 0x0130)
            return U'i';
        // Latin Extended-A pairs a capital with the small letter after it -- on even code points,
        // or odd ones from U+0139 to U+0148 and U+0179 to U+017E.
        if (c >= 0x0100 && c <= 0x017F)
        {
            const bool oddRun = (c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E);
            if (c == 0x0138 || c == 0x0149 || c == 0x017F)
                return c;
            return (c % 2 == 1) == oddRun ? c + 1 : c;
        }
        // Cyrillic beyond the basic set: Ґ, Ѓ ... pairs from U+0460 on.
        if (c >= 0x0460 && c <= 0x04FF && c % 2 == 0)
            return c + 1;
        return c;
    }

    std::u32string FromUtf8(std::string_view text)
    {
        std::u32string out;
        for (size_t at = 0; at < text.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(text[at]);
            const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
            if (length == 0 || at + length > text.size())
            {
                out += U'�';
                ++at;
                continue;
            }
            char32_t c = length == 1 ? lead : lead & (0x7F >> length);
            bool valid = true;
            for (size_t i = 1; i < length; ++i)
            {
                const unsigned char next = static_cast<unsigned char>(text[at + i]);
                valid = valid && (next & 0xC0) == 0x80;
                c = (c << 6) | (next & 0x3F);
            }
            out += valid ? c : U'�';
            at += valid ? length : 1;
        }
        return out;
    }

    std::string ToUtf8(std::u32string_view text)
    {
        std::string out;
        for (char32_t c : text)
        {
            if (c < 0x80)
                out += static_cast<char>(c);
            else if (c < 0x800)
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += static_cast<char>(0xE0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }
}
