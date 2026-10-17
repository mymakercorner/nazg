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
// else, for a VIA board's file, the board loading it -- so a file from before QMK's renumbering
// loads right. A value that cannot be read is left out, and said.
//
// Pure: no device, so it tests with literals.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaMacro.h"
#include "model/NazgAltRepeatKey.h"
#include "model/NazgCombo.h"
#include "model/NazgKeyOverride.h"
#include "model/NazgKeycode.h"
#include "model/NazgQmkSettings.h"
#include "model/NazgTapDance.h"

namespace nazg::vial
{
    // A knob's turns on one layer; none for a turn the file has no value for.
    struct SavedKnob
    {
        std::optional<Keycode> clockwise;
        std::optional<Keycode> counterClockwise;
    };

    // A key of the keymap the file holds a value for that Nazg cannot read: "0x5f40".
    struct UnreadableKey
    {
        uint8_t     layer  = 0;
        uint8_t     row    = 0;
        uint8_t     column = 0;
        std::string text;
    };

    struct SavedLayout
    {
        std::optional<uint64_t> uid;   // the Vial keyboard UID; none for a VIA board (-1)
        int32_t                 vialProtocol = -1;   // the board's when saved; -1 for a VIA board
        int32_t                 viaProtocol  = -1;

        // Nazg's own key; none in vial-gui's files.
        std::optional<uint16_t> vendorId;
        std::optional<uint16_t> productId;

        // The keycode version of the macros' values: the board's, whichever wrote the file.
        qmk::KeycodeVersion keycodeVersion = qmk::c_LatestKeycodeVersion;

        // [layer][row][column], every cell of the matrix; none where the file has no value (-1) or
        // one Nazg cannot read.
        std::vector<std::vector<std::vector<std::optional<Keycode>>>> keymap;
        std::vector<std::vector<SavedKnob>>                           knobs;   // [layer][knob]
        std::optional<uint32_t>                                       layoutOptions;

        // One per slot, as many as the file has; none for a slot with something Nazg cannot read.
        std::vector<std::optional<via::MacroActions>> macros;
        std::vector<std::optional<TapDance>>          tapDances;
        std::vector<std::optional<Combo>>             combos;
        std::vector<std::optional<KeyOverride>>       keyOverrides;
        std::vector<std::optional<AltRepeatKey>>      altRepeatKeys;
        QmkSettings                                   qmkSettings;

        // What could not be read: the keymap's keys, and the rest in words -- "macro 3".
        std::vector<UnreadableKey> unreadableKeys;
        std::vector<std::string>   unreadable;

        // The matrix the keymap covers: its first layer's rows, and the longest of them.
        [[nodiscard]] size_t Rows() const noexcept;
        [[nodiscard]] size_t Columns() const noexcept;
    };

    // The file's text into the model, for a board on `boardVersion` -- macros' values are numbered
    // in it, and a VIA board's file is read with it. Throws std::runtime_error when the text is not
    // a saved layout.
    [[nodiscard]] SavedLayout ReadSavedLayout(std::string_view text, qmk::KeycodeVersion boardVersion);

    // The model into a file's text, as vial-gui writes one, Nazg's key added. Keycodes are encoded
    // with `layout.keycodeVersion`, spelled with vial-gui's names on a Vial board and as raw values
    // on a VIA board -- vial-gui would read names there with the wrong table.
    [[nodiscard]] std::string WriteSavedLayout(const SavedLayout& layout);
}
