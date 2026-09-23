// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialLoader.h"

#include <string>
#include <utility>

#include "adapters/via/NazgViaKeymap.h"
#include "adapters/vial/NazgVialDefinition.h"

namespace nazg
{
    QmkKeycodeVersion QmkKeycodeVersionForVial(uint32_t vialProtocol) noexcept
    {
        if (vialProtocol >= 6)
            return QmkKeycodeVersion::V0_0_7;

        return QmkKeycodeVersion::Legacy;
    }

    Task<Keyboard> LoadVialKeyboard(VialProtocol& protocol)
    {
        // The payoff of the coroutine layer: a sequence of round trips reading top to
        // bottom, with the frame loop still running between each one.
        const std::optional<VialIdentity> identity = co_await protocol.Detect();

        if (!identity)
            throw ProtocolError("this device is not a Vial board");

        const QmkKeycodeVersion keycodeVersion = QmkKeycodeVersionForVial(identity->protocolVersion);

        KeyboardDefinition definition = DecodeDefinition(co_await protocol.DownloadDefinition());

        const uint8_t layers = co_await protocol.GetLayerCount();

        if (layers == 0)
            throw ProtocolError("the device reports no layers");

        // Layout options are optional: a board with no selectable layouts answers 0xFF
        // to the value id, which is a refusal rather than a failure worth propagating.
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

        // One bulk read rather than a call per key: 31 round trips instead of 432 for
        // a 3 x 8 x 18 board.
        const size_t byteCount = ViaKeymapByteCount(layers, definition.matrixRows, definition.matrixColumns);

        const std::vector<uint8_t> keymapBytes =
            co_await protocol.GetKeymapBuffer(0, static_cast<uint16_t>(byteCount));

        Keymap keymap = DecodeViaKeymap(keymapBytes, layers, definition.matrixRows,
                                        definition.matrixColumns, keycodeVersion);

        co_return BuildKeyboard(std::move(definition), std::move(keymap), layoutOptions, keycodeVersion);
    }
}
