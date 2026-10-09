// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// MatrixView - how the board is wired (ui-design.md, "The matrix view"): a section in the
// column's Tools group, last, while the Advanced tools setting is on (Rico, 2026-10-09) -- every
// board has the data, so with the setting off it would bring the column back on every
// keymap-only board.
//
// Two modes in its strip. Wiring: hover a key and its row and its column light, with their
// keys joined; hover a ruler label and that whole row or column does. Click pins what is in
// focus, to move the mouse away; clicking it again lets go. What the board shows is
// ui/NazgMatrixDescription.h's.
//
// Live test: keys turn green as the board reports them pressed, a checklist for a freshly
// built board -- a view of its own, with no hover, no wiring and no dimming. The board is opened once and kept open while the test runs, polled every
// 20 ms as VIA and Vial poll it, and closed when the test stops -- so while it runs nothing
// else may talk to the board: HID hands every open handle a copy of each reply, and one left
// idle would read a stale one next. StopLiveTest() is for whoever needs the board; the test
// also stops once another section is shown, WhileHidden().
//
// The firmware must consent (via-vial-commands.md): a Vial board only while unlocked, which
// the test checks first; a mainline VIA board only when built with VIA_INSECURE, or with
// SECURE_ENABLE and unlocked -- otherwise it answers every key released, which cannot be told
// from nothing pressed, so the panel says so while nothing has been seen.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgMatrixDescription.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class MatrixView : public Section
    {
    public:
        // The board at `path`, drawn by `keyboard`, which gives the legends -- layer 0's.
        // `legends` is the legends' settings. The transport, the keyboard and the settings
        // outlive the view.
        MatrixView(HidTransport&         transport,
                   std::string           path,
                   bool                  isVial,
                   const Keyboard&       keyboard,
                   const LegendSettings& legends);

        // Closes the board if the live test left it open. Never while IsBusy().
        ~MatrixView() override;

        MatrixView(const MatrixView&)            = delete;
        MatrixView& operator=(const MatrixView&) = delete;

        [[nodiscard]] std::string_view Name() const override { return "Matrix"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::ChartGridDots; }
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Tools; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        // A request in flight, which refers to the view.
        [[nodiscard]] bool IsBusy() const override;

        // Stops the live test once its request is back: the next section must have the board.
        void WhileHidden() override;

        // Ends the live test and closes the board, for another request to use it. Only when
        // not IsBusy(); does nothing when no test runs.
        void StopLiveTest();

        // The live test is reading the board. Its keys then type into Nazg, so none of them
        // may drive the UI -- the caller turns keyboard navigation off.
        [[nodiscard]] bool IsLiveTestRunning() const noexcept { return m_Live == Live::Running && !m_WantsWiring; }

    private:
        enum class Live
        {
            Off,        // wiring
            Starting,   // opening the board and asking whether it will answer
            Running,
            Failed,     // m_LiveMessage says why; the board is closed
        };

        [[nodiscard]] MatrixFocus Focus() const { return m_Pinned ? *m_Pinned : m_Hovered; }

        // The strip is on Live test: the board and the panel show the test, not the wiring.
        [[nodiscard]] bool IsLiveShown() const noexcept { return m_Live != Live::Off && !m_WantsWiring; }

        Task<void> StartLive();
        Task<void> Poll();
        void       Fail(std::string message);

        void DrawLivePanel();

        HidTransport&         m_Transport;
        std::string           m_Path;
        bool                  m_IsVial;
        const Keyboard&       m_Keyboard;
        const LegendSettings& m_Legends;

        MatrixFocus                m_Hovered;
        std::optional<MatrixFocus> m_Pinned;
        MatrixCounts               m_Counts;   // of what the board was last described with
        std::string                m_KeyAt;    // the keycode at m_Counts.keysAt, on layer 0
        std::string                m_Outside;  // FindInDefinition()'s findings, as positions
        std::string                m_Stacked;

        Live               m_Live = Live::Off;
        std::string        m_LiveMessage;
        bool               m_WantsWiring = false;   // chosen while a request was in flight
        DeviceId           m_Device      = c_InvalidDevice;
        SwitchMatrixFormat m_Format      = SwitchMatrixFormat::Paged;
        MatrixLive         m_Readings;
        Task<void>         m_Request;
        double             m_LastPoll = 0.0;   // ImGui::GetTime() of the last poll sent
    };
}
