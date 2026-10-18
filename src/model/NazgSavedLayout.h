// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SavedLayout - a saved layout as Nazg reads and writes one (ui-design.md, "Saved layouts"):
// everything a user edits on a board, kept in a file. Two formats fill it -- Vial's .vil
// (adapters/vial/NazgVialSavedLayout.h), which Nazg also writes, and VIA's .layout.json
// (adapters/via/NazgViaSavedLayout.h), which holds the keymap, knobs and macros only -- and one
// review compares it with the board and writes what differs (board/NazgSavedLayoutTransfer.h).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaMacro.h"
#include "model/NazgAltRepeatKey.h"
#include "model/NazgCombo.h"
#include "model/NazgKeyOverride.h"
#include "model/NazgKeycode.h"
#include "model/NazgQmkSettings.h"
#include "model/NazgTapDance.h"

namespace nazg
{
    enum class SavedLayoutFormat : uint8_t
    {
        Vil,   // Vial's, or Nazg's own
        Via,   // the VIA app's .layout.json
    };

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
        SavedLayoutFormat format = SavedLayoutFormat::Vil;

        std::optional<uint64_t> uid;   // the Vial keyboard UID; none for a VIA board (-1) and in VIA's files
        int32_t                 vialProtocol = -1;   // the board's when saved; -1 for a VIA board
        int32_t                 viaProtocol  = -1;

        // Nazg's own key in a .vil, none in vial-gui's files; VIA's vendorProductId in its files.
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
}
