// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The matrix view's board: rulers, the focus's marks, its wiring and the positions no key
// uses. What it means is tested; how it looks is the renderer's.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgMatrixDescription.h"

#include "TestSupport.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

using nazg::BoardDescription;
using nazg::BoardEdge;
using nazg::BoardKey;
using nazg::DescribeMatrix;
using nazg::MatrixCounts;
using nazg::MatrixFocus;
namespace Mark = nazg::Mark;

namespace
{
    constexpr uint8_t c_Rows    = 3;
    constexpr uint8_t c_Columns = 4;

    BoardKey Key(float x, float y, uint8_t row, uint8_t column, bool decal = false)
    {
        BoardKey key;
        key.geometry.x      = x;
        key.geometry.y      = y;
        key.geometry.row    = row;
        key.geometry.column = column;
        key.geometry.decal  = decal;
        return key;
    }

    // A 3 x 4 matrix, wired untidily:
    //   row 0: columns 1, 0, 2, 3 from left to right
    //   row 1: columns 0, 1, 3 -- no key at column 2
    //   row 2: no key at all
    // and a decal on row 0, column 2, which is never wired.
    BoardDescription Board()
    {
        BoardDescription board;
        board.keys = {
            Key(0, 0, 0, 1),         // 0
            Key(1, 0, 0, 0),         // 1
            Key(2, 0, 0, 2),         // 2
            Key(3, 0, 0, 3),         // 3
            Key(0, 1, 1, 0),         // 4
            Key(1, 1, 1, 1),         // 5
            Key(2, 1, 1, 3),         // 6
            Key(5, 0, 0, 2, true),   // 7, the decal
        };
        return board;
    }

    MatrixFocus Row(uint8_t row)
    {
        MatrixFocus focus;
        focus.row = row;
        return focus;
    }

    MatrixFocus At(uint8_t row, uint8_t column)
    {
        MatrixFocus focus;
        focus.row    = row;
        focus.column = column;
        return focus;
    }

    const nazg::EdgeLabel& RowLabel(const BoardDescription& board, uint8_t row)
    {
        return board.labels[row];
    }

    const nazg::EdgeLabel& ColumnLabel(const BoardDescription& board, uint8_t column)
    {
        return board.labels[c_Rows + column];
    }

    // The links drawn with `marks`, each as the pair of keys it joins, smaller index first.
    using LinkSet = std::set<std::pair<size_t, size_t>>;

    LinkSet LinksOf(const BoardDescription& board, uint8_t marks)
    {
        LinkSet links;
        for (const nazg::BoardLine& line : board.lines)
            if (line.marks == marks && line.keys.size() == 2)
                links.emplace(std::min(line.keys[0], line.keys[1]), std::max(line.keys[0], line.keys[1]));
            else if (line.marks == marks)
                links.emplace(SIZE_MAX, SIZE_MAX);   // not a link: fails any comparison
        return links;
    }

    void TestRulers()
    {
        std::printf("rulers\n");

        BoardDescription board = Board();
        (void)DescribeMatrix(board, c_Rows, c_Columns, {});

        Check(board.labels.size() == c_Rows + c_Columns, "one label per row, then one per column");
        Check(RowLabel(board, 0).text == "R0" && RowLabel(board, 0).edge == BoardEdge::Left, "rows on the left, R0 on");
        Check(ColumnLabel(board, 3).text == "C3" && ColumnLabel(board, 3).edge == BoardEdge::Top,
              "columns on top, C0 on");

        Check(RowLabel(board, 0).keys == std::vector<size_t>{ 0, 1, 2, 3 },
              "a row label sits by its keys, the decal left out");
        Check(RowLabel(board, 2).keys.empty(), "a row no key is on has nowhere to sit");
        Check(ColumnLabel(board, 2).keys == std::vector<size_t>{ 2 }, "column 2 has one key");

        bool unmarked = board.lines.empty();
        for (const BoardKey& key : board.keys)
            unmarked &= key.marks == 0;
        for (const nazg::EdgeLabel& label : board.labels)
            unmarked &= label.marks == 0;
        Check(unmarked, "nothing in focus: nothing lit, dimmed, struck or wired");

        Check(nazg::FocusOfLabel(0, c_Rows) == Row(0), "label 0 is row 0");
        MatrixFocus column0;
        column0.column = 0;
        Check(nazg::FocusOfLabel(c_Rows, c_Rows) == column0, "the label after the last row is column 0");
    }

    void TestRowInFocus()
    {
        std::printf("a row in focus\n");

        BoardDescription   board  = Board();
        const MatrixCounts counts = DescribeMatrix(board, c_Rows, c_Columns, Row(1));

        Check(counts.inRow == 3 && counts.inColumn == 0 && !counts.keyAt, "row 1 wires three keys");

        Check((board.keys[4].marks & Mark::Highlighted) != 0 && (board.keys[6].marks & Mark::Highlighted) != 0,
              "its keys are lit");
        Check(board.keys[0].marks == Mark::Dimmed, "the others are dimmed");
        Check(board.keys[7].marks == 0, "the decal is left alone");

        Check((RowLabel(board, 1).marks & Mark::Highlighted) != 0, "its ruler label is lit");
        Check((ColumnLabel(board, 2).marks & Mark::Struck) != 0, "column 2, with no key on row 1, is struck through");
        Check(ColumnLabel(board, 0).marks == 0 && ColumnLabel(board, 3).marks == 0, "the columns it uses are not");
        Check((RowLabel(board, 0).marks & Mark::Struck) == 0, "no row is struck with only a row in focus");

        Check(LinksOf(board, Mark::Highlighted) == LinkSet{ { 4, 5 }, { 5, 6 } }, "its keys joined, neighbour to neighbour");
    }

    void TestShortestLinks()
    {
        std::printf("shortest links\n");

        BoardDescription board = Board();
        (void)DescribeMatrix(board, c_Rows, c_Columns, Row(0));

        // Column 0 is second from the left: joined by where the keys are, not by number.
        Check(LinksOf(board, Mark::Highlighted) == LinkSet{ { 0, 1 }, { 1, 2 }, { 2, 3 } },
              "a row's keys are joined as they sit, whatever their column numbers");

        // A Model F's column: rows numbered out of screen order, 0 at the top, then 2, 3, 1.
        BoardDescription modelF;
        modelF.keys = { Key(0, 0, 0, 0), Key(0, 3, 1, 0), Key(0, 1, 2, 0), Key(0, 2, 3, 0) };
        (void)DescribeMatrix(modelF, 4, 1, MatrixFocus{ std::nullopt, 0 });

        Check(LinksOf(modelF, Mark::HighlightedSecond) == LinkSet{ { 0, 2 }, { 2, 3 }, { 1, 3 } },
              "rows out of screen order: no zigzag, each key joined to its neighbour");
    }

    void TestKeyInFocus()
    {
        std::printf("a key in focus\n");

        BoardDescription   board  = Board();
        const MatrixCounts counts = DescribeMatrix(board, c_Rows, c_Columns, At(1, 3));

        Check(counts.inRow == 3 && counts.inColumn == 2, "row 1 wires three keys, column 3 two");
        Check(counts.keyAt == size_t{ 6 }, "the key where they meet");

        Check(board.keys[6].marks == (Mark::Highlighted | Mark::HighlightedSecond), "it is lit both ways");
        Check(board.keys[3].marks == Mark::HighlightedSecond, "the rest of the column is lit as the column");
        Check(board.keys[2].marks == Mark::Dimmed, "a key on neither is dimmed");

        Check((ColumnLabel(board, 3).marks & Mark::HighlightedSecond) != 0, "the column's label is lit as the column");
        Check((ColumnLabel(board, 2).marks & Mark::Struck) != 0, "column 2 is struck, for row 1");
        Check((RowLabel(board, 2).marks & Mark::Struck) != 0, "row 2 is struck, for column 3");
        Check((RowLabel(board, 0).marks & Mark::Struck) == 0, "row 0, which has a key on column 3, is not");

        Check(LinksOf(board, Mark::HighlightedSecond) == LinkSet{ { 3, 6 } }, "the column's keys joined too, as the column");
    }

    void TestNoKeyAtFocus()
    {
        std::printf("a position with no key\n");

        BoardDescription   board  = Board();
        const MatrixCounts counts = DescribeMatrix(board, c_Rows, c_Columns, At(1, 2));

        Check(!counts.keyAt, "no key at row 1, column 2 -- the decal there does not count");
        Check(counts.inRow == 3 && counts.inColumn == 1, "the row and the column are still counted");
    }
}

namespace
{
    // One byte a row: column 0 is bit 0.
    nazg::SwitchMatrixState Reading(std::vector<uint8_t> rows)
    {
        nazg::SwitchMatrixState state;
        state.rows    = c_Rows;
        state.columns = c_Columns;
        state.bytes   = std::move(rows);
        return state;
    }

    void TestLiveTest()
    {
        std::printf("live test\n");

        nazg::MatrixLive live;

        // Row 1 held down: columns 0, 1 and 3.
        nazg::AddReading(live, Reading({ 0x00, 0x0B, 0x00 }));

        BoardDescription   board  = Board();
        const MatrixCounts counts = DescribeMatrix(board, c_Rows, c_Columns, {}, &live);

        Check(counts.keys == 7 && counts.seenKeys == 3, "three of the seven keys seen");
        Check(board.keys[4].marks == (Mark::Checked | Mark::Pressed), "a key down now is seen and pressed");
        Check(board.keys[0].marks == 0, "a key not seen is left alone");
        Check(board.keys[7].marks == 0, "the decal on a position never pressed is left alone");
        Check((RowLabel(board, 1).marks & Mark::Checked) != 0, "row 1 is complete");
        Check((ColumnLabel(board, 0).marks & Mark::Checked) == 0, "column 0 is not: its row 0 key is unseen");
        Check(counts.pressedWithoutKey.empty(), "every position pressed has a key");

        // Released, then row 0 held, and a position with no key: ghosting, or another layout.
        nazg::AddReading(live, Reading({ 0x0F, 0x00, 0x04 }));

        BoardDescription   after       = Board();
        const MatrixCounts afterCounts = DescribeMatrix(after, c_Rows, c_Columns, {}, &live);

        Check(afterCounts.seenKeys == 7, "every key seen, over two readings");
        Check(after.keys[4].marks == Mark::Checked, "released, it stays seen");
        Check((ColumnLabel(after, 0).marks & Mark::Checked) != 0 && (ColumnLabel(after, 2).marks & Mark::Checked) != 0,
              "each column complete");
        Check((RowLabel(after, 2).marks & Mark::Checked) == 0, "a row with no key is never complete");
        Check(afterCounts.pressedWithoutKey == std::vector<std::pair<uint8_t, uint8_t>>{ { 2, 2 } },
              "row 2, column 2 is pressed where no key is drawn");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRulers();
    TestRowInFocus();
    TestShortestLinks();
    TestKeyInFocus();
    TestNoKeyAtFocus();
    TestLiveTest();

    return TestResult();
}
