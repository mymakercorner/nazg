// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaMacro - the macro buffer, decoded into each macro's actions and encoded back, as the
// firmware plays them. docs/research_material/via-vial-commands.md, "Macros -- the buffer and its
// byte format".
//
// One buffer holds every macro, each ended by a 0 byte. A plain byte is a character, which the
// firmware types through its US table; 0x01 introduces an action -- tap, press, release a key,
// or wait. Four formats, picked by the protocols the board reports:
//
//   Unprefixed    VIA <= 10, Vial 0-1    01/02/03 kc: tap, press, release; no waits
//   Via           VIA >= 11              01 01..03 kc; 01 04 '1' '0' '0' '|' waits 100 ms
//   VialBasic     Vial 2-4               01 01..03 kc; 01 04 d0 d1 waits (d0-1) + (d1-1)*255 ms
//   VialExtended  Vial 5 and later       VialBasic, and 01 05..07 lo hi: any 16-bit keycode
//
// The two wait encodings cannot be read for one another, so the format is never guessed from
// the bytes. Pure: no device, no keycode names -- a key is its 16-bit value, which the keycode
// codec names for the board's keycode version.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/NazgKeyboard.h"

namespace nazg
{
    enum class MacroFormat : uint8_t
    {
        Unprefixed,
        Via,
        VialBasic,
        VialExtended,
    };

    // The format of a board, from what it reports: a Vial board by its Vial protocol -- vial-qmk
    // always reports VIA 9 -- and a VIA board by its VIA protocol.
    [[nodiscard]] MacroFormat MacroFormatFor(const BoardReport& report) noexcept;

    [[nodiscard]] bool HasWaits(MacroFormat format) noexcept;          // all but Unprefixed
    [[nodiscard]] bool HoldsAnyKeycode(MacroFormat format) noexcept;   // VialExtended only: else 8-bit keys

    // The longest wait one action holds: Vial's two bytes reach 65 024 ms; VIA's digits have no
    // limit in the firmware, and are kept to what a 16-bit value holds.
    [[nodiscard]] uint32_t LongestWait(MacroFormat format) noexcept;

    struct MacroAction
    {
        enum class Kind : uint8_t
        {
            Character,   // value: the byte, typed through the firmware's US table
            Tap,         // value: the keycode, pressed then released
            Press,
            Release,
            Wait,        // value: milliseconds
        };

        Kind     kind  = Kind::Character;
        uint32_t value = 0;

        bool operator==(const MacroAction&) const = default;
    };

    using MacroActions = std::vector<MacroAction>;

    // The bytes of a buffer that hold `count` macros: up to and including the count-th 0, or all
    // of them when there are fewer -- what a read needs before it can stop.
    [[nodiscard]] std::optional<size_t> MacrosEnd(const std::vector<uint8_t>& bytes, size_t count) noexcept;

    // `count` macros from the buffer's bytes -- empty ones where the buffer ends early. What does
    // not decode is skipped, as the firmware would mostly skip it.
    [[nodiscard]] std::vector<MacroActions> DecodeMacros(const std::vector<uint8_t>& bytes, size_t count,
                                                         MacroFormat format);

    // The bytes of `macros`, each ended by a 0: what goes into the buffer from its start. Empty and
    // `error` set when an action does not fit the format -- a wait where there are none, a keycode
    // above 0xFF where keys are 8-bit, a character the format cannot hold.
    [[nodiscard]] std::vector<uint8_t> EncodeMacros(const std::vector<MacroActions>& macros, MacroFormat format,
                                                    std::string* error = nullptr);

    // What one action costs, in bytes, or nothing when the format cannot hold it.
    [[nodiscard]] std::optional<size_t> ActionSize(const MacroAction& action, MacroFormat format) noexcept;
}
