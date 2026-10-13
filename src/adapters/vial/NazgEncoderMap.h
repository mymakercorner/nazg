// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The knobs' encoder map, read and written for either protocol (ui-design.md, "Knobs";
// via-vial-commands.md for the wire). What a knob sends turned each way, per layer, is kept by the
// firmware only when it is built with ENCODER_MAP_ENABLE; otherwise a turn is its own code, which
// nothing can read or change.
//
//   Vial  0xFE 0x03 / 0x04 -- both directions in one reply; without the map, the request echoed.
//   VIA   0x14 / 0x15 from protocol 10 -- a call per direction; without the map, 0xFF.
//
// Through VialProtocol, which is VIA's too: the board report says which branch to take.

#pragma once

#include <cstdint>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // The map of `knobs` knobs on `layers` layers, [layer][knob]: a reply per knob and layer on
    // Vial, two on VIA. Empty when the firmware keeps none -- VIA before protocol 10, or the first
    // request refused. Throws ProtocolError if a later request is.
    [[nodiscard]] Task<std::vector<std::vector<KnobTurns>>> ReadEncoderMap(VialProtocol&     protocol,
                                                                           bool              isVial,
                                                                           uint16_t          viaProtocol,
                                                                           uint8_t           layers,
                                                                           uint8_t           knobs,
                                                                           QmkKeycodeVersion version);

    // Store one turn, then read it back and return what the board holds -- as WriteKeycode() does
    // for a key: a locked Vial board's firewall may have changed it.
    //
    // Throws std::invalid_argument if the keycode cannot be stored on this keycode version,
    // before anything is sent.
    [[nodiscard]] Task<Keycode> WriteKnobTurn(VialProtocol&     protocol,
                                              bool              isVial,
                                              uint8_t           layer,
                                              uint8_t           knob,
                                              bool              clockwise,
                                              Keycode           keycode,
                                              QmkKeycodeVersion version);
}
