// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgEncoderMap.h"

#include <optional>
#include <stdexcept>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

namespace nazg
{
    namespace
    {
        // VIA's encoder commands came with protocol 10.
        constexpr uint16_t c_ViaEncoderProtocol = 10;

        // Both turns of one knob on one layer.
        Task<KnobTurns> ReadTurns(VialProtocol& protocol, bool isVial, uint8_t layer, uint8_t knob,
                                  QmkKeycodeVersion version)
        {
            KnobTurns turns;
            if (isVial)
            {
                const VialProtocol::EncoderPair pair = co_await protocol.GetVialEncoder(layer, knob);
                turns.counterClockwise               = DecodeQmkKeycode(pair.counterClockwise, version);
                turns.clockwise                      = DecodeQmkKeycode(pair.clockwise, version);
            }
            else
            {
                turns.counterClockwise = DecodeQmkKeycode(co_await protocol.GetEncoder(layer, knob, false), version);
                turns.clockwise        = DecodeQmkKeycode(co_await protocol.GetEncoder(layer, knob, true), version);
            }
            co_return turns;
        }
    }

    Task<std::vector<std::vector<KnobTurns>>> ReadEncoderMap(VialProtocol&     protocol,
                                                             bool              isVial,
                                                             uint16_t          viaProtocol,
                                                             uint8_t           layers,
                                                             uint8_t           knobs,
                                                             QmkKeycodeVersion version)
    {
        std::vector<std::vector<KnobTurns>> map;
        if (knobs == 0 || layers == 0 || (!isVial && viaProtocol < c_ViaEncoderProtocol))
            co_return map;

        // The first request says whether the firmware keeps a map at all: refused, it has none.
        KnobTurns first;
        try
        {
            first = co_await ReadTurns(protocol, isVial, 0, 0, version);
        }
        catch (const ProtocolError&)
        {
            co_return map;
        }

        map.assign(layers, std::vector<KnobTurns>(knobs));
        map[0][0] = first;
        for (uint8_t layer = 0; layer < layers; ++layer)
            for (uint8_t knob = 0; knob < knobs; ++knob)
                if (layer != 0 || knob != 0)
                    map[layer][knob] = co_await ReadTurns(protocol, isVial, layer, knob, version);
        co_return map;
    }

    // `keycode` by value: a coroutine keeps its parameters in its frame (NazgViaKeymap.cpp).
    Task<Keycode> WriteKnobTurn(VialProtocol&     protocol,
                                bool              isVial,
                                uint8_t           layer,
                                uint8_t           knob,
                                bool              clockwise,
                                Keycode           keycode,
                                QmkKeycodeVersion version)
    {
        const std::optional<uint16_t> value = EncodeQmkKeycode(keycode, version);
        if (!value)
            throw std::invalid_argument(FormatKeycode(keycode) + " cannot be stored on QMK keycodes " +
                                        QmkKeycodeVersionName(version));

        if (isVial)
        {
            co_await protocol.SetVialEncoder(layer, knob, clockwise, *value);
            const VialProtocol::EncoderPair pair = co_await protocol.GetVialEncoder(layer, knob);
            co_return DecodeQmkKeycode(clockwise ? pair.clockwise : pair.counterClockwise, version);
        }

        co_await protocol.SetEncoder(layer, knob, clockwise, *value);
        co_return DecodeQmkKeycode(co_await protocol.GetEncoder(layer, knob, clockwise), version);
    }
}
