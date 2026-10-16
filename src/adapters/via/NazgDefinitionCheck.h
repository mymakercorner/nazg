// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DefinitionCheck - whether a VIA definition is one Nazg can use, beyond parsing: the checks a
// definition written by someone else must pass before it is bundled -- the community bundle's
// seed and, later, its CI (via-registry.md, "Sharing: three levels"), and every file of VIA's
// own bundle in the via_bundle_contents test. The nazg-validate command runs it on files.
//
// A definition that parses can still fail a board: a key wired outside the matrix the board
// reports, a layout option that does not place, a menu control or condition Nazg would leave
// out. Each is a problem said in words, so a vendor or designer can fix the file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

namespace nazg::via
{
    // Larger than any definition VIA has -- 273 KB at most, 2026-10-11 -- and small enough that a
    // file written to hurt the parser is refused before it is read.
    inline constexpr size_t c_MaxDefinitionBytes = 1024 * 1024;

    struct DefinitionCheck
    {
        // The definition, when it parsed; nullopt when it did not, the reason in `problems`.
        std::optional<KeyboardDefinition> definition;

        // What is wrong, in words; empty when the definition is fine.
        std::vector<std::string> problems;

        // How many of the board's own menu controls the file holds -- VIA's color-palette aside,
        // which Nazg does not draw and no definition uses -- for a summary.
        size_t menuControls = 0;

        [[nodiscard]] bool Passed() const noexcept { return definition.has_value() && problems.empty(); }
    };

    // Every check on one file's bytes: its size, that it parses, that it names the board and
    // its VID:PID, that every key is inside the matrix, that every choice of every layout option
    // places, and that every menu control and condition is one Nazg reads.
    [[nodiscard]] DefinitionCheck CheckDefinition(const std::vector<uint8_t>& bytes);
}
