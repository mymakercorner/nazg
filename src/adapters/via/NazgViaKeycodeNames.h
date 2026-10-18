// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaKeycodeNames - a keycode as the VIA app spells it in a saved layout (.layout.json), read back
// into its value. docs/research_material/via-layout-files.md, "Keycodes are VIA's names".
//
// VIA names keycodes from tables of its own, one per protocol -- not QMK's, not vial-gui's: KC_SLCK,
// KC_MS_UP, and its MIDI keys numbered from 0x7110 where QMK starts at 0x7103. A name means what
// VIA's table says, so only its tables give back the value the board held. Four of them differ:
// protocol 9 and older, 10 (only TO moves), 11, and 12 on -- 13 trades names, no value.
//
// Values, not keycodes: what a value means is the codec's, for the board's keycode version
// (adapters/qmk/NazgQmkKeycodeCodec.h). Pure, so it tests with literals.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "adapters/qmk/NazgQmkKeycodes.h"

namespace nazg::via
{
    enum class NameTable : uint8_t
    {
        V9,    // default.ts: protocol 9 and older, before QMK's renumbering
        V10,   // v10.ts
        V11,   // v11.ts: the renumbering, VIA's own MACRO and CUSTOM values
        V12,   // v12.ts and v13.ts
    };

    // The VIA app's choice for a board on VIA `protocol`.
    [[nodiscard]] NameTable NameTableFor(uint16_t protocol) noexcept;

    // For a board whose keycodes are `version` -- a Vial board, whose VIA protocol is always 9 and
    // says nothing of them: before the renumbering, protocol 9's or 10's; after it, 12's.
    [[nodiscard]] NameTable NameTableFor(qmk::KeycodeVersion version) noexcept;

    // The value of a keycode as VIA writes one: a name of the table, a name VIA has since renamed
    // (KC_TILD, MACRO00, USER00, SPC_FN1), MO(1) and the other layer keys, MACRO(3), CUSTOM(0),
    // LT(1,KC_SPC), MT(MOD_LCTL | MOD_LSFT,KC_A), OSM(MOD_LSFT), LM(1,MOD_LCTL), S(KC_1) and the other
    // modifier forms -- and its hex, "0x7c40", which VIA itself reads as nothing. None for anything
    // else, where VIA reads KC_NO.
    [[nodiscard]] std::optional<uint16_t> ReadKeycodeName(std::string_view text, NameTable table);

    // A name of the table alone -- what a macro's braces hold.
    [[nodiscard]] std::optional<uint16_t> ValueOfName(std::string_view name, NameTable table);

    namespace detail
    {
        struct NameRow
        {
            const char* name;
            int32_t     values[4];   // by NameTable; -1: not in that table
        };

        // Sorted by name. NazgViaKeycodeNameTable.cpp.
        [[nodiscard]] const NameRow* NameRows(size_t& count) noexcept;
    }
}
