// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Macro - a macro as Nazg edits it (ui-design.md, "The Macros section"): a flat list of steps, as
// the board stores them -- text, a key tapped (with the modifiers sent with it), a key pressed, a
// key released, a wait. A press and a release are steps of their own, paired only for reading.
//
// Protocol-neutral: ui/NazgMacroSteps.h turns it into a board's actions and back, text through the
// host layout.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/NazgKeycode.h"

namespace nazg
{
    struct MacroStep
    {
        enum class Kind : uint8_t
        {
            Text,      // `text`, UTF-8, typed with the host layout
            Key,       // `key` tapped -- a ModifiedKey for one sent with modifiers
            Press,     // `key` pressed, and kept down
            Release,   // `key` released
            Wait,      // `milliseconds`
        };

        Kind        kind = Kind::Text;
        std::string text;
        Keycode     key          = NamedKey{ "KC_NO" };
        uint32_t    milliseconds = 0;

        bool operator==(const MacroStep&) const = default;
    };

    using Macro = std::vector<MacroStep>;

    // Which release goes with which press, for reading: a release pairs with the last press of the
    // same key not yet released. partner[i] is the other step's index, or the step's own when it is
    // alone -- a press never released, a release never pressed -- or not a press or release.
    [[nodiscard]] std::vector<size_t> PairPressesAndReleases(const Macro& macro);
}
