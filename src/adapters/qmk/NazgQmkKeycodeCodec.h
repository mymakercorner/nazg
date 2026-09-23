// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QmkKeycodeCodec - a QMK board's 16-bit keycodes to the model's Keycode and back.
//
// The one place a raw QMK value is interpreted. Fixed keycodes come from the versioned
// table (NazgQmkKeycodes.h); the parameterised ranges -- LCTL(kc), MT, LT, MO, TD and the
// rest -- are bit arithmetic on layouts that have not moved since keycode spec 0.0.1,
// except PDF(n), which only exists from 0.0.6.
//
// Decoding never fails: anything unplaceable becomes UnknownKey, which encodes back to
// the same value. Encoding can fail, and that is the useful answer -- nullopt means this
// board cannot store the keycode (too new for its version, a layer beyond what the range
// holds, or modifiers QMK cannot express).

#pragma once

#include <cstdint>
#include <optional>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"

namespace nazg
{
    [[nodiscard]] Keycode DecodeQmkKeycode(uint16_t value, QmkKeycodeVersion version);

    [[nodiscard]] std::optional<uint16_t> EncodeQmkKeycode(const Keycode& keycode, QmkKeycodeVersion version);
}
