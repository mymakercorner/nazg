// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HostTyping - text as the computer types it, with its host layout (ui-design.md, "The Macros
// section": "Text is typed the way the computer types it"). A character becomes keystrokes, and
// keystrokes read back as text:
//
//   1. on a key directly, at one of its four levels -- plain, Shift, AltGr, Shift+AltGr (Option
//      on the Mac layouts);
//   2. else its accent's dead key, then its base letter -- Unicode's decomposition says which
//      (Compositions(), ui/NazgComposeTable.cpp): e is e and a circumflex, ^ then E;
//   3. else, for a dead key's own character, the dead key then Space.
//
// From the host layout table (ui/NazgKeycapLegend.h), whose legends are a keycap's: a letter's one
// legend is its capital, so a lone cased letter at a level is the lower case there and the capital
// one level up. The table's dead masks say which keys are dead.
//
// Pure, in nazg_core: no device, no keycode values -- a key is its basic keycode's name.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // One keystroke: a basic keycode, at a level -- bit 0 Shift, bit 1 AltGr.
    struct HostStroke
    {
        std::string_view key;
        uint8_t          level = 0;

        bool operator==(const HostStroke&) const = default;
    };

    inline constexpr uint8_t c_ShiftLevel = 1;
    inline constexpr uint8_t c_AltGrLevel = 2;

    // A letter Unicode decomposes into a base letter and one combining accent.
    struct Composition
    {
        char32_t composed;
        char32_t base;
        char32_t mark;
    };

    [[nodiscard]] std::span<const Composition> Compositions() noexcept;

    // What a key types at a level: the character, and whether the key is dead there -- it then
    // types nothing until the next key. Nothing when the key types nothing at that level.
    struct Typed
    {
        char32_t character = 0;
        bool     dead      = false;
    };

    [[nodiscard]] std::optional<Typed> TypedBy(HostStroke stroke, const HostLayout& layout);

    // The keystrokes that type `character`, or nothing when this layout cannot.
    [[nodiscard]] std::optional<std::vector<HostStroke>> StrokesOf(char32_t character, const HostLayout& layout);

    // What a run of keystrokes types, dead keys composing with the key after them -- a dead key
    // then Space types its own character; then a letter, the composed letter, or both characters
    // when they do not compose, as Windows does. A keystroke that types nothing is skipped.
    class HostTypist
    {
    public:
        explicit HostTypist(const HostLayout& layout) : m_Layout(layout) {}

        void Type(HostStroke stroke);

        // The text so far, a dead key left pending typed alone.
        [[nodiscard]] std::u32string Finish();

    private:
        // A character typed -- none for a dead key that types nothing alone.
        void Add(char32_t character)
        {
            if (character != 0)
                m_Text += character;
        }

        const HostLayout&       m_Layout;
        std::u32string          m_Text;
        std::optional<char32_t> m_Pending;   // a dead key's character
    };

    // The character a dead key types before Space -- its own, except where QMK's label names the
    // accent it adds rather than the key: US International's ' key adds an acute but types '.
    [[nodiscard]] char32_t AloneOf(char32_t deadCharacter, const HostLayout& layout) noexcept;

    // The combining mark a dead key's character adds, or 0 when it adds none Unicode composes.
    [[nodiscard]] char32_t MarkOf(char32_t deadCharacter) noexcept;

    // Lower case, for the letters a host layout prints as capitals: Latin, Greek, Cyrillic.
    [[nodiscard]] char32_t LowerOf(char32_t character) noexcept;

    // UTF-8 and back. An invalid sequence reads as U+FFFD.
    [[nodiscard]] std::u32string FromUtf8(std::string_view text);
    [[nodiscard]] std::string    ToUtf8(std::u32string_view text);
}
