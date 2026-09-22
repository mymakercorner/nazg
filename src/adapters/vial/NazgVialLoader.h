// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Loading a whole keyboard off a Vial board: the one coroutine in this part of the
// stack, and the only thing here that waits.
//
// Roughly 55 round trips at a millisecond or two each -- 22 pages of definition, 31
// chunks of keymap, a handful of queries -- so a tenth of a second or more, which is
// why it cannot be a blocking call on the frame thread. Everything after the bytes
// arrive (inflate, parse, decode the keymap, assemble) is microseconds and is plain
// synchronous code in model/NazgKeyboard.h.
//
//     nazg::Task<void> Connect(HidTransport& transport, std::string path, AppState& state)
//     {
//         DeviceId device = co_await transport.Open(path);
//         HidDeviceChannel channel(transport, device);
//         VialProtocol protocol(channel);
//         state.keyboard = co_await LoadVialKeyboard(protocol);
//         transport.Close(device);
//     }
//
// VIA will get a sibling loader taking the definition from a file or registry instead
// of from the device; everything below the sourcing is already shared.

#pragma once

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // Throws HidTransportError if the device goes away, or ProtocolError if it answers
    // something unusable -- including when it turns out not to be a Vial board.
    [[nodiscard]] Task<Keyboard> LoadVialKeyboard(VialProtocol& protocol);
}
