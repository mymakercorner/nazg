// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// MatrixView - how the board is wired, opened from the board menu (ui-design.md, "The matrix
// view"). Not a section in the column -- every board has the data, so it would bring the
// column back on every keymap-only board -- but it fills the same regions through the same
// contract (ui/NazgSection.h), in place of the sections until it is closed.
//
// Hover a key: its row and its column light, with their wiring. Hover a ruler label: that
// whole row or column. Click pins what is in focus, to move the mouse away; clicking it again
// lets go. What the board shows is ui/NazgMatrixDescription.h's.
//
// Only the structure for now, from the definition alone. The live test -- the strip's second
// entry, *Wiring | Live test* -- and the definition checks in the panel come later.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>

#include "model/NazgKeyboard.h"
#include "ui/NazgMatrixDescription.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class MatrixView : public Section
    {
    public:
        // `keyboard` draws it and gives the legends, layer 0's; `hostLayoutId` is the
        // setting. Both outlive the view.
        MatrixView(const Keyboard& keyboard, const std::string& hostLayoutId);

        [[nodiscard]] std::string_view Name() const override { return "Matrix"; }

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        // Close was clicked: back to the sections.
        [[nodiscard]] bool IsClosed() const noexcept { return m_IsClosed; }

    private:
        [[nodiscard]] MatrixFocus Focus() const { return m_Pinned ? *m_Pinned : m_Hovered; }

        const Keyboard&    m_Keyboard;
        const std::string& m_HostLayoutId;

        MatrixFocus                m_Hovered;
        std::optional<MatrixFocus> m_Pinned;
        MatrixCounts               m_Counts;   // of the focus the board was last described with
        std::string                m_KeyAt;    // the keycode of m_Counts.keyAt, on layer 0
        bool                       m_IsClosed = false;
    };
}
