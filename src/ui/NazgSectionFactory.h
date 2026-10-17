// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SectionFactory - a planned section (ui/NazgSectionPlan.h) made into the section that draws
// it, from what every section is made with. A kind with no section built yet gets a
// placeholder. The matrix view is not planned: Main.cpp adds it while Advanced tools is on.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "adapters/vial/NazgVialProtocol.h"
#include "library/NazgDefinitionChoice.h"
#include "model/NazgKeyHealth.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgSection.h"
#include "ui/NazgSectionPlan.h"

namespace nazg
{
    // The board a section belongs to, and the settings it follows. Everything referred to
    // outlives the sections: main() owns it, and a new load makes new sections.
    struct SectionContext
    {
        HidTransport&                            transport;
        std::string                              path;               // where the board was opened
        const DeviceIdentity&                    identity;           // its USB description
        Keyboard&                                keyboard;
        LegendSettings&                          legends;            // Macros may set the host layout
        const std::optional<vial::UnlockStatus>& lock;               // a Vial board's
        const bool&                              advancedTools;
        SafeMargins&                             safeMargins;        // the diagnostics', for the session
        const bool&                              moveToNextKey;      // Keymap's, after a pick
        bool&                                    hostLayoutChosen;   // Macros asks until it is
    };

    [[nodiscard]] std::unique_ptr<Section> MakeSection(PlannedSection planned, const SectionContext& context);
}
