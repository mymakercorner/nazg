// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMatrixView.h"

#include "imgui.h"

#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    MatrixView::MatrixView(const Keyboard& keyboard, const std::string& hostLayoutId)
        : m_Keyboard(keyboard), m_HostLayoutId(hostLayoutId)
    {
    }

    void MatrixView::DescribeBoard(BoardDescription& board)
    {
        // Layer 0's legends, so each key is recognised; the keymap itself is not the point here.
        const HostLayout* found  = FindHostLayout(m_HostLayoutId);
        const HostLayout& layout = found != nullptr ? *found : UsHostLayout();

        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;

            const KeycapLegend legend        = LegendFor(m_Keyboard.KeycodeFor(key.geometry, 0), layout);
            key[LegendSlot::MiddleLeft].text = legend.primary;
            key[LegendSlot::TopLeft].text    = legend.secondary;
        }

        m_Counts = DescribeMatrix(board, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns, Focus());
        m_KeyAt  = m_Counts.keyAt ? FormatKeycode(m_Keyboard.KeycodeFor(board.keys[*m_Counts.keyAt].geometry, 0))
                                  : std::string();
    }

    void MatrixView::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        MatrixFocus hovered;
        if (events.hoveredKey)
        {
            const DefinitionKey& key = board.keys[*events.hoveredKey].geometry;
            hovered.row              = key.row;
            hovered.column           = key.column;
        }
        else if (events.hoveredLabel)
        {
            hovered = FocusOfLabel(*events.hoveredLabel, m_Keyboard.definition.matrixRows);
        }
        m_Hovered = hovered;

        // A click pins what it is on; on what is pinned already, it lets go.
        if (events.clickedKey || events.clickedLabel)
            m_Pinned = m_Pinned == hovered ? std::nullopt : std::optional<MatrixFocus>(hovered);
    }

    void MatrixView::DrawPanel()
    {
        const MatrixFocus focus = Focus();

        if (focus.IsEmpty())
        {
            ImGui::TextUnformatted("Hover a key, or a row or column label. Click to pin.");
        }
        else if (focus.row && focus.column)
        {
            if (m_Counts.keyAt)
                ImGui::Text("%s at row %d, column %d.", m_KeyAt.c_str(), *focus.row, *focus.column);
            else
                ImGui::Text("No key at row %d, column %d.", *focus.row, *focus.column);
            ImGui::Text("Row %d wires %zu keys, column %d wires %zu.", *focus.row, m_Counts.inRow, *focus.column,
                        m_Counts.inColumn);
        }
        else if (focus.row)
        {
            ImGui::Text("Row %d: %zu keys. Columns with no key in it are struck through.", *focus.row, m_Counts.inRow);
        }
        else
        {
            ImGui::Text("Column %d: %zu keys. Rows with no key in it are struck through.", *focus.column,
                        m_Counts.inColumn);
        }

        if (m_Pinned)
            ColouredText(PanelColour::Muted, "Pinned: click it again to let go.");

        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "Matrix: %d rows, %d columns. Layer 0's legends.",
                     m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);
        if (!m_Keyboard.layoutSelection.empty())
            ColouredText(PanelColour::Muted, "Drawn with the board's layout options; keys of the other choices are "
                                             "not shown.");

        ImGui::Spacing();
        if (ImGui::Button("Close"))
            m_IsClosed = true;
    }
}
