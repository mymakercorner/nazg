// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DiagnosticsReport - Signal levels saved as one HTML file (ui-design.md, "Signal levels, fourth
// round", "The report, second draft"): what the section shows, the way it shows it, readable with
// nothing but a browser -- a report Nazg would be needed to read defeats its purpose (Rico,
// 2026-11-15). Every drawing is SVG made here, from the section's own numbers and the board's own
// key placement; a short script draws nothing, it only shows and hides -- the key clicked, Levels,
// Board / Controller, Light / Dark. Without it -- printed, or a mail program blocking it -- every
// drawing and every key's detail shows, one after the other. The readings ride along as JSON, a
// recording the tests replay.
//
// Tested keys are plain and untested ones hatched in the report, the problems tinted: a whole board
// green says little there (Rico, 2026-11-15). A report has no "now": a key's level at rest takes its
// place, and times are into the pass.
//
// No ImGui: it tests with recordings.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/leydenjar/NazgLeydenJarProtocol.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "model/NazgKeyHealth.h"

namespace nazg
{
    // One key judged, as Signal levels has it -- in the order of BoardHealth::keys.
    struct ReportKey
    {
        std::string name;   // what it prints, or its position
        uint8_t     row              = 0;   // QMK's matrix
        uint8_t     column           = 0;
        uint8_t     controllerRow    = 0;
        uint8_t     controllerColumn = 0;
        bool        isMapped         = false;
        uint16_t    threshold        = 0;
        uint8_t     bin              = 0;
    };

    // Every key the board draws, decals left out: its shape, and the key judged there if any.
    struct ReportShape
    {
        via::DefinitionKey    geometry;
        std::optional<size_t> key;      // into DiagnosticsReport::keys
        std::string           legend;   // what the keycap says, for a key not judged
    };

    struct DiagnosticsReport
    {
        std::string title;     // the board's name
        std::string usb;       // what the board says over USB, in a line
        std::string savedBy;   // "Nazg 0.1.0-test.8"
        std::string savedAt;   // when, as people write it

        leydenjar::ControllerInfo info;
        LevelScale                scale;
        SafeMargins               margins;   // what the verdicts were judged by

        std::vector<ReportKey>   keys;
        std::vector<ReportShape> shapes;
        BoardHealth              health;

        // Each connector's readings at [controller column * controller rows + controller row], and
        // their times in seconds from the first.
        std::vector<std::vector<uint16_t>> traces;
        std::vector<double>                times;

        // The same readings as a recording (adapters/leydenjar/NazgLeydenJarRecording.h), embedded.
        std::string readings;
    };

    [[nodiscard]] std::string DiagnosticsReportHtml(const DiagnosticsReport& report);

    // A report's embedded recording, or empty when it has none: what the tests replay.
    [[nodiscard]] std::string ReadingsOfReport(const std::string& html);
}
