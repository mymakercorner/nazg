// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaKeymap - the keymap buffer as dynamic_keymap_get_buffer returns it, turned into the
// model's Keymap.
//
// Shared by the VIA and Vial loaders: Vial firmware answers the VIA keymap commands
// unchanged. The buffer is the device's EEPROM layout -- layer by layer, row-major within
// a layer, two big-endian bytes per cell -- and each value is decoded for the board's QMK
// keycode version on the way in, so nothing above this layer sees a raw keycode.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // How many bytes the device sends for a keymap of this shape: two per cell.
    [[nodiscard]] size_t ViaKeymapByteCount(uint8_t layers, uint8_t rows, uint8_t columns) noexcept;

    // Throws std::invalid_argument if the buffer is the wrong size for the shape.
    [[nodiscard]] Keymap DecodeViaKeymap(const std::vector<uint8_t>& bytes,
                                         uint8_t                     layers,
                                         uint8_t                     rows,
                                         uint8_t                     columns,
                                         QmkKeycodeVersion           version);
}
