// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SavedLayoutReview - loading a saved layout (.vil): what the file would change, shown before
// anything is written, then written on Apply (ui-design.md, "Saved layouts"; Rico, 2026-10-18).
//
// It takes the board screen's place, as Settings does: a section with no column, drawn by
// DrawView() with Preview() -- the board as the file would leave it -- so the strip is the layers,
// a dot on each the file changes, the board lights the keys that change, and the panel says what
// else does, line by line, a line under the pointer lighting the keys it concerns.
//
// The board is read first -- the parts a .vil keeps that the Keyboard does not hold -- then
// compared with the file (board/NazgSavedLayoutTransfer.h). A file from another keyboard is asked
// about before that, as Vial does. Once written, what the board did not keep is said, and Back to
// the board loads the board again, so every section reads what it now holds.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "adapters/vial/NazgVialProtocol.h"
#include "adapters/vial/NazgVialSavedLayout.h"
#include "async/NazgTask.h"
#include "board/NazgSavedLayoutTransfer.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class SavedLayoutReview : public Section
    {
    public:
        // The board at `path`, `keyboard` as loaded, `vendorId` and `productId` its own; `fileName`
        // for the title, `file` as read. `lock`: the board's, none on a board without one. The
        // transport, the keyboard, the settings and the lock outlive this object.
        SavedLayoutReview(HidTransport& transport, std::string path, const Keyboard& keyboard, uint16_t vendorId,
                          uint16_t productId, const LegendSettings& legends,
                          const std::optional<vial::UnlockStatus>& lock, std::string fileName, vial::SavedLayout file);

        std::string_view Name() const override { return "Load from file"; }
        Strip            DescribeStrip() const override;
        void             OnStripChosen(size_t entry) override;
        void             OnStripHovered(std::optional<size_t> entry) override;
        void             DescribeBoard(BoardDescription& board) override;
        void             DrawPanel() override;
        bool             IsBusy() const override { return m_Task.IsValid() && !m_Task.IsDone(); }

        // The board as the file would leave it, for DrawView(): the board itself until compared.
        [[nodiscard]] const Keyboard& Preview() const noexcept { return m_Preview; }

        // What the user asked of App, once each.
        struct Action
        {
            bool cancel = false;   // drop the review: nothing was written
            bool apply  = false;   // write -- App unlocks a locked board first, when the changes need it
            bool back   = false;   // written: load the board again
        };
        [[nodiscard]] Action TakeAction() noexcept;

        // The writes need the board unlocked (LayoutChanges::NeedsUnlock()).
        [[nodiscard]] bool NeedsUnlock() const;

        // Writes the changes, once App has the board ready.
        void StartWriting();

    private:
        enum class Stage
        {
            Reading,
            Asking,    // another keyboard's file: load it anyway?
            Review,
            Writing,
            Written,
        };

        Task<void> Read();
        Task<void> Write();
        void       Compare();
        [[nodiscard]] bool IsOtherKeyboard() const;
        [[nodiscard]] bool IsLocked() const;

        void DrawHead();
        void DrawLines();
        void DrawSays();
        void DrawQuestion();

        HidTransport&                            m_Transport;
        std::string                              m_Path;
        const Keyboard&                          m_Keyboard;
        uint16_t                                 m_VendorId;
        uint16_t                                 m_ProductId;
        const LegendSettings&                    m_Legends;
        const std::optional<vial::UnlockStatus>& m_Lock;
        std::string                              m_FileName;
        vial::SavedLayout                        m_File;

        Stage         m_Stage = Stage::Reading;
        BoardContents m_Board;
        LayoutChanges m_Changes;
        Keyboard      m_Preview;
        WriteProgress m_Progress;
        std::string   m_Failure;   // a read or write that failed

        uint8_t                m_Layer = 0;
        std::optional<uint8_t> m_Peek;
        enum class Line
        {
            None,
            Keymap,
            Macros,
            TapDance,
        };
        Line   m_Hovered    = Line::None;
        Line   m_HoveredNow = Line::None;
        Action m_Action;

        Task<void> m_Task;
    };
}
