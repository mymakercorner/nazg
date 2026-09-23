// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaKeymap.h"

#include <optional>
#include <stdexcept>
#include <string>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"

namespace nazg
{
    size_t ViaKeymapByteCount(uint8_t layers, uint8_t rows, uint8_t columns) noexcept
    {
        return static_cast<size_t>(layers) * rows * columns * 2;
    }

    Keymap DecodeViaKeymap(const std::vector<uint8_t>& bytes,
                           uint8_t                     layers,
                           uint8_t                     rows,
                           uint8_t                     columns,
                           QmkKeycodeVersion           version)
    {
        const size_t expected = ViaKeymapByteCount(layers, rows, columns);

        if (bytes.size() != expected)
            throw std::invalid_argument("keymap buffer is " + std::to_string(bytes.size()) +
                                        " bytes; expected " + std::to_string(expected));

        Keymap keymap(layers, rows, columns);

        size_t offset = 0;
        for (uint8_t layer = 0; layer < layers; ++layer)
            for (uint8_t row = 0; row < rows; ++row)
                for (uint8_t column = 0; column < columns; ++column)
                {
                    const auto value = static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
                    offset += 2;

                    keymap.Set(layer, row, column, DecodeQmkKeycode(value, version));
                }

        return keymap;
    }

    // `keycode` by value: a coroutine keeps its parameters in its frame, and a reference
    // could dangle across the co_awaits if the caller's Keycode went away meanwhile.
    Task<Keycode> WriteKeycode(ViaProtocol&      protocol,
                               uint8_t           layer,
                               uint8_t           row,
                               uint8_t           column,
                               Keycode           keycode,
                               QmkKeycodeVersion version)
    {
        const std::optional<uint16_t> value = EncodeQmkKeycode(keycode, version);

        if (!value)
            throw std::invalid_argument(FormatKeycode(keycode) + " cannot be stored on QMK keycodes " +
                                        QmkKeycodeVersionName(version));

        co_await protocol.SetKeycode(layer, row, column, *value);

        const uint16_t stored = co_await protocol.GetKeycode(layer, row, column);

        co_return DecodeQmkKeycode(stored, version);
    }
}
