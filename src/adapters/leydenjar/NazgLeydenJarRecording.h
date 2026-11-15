// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Recording - Signal levels' readings as one JSON document, in the format of Nazg's test recordings
// (tests/data/leyden-jar/README.md): the controller's information, the board's definition and layer 0,
// then every reading of every connector. Written by the Diagnostics section's temporary Save
// readings... (Advanced tools), and to be embedded in the saved report (ui-design.md, "Signal levels,
// fourth round": the readings embedded) -- a tester's session becomes a recording the tests replay.
//
// Pure: no ImGui, no transport.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "adapters/leydenjar/NazgLeydenJarProtocol.h"

namespace nazg::leydenjar
{
    struct Recording
    {
        std::string    board;   // what the board calls itself over USB
        ControllerInfo info;

        // The board's definition as Nazg has it, JSON; left out when empty or not JSON.
        std::vector<uint8_t> definition;

        // Layer 0: "row,column" -- QMK's matrix -- to the keycode's name.
        std::vector<std::pair<std::string, std::string>> keymap;

        // Seconds from the first reading, one per reading; and each connector's readings, at
        // [controller column * controller rows + controller row], as many as the times.
        std::vector<double>                times;
        std::vector<std::vector<uint16_t>> traces;
    };

    [[nodiscard]] std::string RecordingJson(const Recording& recording);
}
