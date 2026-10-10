// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// AltRepeatKey - what Alt Repeat sends after a given last key: after Ctrl+Z, Ctrl+Y (ui-design.md,
// "The Alt Repeat Key section"). Vial's, one slot of its table; protocol-neutral here -- the
// section turns it into the board's entry and back.
//
// What the firmware does with it is vial-qmk's get_alt_repeat_key_keycode_user() (quantum/vial.c)
// over QMK's repeat_key.c, read 2026-10-10: the last key is matched as its base key, the modifiers
// in its keycode required, the allowed ones permitted too; the entry with the most required
// modifiers wins, then the first slot; a default answers any last key no entry matches.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "model/NazgKeycode.h"

namespace nazg
{
    // The options byte as the board stores it: vial-qmk's vial_arep_option_* and its enable flag.
    namespace AltRepeatOption
    {
        inline constexpr uint8_t DefaultToThis    = 0x01;   // after any last key no entry matches, this alt key
        inline constexpr uint8_t Bidirectional    = 0x02;   // after the alt key, the last key back
        inline constexpr uint8_t IgnoreHandedness = 0x04;   // Right Ctrl counts as Ctrl, for every mask
        inline constexpr uint8_t Enabled          = 0x08;
    }

    struct AltRepeatKey
    {
        std::optional<Keycode> lastKey;   // its modifiers (Ctrl+Z) required with it
        std::optional<Keycode> altKey;    // what Alt Repeat sends
        uint8_t                allowed = 0;   // modifiers that may be held too, in the USB HID order (Mod)
        uint8_t                options = 0;

        // No key either side and no modifier: a slot nobody set, whatever its options.
        [[nodiscard]] bool IsEmpty() const;

        [[nodiscard]] bool IsOn() const { return (options & AltRepeatOption::Enabled) != 0; }
        [[nodiscard]] bool Has(uint8_t option) const { return (options & option) != 0; }

        bool operator==(const AltRepeatKey&) const = default;
    };

    // A new entry as Nazg starts one -- usable at once, where Vial's reset slot is off: on, left
    // and right alike.
    [[nodiscard]] AltRepeatKey NewAltRepeatKey();

    // A keycode as the firmware matches it: a key with modifiers is the key, the modifiers apart; a
    // mod-tap or layer-tap is its tap key. Anything else is itself, with no modifiers.
    struct BaseKey
    {
        Keycode key;
        uint8_t mods = 0;
    };
    [[nodiscard]] BaseKey BaseOf(const Keycode& keycode);

    // Why QMK never remembers `keycode` as the last key -- "a modifier", "a layer key" -- or none
    // when it does (process_repeat_key.c, remember_last_key()).
    [[nodiscard]] std::optional<std::string_view> NeverRemembered(const Keycode& keycode);

    // A Repeat key, which Alt Repeat cannot send.
    [[nodiscard]] bool IsRepeatKey(const Keycode& keycode);

    // Whether vial-qmk sends `keycode` wrong as an alt key: a right-hand modifier with it.
    [[nodiscard]] bool SentWrong(const Keycode& keycode);

    // `keycode` with its right-hand modifiers moved to the left: what vial-qmk sends right.
    [[nodiscard]] Keycode WithLeftModifiers(const Keycode& keycode);

    // Both on, the same base last key and the same required modifiers -- by side unless either
    // ignores it: when both match, the firmware plays the first.
    [[nodiscard]] bool SameLastKey(const AltRepeatKey& first, const AltRepeatKey& second);

    // Whether QMK pairs the two keys itself, with no modifier and no entry (repeat_key.c): Left and
    // Right, Up and Down, Home and End, Page Up and Down, Backspace and Delete, [ and ].
    [[nodiscard]] bool QmkPairs(const Keycode& first, const Keycode& second);
}
