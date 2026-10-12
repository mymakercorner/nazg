// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DefinitionPicker - which definition draws a VIA board, when Nazg has to ask.
//
// FIRST DRAFT, like the board view: each candidate drawn small beside its name and where
// it came from, since names are unreliable and a layout tells apart at a glance. With no
// candidate at all it says what to do instead: import the vendor's via.json. A file just
// imported for another keyboard is said too: Nazg matches a definition by VID:PID only, and
// a user must not take another keyboard's (Rico, 2026-10-12).
//
// It only draws and reports what was clicked; remembering the choice and loading the
// board are the caller's. ImGui only, no SDL.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "library/NazgDefinitionChoice.h"

namespace nazg
{
    struct DefinitionPickerAction
    {
        std::optional<size_t> picked;   // index into the candidates
        bool                  import = false;
        bool                  cancel = false;
    };

    // Into the current ImGui window. `current` marks the definition drawing the board now,
    // when there is one; `canCancel` offers to leave the picker without choosing; `notes`
    // are said above the candidates, as warnings.
    [[nodiscard]] DefinitionPickerAction DrawDefinitionPicker(const std::vector<DefinitionCandidate>& candidates,
                                                              const std::optional<DefinitionRef>&     current,
                                                              const DeviceIdentity&                   device,
                                                              bool                                    canCancel,
                                                              const std::vector<std::string>&         notes);

    // "user definition, imported from D:/d60b.json" or "official definition, from VIA".
    [[nodiscard]] std::string DescribeDefinitionSource(const DefinitionCandidate& candidate);
}
