// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Combo - up to four keys pressed together, sending another key instead of themselves
// (ui-design.md, "The Combos section"). Vial's, one slot of its table; protocol-neutral here --
// the section turns it into the board's entry and back.
//
// What the firmware does with it is QMK's (quantum/process_keycode/process_combo.c, read
// 2026-10-10), and this says it for the panel: which inputs it matches, where the keys sending
// them are, and how two combos sharing keys get along.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "model/NazgKeyboard.h"
#include "model/NazgKeycode.h"

namespace nazg
{
    struct Combo
    {
        // As the board stores them, in order; none where it stores 0. The firmware stops at the
        // first empty one (COMBO_END), so an input after it is never matched -- Nazg never writes
        // such a gap, but a board written by another app may hold one.
        std::array<std::optional<Keycode>, 4> inputs;
        std::optional<Keycode>                output;

        // No input and no output: an unused slot.
        [[nodiscard]] bool IsEmpty() const;

        // The inputs the firmware matches: up to the first empty one.
        [[nodiscard]] std::vector<Keycode> MatchedInputs() const;

        // An input after an empty one: the firmware never reaches it.
        [[nodiscard]] bool HasGap() const;

        // The inputs moved up over the empty ones, in their order.
        void CloseGaps();

        bool operator==(const Combo&) const = default;
    };

    // How two combos sharing keys get along, from the first's side (process_combo.c, overlaps()):
    // when both are complete, the one with more keys wins; with the same keys, the later slot.
    enum class ComboRelation : uint8_t
    {
        None,       // neither inside the other, or either matches nothing
        SameKeys,   // the same set of inputs
        Inside,     // every input of the first is one of the second's, which has more: it wins
        Holds,      // every input of the second is one of the first's, which has more: this one wins
    };

    [[nodiscard]] ComboRelation RelationOf(const Combo& first, const Combo& second);

    // The first layer, from 0 up and among `layers` (bit n for layer n), on which a key of the
    // selected layout sends `keycode` -- its own keycode there, as a combo or a key override
    // matches what a key sends; none when no key does.
    [[nodiscard]] std::optional<uint8_t> LayerSending(const Keyboard& keyboard, const Keycode& keycode,
                                                      uint16_t layers = 0xFFFF);

    // A key on layer 0 that sends `keycode` only as its tap -- LGUI_T(KC_A) for KC_A, a home-row
    // modifier -- which a combo of KC_A never matches: the combo needs the whole keycode. None when
    // there is no such key, or when some key sends `keycode` itself.
    [[nodiscard]] std::optional<Keycode> TapHoldSending(const Keyboard& keyboard, const Keycode& keycode);
}
