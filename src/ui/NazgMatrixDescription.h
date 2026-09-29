// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The matrix view's board: which row and which column of the switch matrix each key sits on
// (ui-design.md, "The matrix view"). A row ruler on the left and a column ruler on top; a
// row or a column in focus lights, on the board and in its ruler, with its keys joined by the
// shortest links -- a row is a set, and neither its column numbers nor the screen say where
// its trace runs -- and everything else dims. With a row in focus, the columns with no key in it are struck
// through in the column ruler, and the other way round.
//
// Definition data only: no protocol, no board needed. The keys are the ones the board is
// drawn with, at its layout choice; keys of the other choices are not drawn.
//
// Pure, as ui/NazgBoardDescription.h is, so it tests with literals. ui/NazgMatrixView.h
// puts it on screen.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ui/NazgBoardDescription.h"

namespace nazg
{
    // What is in focus: a row, a column, or both -- a key's.
    struct MatrixFocus
    {
        std::optional<uint8_t> row;
        std::optional<uint8_t> column;

        [[nodiscard]] bool IsEmpty() const noexcept { return !row && !column; }

        friend bool operator==(const MatrixFocus& a, const MatrixFocus& b)
        {
            return a.row == b.row && a.column == b.column;
        }
        friend bool operator!=(const MatrixFocus& a, const MatrixFocus& b) { return !(a == b); }
    };

    // What the focus covers, for the panel to say.
    struct MatrixCounts
    {
        size_t                inRow    = 0;   // keys wired to the row in focus
        size_t                inColumn = 0;   // keys wired to the column in focus
        std::optional<size_t> keyAt;          // the key where both meet, if any: into board.keys
    };

    // Adds the rulers, the focus's marks and its wiring to `board`, which holds the keys as
    // DescribeKeyboard() gives them and no labels yet. The rulers are one label per row, R0
    // on, then one per column, C0 on -- `rows` and `columns` being the definition's matrix
    // size -- so a label's index says what it is (FocusOfLabel()). Decals are never
    // counted, wired or marked.
    MatrixCounts DescribeMatrix(BoardDescription& board, uint8_t rows, uint8_t columns, const MatrixFocus& focus);

    // The focus a ruler label stands for, by its index in the labels DescribeMatrix() added.
    [[nodiscard]] MatrixFocus FocusOfLabel(size_t label, uint8_t rows);
}
