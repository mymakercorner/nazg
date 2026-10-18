// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaSavedLayout - the VIA app's saved layout, the .layout.json file, read into the model.
// docs/research_material/via-layout-files.md has the format; ui-design.md, "Saved layouts", what
// Nazg does with one -- the same review as a .vil's, through the same Load from file...
//
// A VIA file holds the keymap, every matrix cell of a layer in one flat list, the knobs and the
// macros, keycodes spelled with VIA's names (adapters/via/NazgViaKeycodeNames.h), macros as VIA's
// expressions: "Hello{KC_ENT}", {KC_LCTL,KC_C} a chord, {+KC_LSFT} and {-KC_LSFT}, {100} a wait.
// No protocol or keycode version: its names are read with the table VIA would use on the board.
//
// Read, never written (Rico, 2026-10-18): Nazg saves .vil. Where VIA loads what it cannot read as
// KC_NO -- its own hex among it -- Nazg reads the hex and leaves the rest out, said.
//
// Pure: no device, so it tests with literals.

#pragma once

#include <cstdint>
#include <string_view>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaKeycodeNames.h"
#include "model/NazgSavedLayout.h"

namespace nazg::via
{
    // A JSON object with VIA's layers and vendorProductId: one of its saved layouts.
    [[nodiscard]] bool IsSavedLayout(std::string_view text);

    // The file's text into the model, for a board of `rows` x `columns` whose keycodes are
    // `boardVersion`, names read with `table`. A layer whose length is not the matrix's leaves the
    // keymap out, said: its keys cannot be placed. Throws std::runtime_error when the text is not
    // one of VIA's saved layouts.
    [[nodiscard]] SavedLayout ReadSavedLayout(std::string_view text, NameTable table, qmk::KeycodeVersion boardVersion,
                                              uint8_t rows, uint8_t columns);
}
