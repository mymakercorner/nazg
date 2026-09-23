// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Loading a whole keyboard off a VIA board -- the sibling of NazgVialLoader.h.
//
// The one real difference from Vial is that a VIA board does not carry its definition:
// the caller brings it, from a file today and perhaps a registry later. Everything after
// that -- layers, layout options, the keymap buffer and its decode -- is the same code
// the Vial loader runs, because Vial firmware answers these commands unchanged.
//
// The other difference is how the keycode version is found. VIA has a real answer from
// protocol 13 on (id_keycodes_version), and before that only the protocol version to go
// by. See docs/research_material/keycodes.md, "Picking the dictionary for a board".

#pragma once

#include <cstdint>
#include <optional>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // The keycode table for a VIA protocol version that cannot report its own:
    //   12  -> 0.0.8, the newest a protocol-12 build can have (13 landed before 0.0.9);
    //          an older board anywhere from 0.0.2 differs only in the output keys moved
    //          at 0.0.6, which then decode as unknown values and still write back intact
    //   11  -> 0.0.1, which arrived in the same commit as protocol 11
    //   <=10 -> nullopt: pre-renumbering keycodes have no table yet
    // Protocol 13 and later report their version, so the loader asks instead of calling this.
    [[nodiscard]] std::optional<QmkKeycodeVersion> QmkKeycodeVersionForVia(uint16_t viaProtocol) noexcept;

    // Throws HidTransportError if the device goes away, ProtocolError if it answers
    // something unusable -- including a protocol whose keycodes have no table -- and
    // std::invalid_argument if the definition's matrix does not fit the keymap.
    [[nodiscard]] Task<Keyboard> LoadViaKeyboard(ViaProtocol& protocol, KeyboardDefinition definition);
}
