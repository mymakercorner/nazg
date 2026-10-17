// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VialKeycodeNames - a keycode value as vial-gui spells it in a saved layout (.vil), and back.
// docs/research_material/vil-files.md, "Keycodes are names".
//
// vial-gui names a keycode from a table of its own, not QMK's: KC_ENTER where QMK writes KC_ENT,
// RGB_TOG where QMK 0.0.9 writes UG_TOGG, M3 and USER05 and LT1(KC_SPACE) -- and it reads back only
// its own names. So a file Nazg writes spells keys with these names, or Vial would load them as
// nothing; and a file Vial wrote is read through them. Two tables, as vial-gui has: v6 for a board
// on Vial protocol 6, v5 for everything else -- a VIA board included, which vial-gui reads with
// protocol -1 and so with v5: right for the VIA 9 boards it opens, all from before the renumbering,
// wrong for a later one, so Nazg writes a VIA board's keycodes as raw values instead
// (adapters/vial/NazgVialSavedLayout.h).
//
// Values, not keycodes: what a value means is the codec's, for the board's keycode version
// (adapters/qmk/NazgQmkKeycodeCodec.h). Pure, so it tests with literals.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nazg::vial
{
    enum class NameTable : uint8_t
    {
        V5,
        V6,
    };

    // vial-gui's choice: v6 on Vial protocol 6, v5 below it and on VIA boards (-1).
    [[nodiscard]] NameTable NameTableFor(int32_t vialProtocol) noexcept;

    // The name vial-gui writes for `value`: a key of its table, a masked key as outer(inner) --
    // LSFT(KC_5), LT1(KC_SPACE) -- M3, TD(2), MO(1) and the other layer keys; else the value in hex,
    // "0x7e05", as vial-gui writes one.
    [[nodiscard]] std::string SpellKeycode(uint16_t value, NameTable table);

    // The value of a name vial-gui writes -- the forms above, a hex or decimal number; none for
    // anything else.
    [[nodiscard]] std::optional<uint16_t> ReadKeycodeName(std::string_view text, NameTable table);

    namespace detail
    {
        enum class NameKind : uint8_t
        {
            Key,      // a keycode, written as its name
            Masked,   // LSFT(kc): the value is the range's base, kc a basic key
            Base,     // QK_MACRO, QK_MOMENTARY...: the start of a range numbered M3, MO(1)
        };

        struct NameRow
        {
            const char* name;
            int32_t     v5;   // -1: not in that table
            int32_t     v6;
            NameKind    kind;
        };

        // Sorted by name. NazgVialKeycodeNameTable.cpp.
        [[nodiscard]] const NameRow* NameRows(size_t& count) noexcept;
    }
}
