// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HostKeys - the keys the computer receives, matched to the board's keys, for the live test on a
// firmware that does not report its switches (ui-design.md, "The key test"). The computer names a
// key by its USB HID usage; the board's keymap by QMK's keycode names. A key is tested by the key
// it sends -- its own, or the one a modifier or a hold goes with -- and a key that sends nothing the
// computer reports cannot be tested this way: a layer key, KC_NO, Boot, a macro.
//
// Pure: no device, no window, so it tests with literals.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"

namespace nazg
{
    // A key the computer reports, as USB HID numbers it: the page above the usage.
    inline constexpr uint32_t c_KeyboardPage = 0x07;
    inline constexpr uint32_t c_ConsumerPage = 0x0C;

    [[nodiscard]] constexpr uint32_t HidUsage(uint32_t page, uint32_t usage) noexcept
    {
        return (page << 16) | usage;
    }

    // The names of the keycodes that make the computer report `usage`, on a board whose keycodes
    // are `version`: one for most keys. The volume keys have two -- KC_VOLU, sent on the consumer
    // page, and KC_KB_VOLUME_UP, on the keyboard page, both arrive as Volume Up. None for a usage
    // no keycode tested here sends.
    [[nodiscard]] std::vector<std::string_view> KeycodesSending(uint32_t usage, qmk::KeycodeVersion version);

    // The name a key is tested by: its own, or the key a modifier or a hold goes with --
    // LSFT(KC_1), MT(MOD_LCTL, KC_A), LT(1, KC_SPC). None when the computer would report nothing of
    // it: a layer key, KC_NO, KC_TRNS on the base layer, Boot, a macro, a mouse or system key.
    [[nodiscard]] std::optional<std::string_view> TestedName(const Keycode& keycode, qmk::KeycodeVersion version);
}
