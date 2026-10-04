// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QmkExpression - a keycode typed as a QMK keymap writes it, read into the model's Keycode: the
// keycode picker's Any entry (ui-design.md, "The key line"), as VIA's and Vial's "Any" dialogs.
// The reverse of FormatKeycode(), which hover and the key line print -- whatever it prints, this
// reads back.
//
// QMK's own spellings, in any case and with any spaces: KC_A, LCTL(LSFT(KC_A)) and C(S(KC_A)),
// MT(MOD_LCTL|MOD_LSFT,KC_A) and LCTL_T(KC_A), LT(1,KC_SPC), MO(2) and the other layer keys,
// LM(1,MOD_LALT), OSM(MOD_LSFT), SH_T(KC_A), TD(3), MC_5, the _______ and XXXXXXX of keymaps, and
// a raw 0x value. Names are the board's keycode version's: one the board does not have is not read.
// Whether the board can STORE what was read is the encoder's answer, not this one's.
//
// Pure code, so it tests with literals.

#pragma once

#include <optional>
#include <string_view>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"

namespace nazg
{
    [[nodiscard]] std::optional<Keycode> ParseQmkExpression(std::string_view text, QmkKeycodeVersion version);
}
