// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycapLegend - what to print on a key: a Keycode seen through the host's layout.
//
// The firmware stores positions, not characters: KC_Q is "the key where US QWERTY has
// Q", and it types "a" on a French AZERTY host. So legends are presentation, computed
// here at draw time from the keycode and a HostLayout; the keymap itself never changes
// with the host. Decided 2026-09-23 -- see docs/research_material/keycodes.md, "Host
// layouts": plain and Shift legends, one global host layout, US first.
//
// Pure code, no ImGui, so it tests with literals.

#pragma once

#include <span>
#include <string>
#include <string_view>

#include "model/NazgKeycode.h"

namespace nazg
{
    // What one position types on a host layout: plain, and with Shift held. `shifted`
    // is empty where Shift adds nothing worth printing -- the letters, whose one legend
    // is the capital.
    struct HostLegend
    {
        std::string_view key;       // basic keycode name: KC_1, KC_Q, KC_NUBS
        std::string_view plain;
        std::string_view shifted;
    };

    // A host keyboard layout: the positions whose legend depends on it. Everything else
    // -- Enter, F1, arrows, keypad -- types the same on every layout and is labelled
    // from the keycode table.
    struct HostLayout
    {
        std::string_view            name;
        std::span<const HostLegend> legends;

        [[nodiscard]] const HostLegend* Find(std::string_view key) const noexcept;
    };

    [[nodiscard]] const HostLayout& UsHostLayout() noexcept;

    // Two legends, as on a printed keycap: `primary` is the main one, `secondary` the
    // small one above it -- the Shift character for a symbol key, the hold action for a
    // tap-hold key (LT(1,KC_A) is "A" under "LT 1"). Either can be empty.
    struct KeycapLegend
    {
        std::string primary;
        std::string secondary;

        bool operator==(const KeycapLegend&) const = default;
    };

    [[nodiscard]] KeycapLegend LegendFor(const Keycode& keycode, const HostLayout& layout);
}
