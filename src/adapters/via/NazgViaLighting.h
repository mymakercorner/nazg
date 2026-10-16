// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaLighting - one lighting system's values read, written and stored through the VIA custom
// value commands, in whichever of the three formats the board speaks (model/NazgLighting.h,
// LightingWire; via-vial-commands.md, "Lighting"). Written values are held in RAM -- QMK's
// `_noeeprom` functions -- until SaveLighting() stores them: the keyboard shows a change at once.

#pragma once

#include <cstdint>
#include <vector>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgLighting.h"

namespace nazg::via
{
    // VialRGB's own facts, asked once: its protocol (1 so far) and the brightness its RGB Matrix
    // tops out at -- RGB_MATRIX_MAXIMUM_BRIGHTNESS, the scale of its brightness.
    struct VialRgbInfo
    {
        uint16_t version       = 0;
        uint8_t  maxBrightness = 255;
    };

    [[nodiscard]] Task<VialRgbInfo> ReadVialRgbInfo(Protocol& via);

    // The VialRGB effects the board was built with, as VialRGB ids, off (0) left out.
    [[nodiscard]] Task<std::vector<uint16_t>> ReadVialRgbEffects(Protocol& via);

    // `maxBrightness` is VialRGB's (VialRgbInfo); every other wire's brightness is 0-255 already.
    [[nodiscard]] Task<LightingValues> ReadLighting(Protocol& via, LightingTarget target, uint8_t maxBrightness = 255);

    // Writes what `line` shows of `values` -- On and Effect both write the effect, 0 for off. On
    // the Legacy wire an effect is written twice: vial-qmk sets the mode before turning the light
    // on, so the first only turns on a light that was off. VialRGB sets effect, speed and colour
    // together, whatever the line.
    [[nodiscard]] Task<void> WriteLighting(Protocol& via, LightingTarget target, LightingLine line,
                                           const LightingValues& values, uint8_t maxBrightness = 255);

    // Stores what was written: the system's channel, or every lighting value without one.
    [[nodiscard]] Task<void> SaveLighting(Protocol& via, LightingTarget target);
}
