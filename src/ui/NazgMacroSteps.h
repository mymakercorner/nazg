// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// MacroSteps - a macro's steps (model/NazgMacro.h) from the actions a board stores
// (adapters/via/NazgViaMacro.h) and back, for one board and one host layout.
//
// Text is typed with the host layout (ui/NazgHostTyping.h) and stored compactly: a keystroke the
// firmware's US table reaches is one character byte -- the US character on that key, 'q' for a
// on French; any other keystroke is an action -- AltGr ones as one 16-bit tap on Vial 5 and later,
// as Right Alt pressed around the tap on the others. Reading undoes it, so text written by another
// app shows what it types with this host layout ("l'eau" from VIA reads "léau" on US
// International): nothing is converted until it is written again.
//
// A key sent with modifiers is one step: one 16-bit tap on Vial 5 and later; elsewhere the
// modifiers pressed, the key tapped, the modifiers released -- VIA's chord, which reading folds
// back into one step.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaMacro.h"
#include "model/NazgMacro.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    struct MacroContext
    {
        MacroFormat       format;
        QmkKeycodeVersion version;
        const HostLayout& host;
    };

    // A step this board, or this host layout, cannot store.
    struct MacroProblem
    {
        size_t      step = 0;
        std::string what;   // "é cannot be typed with the US layout"
    };

    struct MacroWriting
    {
        MacroActions              actions;
        std::vector<MacroProblem> problems;   // none: the actions are the macro
    };

    [[nodiscard]] Macro        StepsOf(const MacroActions& actions, const MacroContext& context);
    [[nodiscard]] MacroWriting ActionsOf(const Macro& macro, const MacroContext& context);

    // The bytes the macro takes in the buffer, its 0 included -- what the actions that can be stored
    // cost.
    [[nodiscard]] size_t BytesOf(const MacroActions& actions, MacroFormat format) noexcept;

    // The macro in VIA's script syntax, read-only, for those who know it (ui-design.md, "The macro's
    // line"): text as it is, '{' as "\{"; {KC_ENT} a key, {KC_LGUI,KC_R} one sent with modifiers,
    // {+KC_LALT} and {-KC_LALT} a press and a release, {300} a wait. A key VIA cannot hold is
    // written in QMK's words: {MO(1)}.
    [[nodiscard]] std::string ViaScriptOf(const Macro& macro);
}
