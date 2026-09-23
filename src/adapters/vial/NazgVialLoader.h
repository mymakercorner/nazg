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

#include <cstdint>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // Which QMK keycode table a Vial board uses, from its VIAL protocol version -- never
    // its VIA one, which vial-qmk hard-codes to 9 whatever its keycodes are.
    //
    // The split is clean: the bump to 6 came inside the very QMK merge that brought the
    // renumbering, so every protocol-5 build is pre-renumbering and every protocol-6 build
    // is keycode spec 0.0.2 or later. Upstream vial-qmk is at 0.0.7 (last QMK merge
    // 2025-06-21), so that is the table. A board built from an older vial-qmk can be
    // anywhere from 0.0.2, and the only value that moved in between is the output group at
    // 0.0.6 -- such a key decodes as an unknown value and still writes back unchanged.
    // Protocol 5 and below are Legacy, with TO(n) carrying its ON_PRESS bit as vial-gui's
    // own v5 table assumes. See docs/research_material/keycodes.md, "Vial's protocol
    // history against QMK's renumbering".
    [[nodiscard]] QmkKeycodeVersion QmkKeycodeVersionForVial(uint32_t vialProtocol) noexcept;

    // Throws HidTransportError if the device goes away, or ProtocolError if it answers
    // something unusable -- including when it turns out not to be a Vial board.
    [[nodiscard]] Task<Keyboard> LoadVialKeyboard(VialProtocol& protocol);
}
