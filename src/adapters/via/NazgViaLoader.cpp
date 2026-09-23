// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaLoader.h"

#include <string>
#include <utility>
#include <vector>

#include "adapters/via/NazgViaKeymap.h"

namespace nazg
{
    std::optional<QmkKeycodeVersion> QmkKeycodeVersionForVia(uint16_t viaProtocol) noexcept
    {
        if (viaProtocol >= 12)
            return QmkKeycodeVersion::V0_0_8;
        if (viaProtocol == 11)
            return QmkKeycodeVersion::V0_0_1;

        return std::nullopt;
    }

    // `definition` by value: it lives in the coroutine frame for the whole load, whatever
    // happens to the caller's copy meanwhile.
    Task<Keyboard> LoadViaKeyboard(ViaProtocol& protocol, KeyboardDefinition definition)
    {
        const uint16_t viaProtocol = co_await protocol.GetProtocolVersion();

        std::optional<QmkKeycodeVersion> keycodeVersion;

        if (viaProtocol >= 13)
        {
            // The board says which keycodes it has. One newer than this build's table
            // is read with the newest table there is: keycodes added since decode as
            // unknown values and write back unchanged, which beats refusing the board.
            const uint32_t bcd = co_await protocol.GetKeyboardValue(ViaKeyboardValue::KeycodesVersion);
            keycodeVersion     = QmkKeycodeVersionFromBcd(bcd);

            if (!keycodeVersion)
            {
                if (bcd == 0)
                    throw ProtocolError("the board reports keycode version 0.0.0");

                keycodeVersion = c_LatestQmkKeycodeVersion;
            }
        }
        else
        {
            keycodeVersion = QmkKeycodeVersionForVia(viaProtocol);
        }

        if (!keycodeVersion)
            throw ProtocolError("VIA protocol " + std::to_string(viaProtocol) +
                                " uses pre-renumbering keycodes, which are not supported yet");

        const uint8_t layers = co_await protocol.GetLayerCount();

        if (layers == 0)
            throw ProtocolError("the device reports no layers");

        // As in the Vial loader: a board without selectable layouts refuses the value id.
        uint32_t layoutOptions = 0;
        bool     hasLayouts    = true;

        try
        {
            layoutOptions = co_await protocol.GetKeyboardValue(ViaKeyboardValue::LayoutOptions);
        }
        catch (const ProtocolError&)
        {
            hasLayouts = false;
        }

        if (!hasLayouts)
            layoutOptions = 0;

        const size_t byteCount = ViaKeymapByteCount(layers, definition.matrixRows, definition.matrixColumns);

        const std::vector<uint8_t> keymapBytes =
            co_await protocol.GetKeymapBuffer(0, static_cast<uint16_t>(byteCount));

        Keymap keymap = DecodeViaKeymap(keymapBytes, layers, definition.matrixRows, definition.matrixColumns,
                                        *keycodeVersion);

        co_return BuildKeyboard(std::move(definition), std::move(keymap), layoutOptions, *keycodeVersion);
    }
}
