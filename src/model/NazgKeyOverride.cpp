// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyOverride.h"

namespace nazg
{
    bool KeyOverride::IsEmpty() const
    {
        return !trigger && !replacement && held == 0 && notHeld == 0 && hidden == 0;
    }

    uint8_t KeyOverride::Activations() const
    {
        const uint8_t set = options & KeyOverrideOption::Activations;
        return set != 0 ? set : KeyOverrideOption::Activations;
    }

    KeyOverride NewKeyOverride()
    {
        KeyOverride fresh;
        fresh.layers  = c_AllKeyOverrideLayers;
        fresh.options = KeyOverrideOption::Enabled | KeyOverrideOption::Activations;
        return fresh;
    }

    bool CanBeSent(uint16_t raw)
    {
        // QK_MODS (0x0100-0x1FFF) holds a basic key under its modifiers, which go as weak mods;
        // what remains is registered with register_code(uint8_t). Below KC_A nothing is a key.
        return raw <= 0x1FFF && (raw & 0xFF) >= 0x04;
    }

    bool SameRule(const KeyOverride& first, const KeyOverride& second, uint16_t boardLayers)
    {
        return first.IsOn() && second.IsOn() && !first.IsEmpty() && !second.IsEmpty() &&
               first.trigger == second.trigger && first.held == second.held &&
               (first.layers & second.layers & boardLayers) != 0;
    }
}
