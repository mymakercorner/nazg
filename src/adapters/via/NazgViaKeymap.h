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
//
// Writing goes the other way through the same codec, one cell at a time.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
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

    // Store one keycode, then read the cell back and return what the board ACTUALLY
    // holds. That is not always what was sent: a locked Vial board runs every write
    // through its keycode firewall, which silently turns QK_BOOT into KC_NO and still
    // answers success -- so the read-back is the only honest answer, and the caller
    // should compare it with what it asked for. See via-vial-commands.md, trap 8.
    //
    // Throws std::invalid_argument if the keycode cannot be stored on this keycode
    // version (EncodeQmkKeycode returned nullopt), before anything is sent.
    [[nodiscard]] Task<Keycode> WriteKeycode(ViaProtocol&      protocol,
                                             uint8_t           layer,
                                             uint8_t           row,
                                             uint8_t           column,
                                             Keycode           keycode,
                                             QmkKeycodeVersion version);
}
