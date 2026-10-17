// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SavedLayoutTransfer - a saved layout (.vil, adapters/vial/NazgVialSavedLayout.h) and the open
// board: what the board holds, what a file would change on it, and the writing of it. ui-design.md,
// "Saved layouts".
//
// Reading takes the keymap, the knobs and layout options from the Keyboard -- the sections keep
// them as the board stores them after each write -- and reads the rest: macros, tap dances,
// combos, key overrides, alt repeat keys and QMK settings, about a second on the Model F.
//
// Comparing is pure: what differs, slot by slot, and what is left out and why -- a keymap of
// another matrix, slots past the board's count, macros too long for its buffer. Writing writes
// only what differs and reads each back, as the sections do: what the board did not keep is said.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "adapters/vial/NazgVialSavedLayout.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // What the board holds, as a saved layout, and what writing macros back needs.
    struct BoardContents
    {
        vial::SavedLayout    layout;
        std::vector<uint8_t> macroBytes;   // the buffer as read, up to the last macro's end
        uint16_t             macroBufferSize = 0;
    };

    // `vendorId` and `productId`: the board's, written into the file Nazg saves.
    [[nodiscard]] Task<BoardContents> ReadBoardContents(vial::Protocol& protocol, const Keyboard& keyboard,
                                                        uint16_t vendorId, uint16_t productId);

    struct KeyChange
    {
        uint8_t layer  = 0;
        uint8_t row    = 0;
        uint8_t column = 0;
        Keycode now;
        Keycode then;
    };

    struct KnobChange
    {
        uint8_t layer     = 0;
        uint8_t knob      = 0;
        bool    clockwise = false;
        Keycode now;
        Keycode then;
    };

    // What a file changes on the board.
    struct LayoutChanges
    {
        bool                    keymapFits = true;   // same matrix: the keymap, knobs and layout options load
        std::vector<KeyChange>  keys;                 // the keys a definition key uses, by layer
        std::vector<KnobChange> knobs;
        std::optional<uint32_t> layoutOptions;        // the file's, when it differs
        std::vector<size_t>     macros;               // slots that differ
        std::vector<size_t>     tapDances;
        std::vector<size_t>     combos;
        std::vector<size_t>     keyOverrides;
        std::vector<size_t>     altRepeatKeys;
        std::vector<uint16_t>   qmkSettings;          // qsids that differ

        // What the file holds that is not loaded, each said in the review: "the file's keymap has
        // 8 × 16 cells, this keyboard's 8 × 18".
        std::vector<std::string> leftOut;

        // How many writes: a key, a turn, a slot, a setting each; the macros one.
        [[nodiscard]] size_t Count() const noexcept;
        [[nodiscard]] bool   IsEmpty() const noexcept { return Count() == 0; }

        // A locked Vial board refuses the macro buffer, and stores QK_BOOT as nothing: the writes need
        // it unlocked (via-vial-commands.md, "What the lock gates").
        [[nodiscard]] bool NeedsUnlock(const vial::SavedLayout& file) const;
    };

    [[nodiscard]] LayoutChanges CompareWithBoard(const BoardContents& board, const vial::SavedLayout& file,
                                                 const Keyboard& keyboard);

    // How a write goes: done of total, and what the board did not keep.
    struct WriteProgress
    {
        size_t                   done  = 0;
        size_t                   total = 0;
        std::vector<std::string> notKept;
    };

    // Writes every change, each read back. Throws when the board goes away; what was written stays.
    [[nodiscard]] Task<void> WriteChanges(vial::Protocol& protocol, const Keyboard& keyboard, const BoardContents& board,
                                          const vial::SavedLayout& file, const LayoutChanges& changes,
                                          WriteProgress& progress);
}
