// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMatrixDescription.h"

#include <algorithm>
#include <cmath>
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

        // One key per position, the first in the definition. Keys wired in parallel can sit far
        // apart, and a ruler label is centred on its key nearest the edge: without this, a
        // parallel key higher up or further left could take its row's or column's label away.
        std::vector<size_t> OnePerPosition(const BoardDescription& board, const std::vector<size_t>& keys)
        {
            std::vector<size_t> kept;
            for (size_t key : keys)
            {
                const DefinitionKey& geometry = board.keys[key].geometry;
                const bool isRepeat = std::any_of(kept.begin(), kept.end(), [&](size_t other) {
                    return board.keys[other].geometry.row == geometry.row &&
                           board.keys[other].geometry.column == geometry.column;
                });
                if (!isRepeat)
                    kept.push_back(key);
            }
            return kept;
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

    void AddReading(MatrixLive& live, const SwitchMatrixState& reading)
    {
        if (live.seen.bytes.size() != reading.bytes.size())
        {
            live.seen       = reading;
            live.seen.bytes.assign(reading.bytes.size(), 0x00);
        }

        for (size_t index = 0; index < reading.bytes.size(); ++index)
            live.seen.bytes[index] |= reading.bytes[index];
        live.pressed = reading;
    }

    MatrixCounts DescribeMatrix(BoardDescription& board, uint8_t rows, uint8_t columns, const MatrixFocus& focus,
                                const MatrixLive* live)
    {
        MatrixCounts counts;

        const auto isSeen = [&](size_t key) {
            return live != nullptr && live->seen.IsPressed(board.keys[key].geometry.row, board.keys[key].geometry.column);
        };
        const auto allSeen = [&](const std::vector<size_t>& keys) {
            return live != nullptr && !keys.empty() && std::all_of(keys.begin(), keys.end(), isSeen);
        };

        // The rulers, rows first. A row or a column no key is on gets no place along its edge
        // and is not drawn (ui/NazgBoardView.h). In the live test, one whose keys have all
        // been seen is ticked off.
        for (uint8_t row = 0; row < rows; ++row)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Left;
            label.text = "R" + std::to_string(row);
            label.keys = OnePerPosition(board, Wiring(board, true, row));

            if (focus.row == row)
                label.marks |= Mark::Highlighted;
            else if (focus.column && !HasKeyAt(board, row, *focus.column))
                label.marks |= Mark::Struck;
            if (allSeen(label.keys))
                label.marks |= Mark::Checked;

            board.labels.push_back(std::move(label));
        }

        for (uint8_t column = 0; column < columns; ++column)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Top;
            label.text = "C" + std::to_string(column);
            label.keys = OnePerPosition(board, Wiring(board, false, column));

            if (focus.column == column)
                label.marks |= Mark::HighlightedSecond;
            else if (focus.row && !HasKeyAt(board, *focus.row, column))
                label.marks |= Mark::Struck;
            if (allSeen(label.keys))
                label.marks |= Mark::Checked;

            board.labels.push_back(std::move(label));
        }

        // The keys: in the live test, seen and pressed; with a focus, the row's and the
        // column's lit and the rest dimmed.
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            BoardKey& key = board.keys[index];
            if (!IsWired(key))
                continue;

            // One outside the matrix can never be seen, so it is not waited for.
            if (key.geometry.row < rows && key.geometry.column < columns)
                ++counts.keys;
            if (isSeen(index))
            {
                key.marks |= Mark::Checked;
                ++counts.seenKeys;
            }
            if (live != nullptr && live->pressed.IsPressed(key.geometry.row, key.geometry.column))
                key.marks |= Mark::Pressed;

            if (focus.IsEmpty())
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
            if (onRow && onColumn)
                counts.keysAt.push_back(index);
            if (!onRow && !onColumn)
                key.marks |= Mark::Dimmed;
        }

        // Pressed where the layout drawn has no key: another layout choice's key, or ghosting.
        if (live != nullptr)
            for (uint8_t row = 0; row < live->pressed.rows; ++row)
                for (uint8_t column = 0; column < live->pressed.columns; ++column)
                    if (live->pressed.IsPressed(row, column) && !HasKeyAt(board, row, column))
                        counts.pressedWithoutKey.emplace_back(row, column);

        // The wiring: which keys share the row, which the column.
        if (focus.row)
            AddLinks(board, Wiring(board, true, *focus.row), Mark::Highlighted);
        if (focus.column)
            AddLinks(board, Wiring(board, false, *focus.column), Mark::HighlightedSecond);

        return counts;
    }

    MatrixFindings FindInDefinition(const BoardDescription& board, uint8_t rows, uint8_t columns)
    {
        MatrixFindings findings;

        // Placed keys come from the same numbers moved by the same offset, so "the same
        // place" needs only a little slack.
        const auto same = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
        const auto onTop = [&](const DefinitionKey& a, const DefinitionKey& b) {
            return same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height) &&
                   same(a.rotation, b.rotation) && same(a.rotationX, b.rotationX) && same(a.rotationY, b.rotationY);
        };

        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const BoardKey& key = board.keys[index];
            if (!IsWired(key))
                continue;

            if (key.geometry.row >= rows || key.geometry.column >= columns)
                findings.outside.push_back(index);

            for (size_t later = index + 1; later < board.keys.size(); ++later)
                if (IsWired(board.keys[later]) && onTop(key.geometry, board.keys[later].geometry))
                    findings.stacked.emplace_back(index, later);
        }
        return findings;
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
