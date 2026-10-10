// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyOverride - with some modifiers held, a key sends another instead: Shift + Backspace for
// Delete (ui-design.md, "The Key Overrides section"). Vial's, one slot of its table;
// protocol-neutral here -- the section turns it into the board's entry and back.
//
// What the firmware does with it is QMK's (quantum/process_keycode/process_key_override.c, read
// 2026-10-10): the trigger is matched against what the pressed key sends, on the layer it was
// pressed on; the slots are tried in order and the first that matches wins; the replacement goes
// through register_code(), 8 bits, so only a plain key -- with modifiers -- can be sent.

#pragma once

#include <cstdint>
#include <optional>

#include "model/NazgKeycode.h"

namespace nazg
{
    // The options byte as the board stores it: QMK's ko_option_t in bits 0-5, Vial's own enable
    // flag in bit 7.
    namespace KeyOverrideOption
    {
        inline constexpr uint8_t TriggerDown            = 0x01;   // may start when the key is pressed
        inline constexpr uint8_t RequiredModDown        = 0x02;   // ... when a held modifier is pressed after it
        inline constexpr uint8_t NegativeModUp          = 0x04;   // ... when a modifier not allowed is released
        inline constexpr uint8_t Activations            = 0x07;   // none of the three set reads as all three
        inline constexpr uint8_t OneMod                 = 0x08;   // any one held modifier is enough
        inline constexpr uint8_t NoReregister           = 0x10;   // the key still held is not sent again after
        inline constexpr uint8_t NoUnregisterOnOtherKey = 0x20;   // another key pressed does not end it
        inline constexpr uint8_t Enabled                = 0x80;
    }

    // Every layer bit: what *All* writes, so layers added later are covered too.
    inline constexpr uint16_t c_AllKeyOverrideLayers = 0xFFFF;

    struct KeyOverride
    {
        std::optional<Keycode> trigger;       // none: the modifiers alone
        std::optional<Keycode> replacement;   // none: the combination is blocked
        uint16_t               layers = 0;    // bit n: acts when the key is pressed on layer n

        // Modifier masks, one bit each in the USB HID order (Mod in model/NazgKeycode.h). Both
        // sides of a modifier set: either side.
        uint8_t held    = 0;   // all of them, or any one with KeyOverrideOption::OneMod
        uint8_t notHeld = 0;   // any of them held stops it
        uint8_t hidden  = 0;   // kept from the computer while it acts

        uint8_t options = 0;

        // No key, nothing sent and no modifier: a slot nobody set, whatever its options.
        [[nodiscard]] bool IsEmpty() const;

        [[nodiscard]] bool IsOn() const { return (options & KeyOverrideOption::Enabled) != 0; }

        // What may start it, as the firmware reads the options: none set is all three.
        [[nodiscard]] uint8_t Activations() const;

        bool operator==(const KeyOverride&) const = default;
    };

    // A new override as Nazg starts one -- usable at once, where Vial's reset slot is off and on
    // no layer: on, every layer, QMK's usual options (ko_make_basic()'s).
    [[nodiscard]] KeyOverride NewKeyOverride();

    // Whether the firmware can send `raw`, a QMK keycode, as a replacement: a basic key -- media
    // keys included -- alone or with modifiers. A layer key, Boot, a macro send nothing.
    [[nodiscard]] bool CanBeSent(uint16_t raw);

    // Both on, the same key and the same modifiers held, a layer of `boardLayers` in common: the
    // firmware only ever plays the first of the two.
    [[nodiscard]] bool SameRule(const KeyOverride& first, const KeyOverride& second, uint16_t boardLayers);
}
