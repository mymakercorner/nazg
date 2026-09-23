// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycodePicker - choose a keycode to put on a key.
//
// FIRST DRAFT, expected to be replaced, like NazgKeyboardView.h. It lists everything the
// board's keycode version can store -- the whole QMK table, grouped by QMK's own `group`,
// plus layer keys for the board's layers -- which is the point it proves: no keycode needs
// typing as hex. How a real picker should look is a design question for later.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    struct KeycodePickerState
    {
        char filter[64] = {};
    };

    // Returns the keycode clicked this frame, if any.
    [[nodiscard]] std::optional<Keycode> DrawKeycodePicker(KeycodePickerState& state,
                                                           QmkKeycodeVersion   version,
                                                           uint8_t             layerCount,
                                                           const HostLayout&   layout);
}
