// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyLine - the controls of Keymap's key line (ui-design.md, "The key line") that other sections
// use too: toggles, popups under them, the modifier choices, and "Sent with" -- one control in
// Keymap, Macros and Tap Dance alike (Rico, 2026-10-10: Keymap's popup, over the four toggles
// Macros and Tap Dance had first).
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // A toggle in a popup or on the line: on, it looks pressed. True when clicked.
    bool ToggleButton(const char* label, bool on, bool enabled = true);

    // Opens `popup` under the item just drawn.
    void OpenPopupUnder(const char* popup);

    // The four modifiers' words by the host's names, in the USB HID order: Ctrl, Shift, Alt or
    // Option, Win, Cmd or Super.
    [[nodiscard]] std::array<const char*, 4> ModifierWords(ModifierNames names);

    // The four modifiers by the host's names and the side, editing `mods` -- one at least stays on
    // unless `none` is offered, as a "None" first. The new set when something was clicked.
    [[nodiscard]] std::optional<uint8_t> ModifierChoices(uint8_t mods, ModifierNames names, bool none);

    // "Sent with", and a button naming the modifiers -- "Ctrl Sft ▾", or "nothing" -- that opens
    // the choices under it, None first: back to the plain key in one click. For a basic key, or one
    // already sent with modifiers; anything else draws nothing. `key` is changed in place by a
    // click. True when it drew.
    bool DrawSentWith(Keycode& key, const LegendSettings& legends, QmkKeycodeVersion version);
}
