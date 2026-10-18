// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VialSavedLayout - Vial's saved layout, the .vil file, read into the model and written from it.
// docs/research_material/vil-files.md has the format; ui-design.md, "Saved layouts", what Nazg
// does with one.
//
// A .vil is vial-gui's JSON: the keymap of every matrix cell, the knobs, layout options, macros,
// tap dances, combos, key overrides, alt repeat keys and QMK settings, keycodes spelled as vial-gui
// names them (adapters/vial/NazgVialKeycodeNames.h). Nazg writes what vial-gui writes, so Vial
// loads Nazg's files as its own, and adds one key vial-gui ignores, "nazg": the board's VID:PID --
// a VIA board's file has no UID to say which board it came from -- and the keycode version its raw
// values are numbered in.
//
// What a value in the file means depends on the firmware that saved it: vial-gui names keys from
// its v5 or v6 table by the file's Vial protocol, and replays a raw value as it is. Nazg reads a
// raw value with the keycodes of the firmware that saved it -- Nazg's key, else the Vial protocol,
// else the VIA protocol -- so a file from before QMK's renumbering loads right. A value that cannot
// be read is left out, and said.
//
// Pure: no device, so it tests with literals.

#pragma once

#include <string>
#include <string_view>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "model/NazgSavedLayout.h"

namespace nazg::vial
{
    // The file's text into the model, for a board on `boardVersion` -- macros' values are numbered
    // in it, and a VIA board's file is read with it. Throws std::runtime_error when the text is not
    // a saved layout.
    [[nodiscard]] SavedLayout ReadSavedLayout(std::string_view text, qmk::KeycodeVersion boardVersion);

    // The model into a file's text, as vial-gui writes one, Nazg's key added. Keycodes are encoded
    // with `layout.keycodeVersion`, spelled with vial-gui's names on a Vial board and as raw values
    // on a VIA board: vial-gui would read names there with its v5 table, before the renumbering --
    // right for the VIA 9 boards it opens, wrong for any later one.
    [[nodiscard]] std::string WriteSavedLayout(const SavedLayout& layout);
}
