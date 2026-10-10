// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// TapDanceSection - a Vial board's tap dances (ui-design.md, "The Tap Dance section", after the
// mockup ui-design/tap-dance-section.html): the slots in the strip, the board marking the keys that
// hold a tap dance, and in the panel the selected one's four actions side by side -- each with a
// small drawing of its gesture and its key, an empty one showing faint what the firmware does
// instead -- its tapping term, a line saying when the tap is sent, and Keymap's picker.
//
// Read from the board when the section is first shown, one slot per round trip; written by Save,
// the changed slots only, each read back -- a locked board stores Boot as nothing. Revert drops the
// changes.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "model/NazgTapDance.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class TapDanceSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; `lock` is its lock state. All outlive the
        // section -- ui/NazgSection.h.
        TapDanceSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                        const std::optional<VialUnlockStatus>& lock, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Tap Dance"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::HandClick; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;

        [[nodiscard]] bool        HasUnsavedChanges() const override;
        [[nodiscard]] std::string UnsavedSummary() const override;
        void                      SaveChanges() override;
        void                      DiscardChanges() override;

    private:
        [[nodiscard]] bool        IsLocked() const;
        [[nodiscard]] bool        IsChanged(size_t dance) const;
        [[nodiscard]] size_t      KeysHolding(size_t dance) const;

        [[nodiscard]] TapDance          DanceOf(const VialTapDanceEntry& entry) const;
        [[nodiscard]] std::optional<VialTapDanceEntry> EntryOf(const TapDance& dance) const;

        Task<void> Load();
        Task<void> Save(std::vector<size_t> dances);

        // The panel's parts, top to bottom.
        void DrawDanceLine();
        void DrawActions();
        void DrawTiming();
        void DrawTools();
        void DrawPicker();


        HidTransport&                          m_Transport;
        std::string                            m_Path;
        Keyboard&                              m_Keyboard;
        const LegendSettings&                  m_Legends;
        const std::optional<VialUnlockStatus>& m_Lock;
        const bool&                            m_AdvancedTools;

        std::vector<TapDance> m_Saved;    // as the board holds them
        std::vector<TapDance> m_Dances;   // as edited

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        // The slot shown, and in it the action the picker sets.
        size_t      m_Dance  = 0;
        DanceAction m_Action = DanceAction::Tap;

        KeycodePickerState m_Picker;
    };
}
