// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMatrixDescription.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace nazg
{
    namespace
    {
        bool IsWired(const BoardKey& key)
        {
            return !key.geometry.decal;
        }

        // The keys on one row or one column.
        std::vector<size_t> Wiring(const BoardDescription& board, bool isRow, uint8_t line)
        {
            std::vector<size_t> keys;
            for (size_t index = 0; index < board.keys.size(); ++index)
            {
                const DefinitionKey& key = board.keys[index].geometry;
                if (IsWired(board.keys[index]) && (isRow ? key.row : key.column) == line)
                    keys.push_back(index);
            }
            return keys;
        }

        // The shortest links joining `keys`, centre to centre: a minimum spanning tree, grown
        // from the first by Prim's rule. A row or a column is a set of keys, not a sequence --
        // its number order is not where its trace runs -- so no order is drawn; on a regular
        // board this is the straight line through the row.
        std::vector<std::pair<size_t, size_t>> ShortestLinks(const BoardDescription& board, const std::vector<size_t>& keys)
        {
            std::vector<std::pair<size_t, size_t>> links;
            if (keys.size() < 2)
                return links;

            std::vector<std::pair<float, float>> centres;
            for (size_t key : keys)
                centres.push_back(KeyCentre(board.keys[key].geometry));

            const auto distance = [&](size_t a, size_t b) {
                const float dx = centres[a].first - centres[b].first;
                const float dy = centres[a].second - centres[b].second;
                return dx * dx + dy * dy;
            };

            // For each key not joined yet: the nearest joined one, and how far it is.
            std::vector<bool>   joined(keys.size(), false);
            std::vector<size_t> nearest(keys.size(), 0);
            std::vector<float>  gap(keys.size(), 0.0f);
            joined[0] = true;
            for (size_t i = 1; i < keys.size(); ++i)
                gap[i] = distance(0, i);

            for (size_t step = 1; step < keys.size(); ++step)
            {
                size_t next = 0;
                for (size_t i = 1; i < keys.size(); ++i)
                    if (!joined[i] && (next == 0 || gap[i] < gap[next]))
                        next = i;

                joined[next] = true;
                links.emplace_back(keys[nearest[next]], keys[next]);

                for (size_t i = 1; i < keys.size(); ++i)
                    if (!joined[i] && distance(next, i) < gap[i])
                    {
                        gap[i]     = distance(next, i);
                        nearest[i] = next;
                    }
            }
            return links;
        }

        void AddLinks(BoardDescription& board, const std::vector<size_t>& keys, uint8_t marks)
        {
            for (const auto& [from, to] : ShortestLinks(board, keys))
                board.lines.push_back({ { from, to }, marks });
        }

        bool HasKeyAt(const BoardDescription& board, uint8_t row, uint8_t column)
        {
            return std::any_of(board.keys.begin(), board.keys.end(), [&](const BoardKey& key) {
                return IsWired(key) && key.geometry.row == row && key.geometry.column == column;
            });
        }
    }

    MatrixCounts DescribeMatrix(BoardDescription& board, uint8_t rows, uint8_t columns, const MatrixFocus& focus)
    {
        MatrixCounts counts;

        // The rulers, rows first. A row or a column no key is on gets no place along its edge
        // and is not drawn (ui/NazgBoardView.h).
        for (uint8_t row = 0; row < rows; ++row)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Left;
            label.text = "R" + std::to_string(row);
            label.keys = Wiring(board, true, row);

            if (focus.row == row)
                label.marks |= Mark::Highlighted;
            else if (focus.column && !HasKeyAt(board, row, *focus.column))
                label.marks |= Mark::Struck;

            board.labels.push_back(std::move(label));
        }

        for (uint8_t column = 0; column < columns; ++column)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Top;
            label.text = "C" + std::to_string(column);
            label.keys = Wiring(board, false, column);

            if (focus.column == column)
                label.marks |= Mark::HighlightedSecond;
            else if (focus.row && !HasKeyAt(board, *focus.row, column))
                label.marks |= Mark::Struck;

            board.labels.push_back(std::move(label));
        }

        if (focus.IsEmpty())
            return counts;

        // The keys: the row's and the column's lit, the rest dimmed.
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            BoardKey& key = board.keys[index];
            if (!IsWired(key))
                continue;

            const bool onRow    = focus.row == key.geometry.row;
            const bool onColumn = focus.column == key.geometry.column;

            if (onRow)
            {
                key.marks |= Mark::Highlighted;
                ++counts.inRow;
            }
            if (onColumn)
            {
                key.marks |= Mark::HighlightedSecond;
                ++counts.inColumn;
            }
            if (onRow && onColumn && !counts.keyAt)
                counts.keyAt = index;
            if (!onRow && !onColumn)
                key.marks |= Mark::Dimmed;
        }

        // The wiring: which keys share the row, which the column.
        if (focus.row)
            AddLinks(board, Wiring(board, true, *focus.row), Mark::Highlighted);
        if (focus.column)
            AddLinks(board, Wiring(board, false, *focus.column), Mark::HighlightedSecond);

        return counts;
    }

    MatrixFocus FocusOfLabel(size_t label, uint8_t rows)
    {
        MatrixFocus focus;
        if (label < rows)
            focus.row = static_cast<uint8_t>(label);
        else
            focus.column = static_cast<uint8_t>(label - rows);
        return focus;
    }
}
