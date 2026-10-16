// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaLoader.h"

#include <string>
#include <utility>
#include <vector>

#include "adapters/via/NazgViaKeymap.h"

namespace nazg::via
{
    qmk::KeycodeVersion KeycodeVersionFor(uint16_t viaProtocol) noexcept
    {
        if (viaProtocol >= 12)
            return qmk::KeycodeVersion::V0_0_8;
        if (viaProtocol == 11)
            return qmk::KeycodeVersion::V0_0_1;
        if (viaProtocol == 10)
            return qmk::KeycodeVersion::LegacyVia10;

        return qmk::KeycodeVersion::Legacy;
    }

    // `definition` by value: it lives in the coroutine frame for the whole load, whatever
    // happens to the caller's copy meanwhile.
    Task<Keyboard> LoadKeyboard(Protocol& protocol, KeyboardDefinition definition)
    {
        const uint16_t viaProtocol = co_await protocol.GetProtocolVersion();

        qmk::KeycodeVersion keycodeVersion = KeycodeVersionFor(viaProtocol);

        if (viaProtocol >= 13)
        {
            // The board says which keycodes it has. One newer than this build's table
            // is read with the newest table there is: keycodes added since decode as
            // unknown values and write back unchanged, which beats refusing the board.
            const uint32_t bcd = co_await protocol.GetKeyboardValue(KeyboardValue::KeycodesVersion);

            if (bcd == 0)
                throw ProtocolError("the board reports keycode version 0.0.0");

            keycodeVersion = qmk::KeycodeVersionFromBcd(bcd).value_or(qmk::c_LatestKeycodeVersion);
        }

        const uint8_t layers = co_await protocol.GetLayerCount();

        if (layers == 0)
            throw ProtocolError("the device reports no layers");

        // As in the Vial loader: a board without selectable layouts refuses the value id.
        uint32_t layoutOptions = 0;
        bool     hasLayouts    = true;

        try
        {
            layoutOptions = co_await protocol.GetKeyboardValue(KeyboardValue::LayoutOptions);
        }
        catch (const ProtocolError&)
        {
            hasLayouts = false;
        }

        if (!hasLayouts)
            layoutOptions = 0;

        const size_t byteCount = KeymapByteCount(layers, definition.matrixRows, definition.matrixColumns);

        const std::vector<uint8_t> keymapBytes =
            co_await protocol.GetKeymapBuffer(0, static_cast<uint16_t>(byteCount));

        Keymap keymap = DecodeKeymap(keymapBytes, layers, definition.matrixRows, definition.matrixColumns,
                                     keycodeVersion);

        co_return BuildKeyboard(std::move(definition), std::move(keymap), layoutOptions, keycodeVersion);
    }
}
