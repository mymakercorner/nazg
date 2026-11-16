// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HostNames - QMK's international keycode names, the ones keymap_german.h and its siblings give:
// DE_SLSH for LSFT(KC_7), FR_A for KC_Q, CH_EURO for RALT(KC_E) (tester-feedback.md, 19). A board
// stores only the keycode; the name is the host layout's, so a name means something only within
// its layout -- FR_A is KC_Q on French and KC_A on Canadian French. The names, per layout, are
// committed data in NazgHostNameTable.cpp.
//
// Two uses: the name a keycode has on the host layout chosen, for hover; and a name typed in the
// Any entry, read on that layout first, else on every layout that has it.

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // One of QMK's names and its keycode as a QMK keymap writes it, in basic keycodes only:
    // { "DE_SLSH", "LSFT(KC_7)" }.
    struct HostName
    {
        std::string_view name;
        std::string_view expression;
    };

    // A layout's names, by its id ("german"); none for an id the table does not have.
    [[nodiscard]] std::span<const HostName> HostNamesOf(std::string_view layoutId) noexcept;

    // The name `layout` gives `keycode`, compared as keycodes, not as text; empty when it has none.
    [[nodiscard]] std::string_view HostNameOf(const Keycode& keycode, const HostLayout& layout);

    // A name typed, in any case: the keycode it stands for, or why it was not read.
    struct HostNameReading
    {
        std::optional<Keycode> keycode;
        std::string            note;            // a name of another layout, read; or why not
        bool                   known = false;   // a name some layout has, read or not
    };

    // On `chosen` first; else on every layout that has the name, read when they all agree -- the
    // note naming them and `chosen` -- refused when they differ, the note naming each.
    [[nodiscard]] HostNameReading ReadHostName(std::string_view typed, const HostLayout& chosen,
                                               qmk::KeycodeVersion version);
}
