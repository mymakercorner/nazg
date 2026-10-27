// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// MatrixView - how the board is wired and whether its keys work (ui-design.md, "The matrix view",
// "The key test"): a section in the column's Tools group, last, on every board (Rico, 2026-10-18;
// from 2026-10-09 it showed only with Advanced tools on).
//
// Two modes in its strip, Live test first and shown first (Rico, 2026-10-18).
//
// Live test: keys turn green as they are found working -- a view of its own, with no hover, no
// wiring and no dimming. It starts as soon as it shows, one of two ways, chosen at the strip's end
// (ui-design.md, "The key test"; Rico, 2026-10-26):
//
// - The computer's keys, first on every board: a key lights when the computer receives what it
//   sends -- the keymap and the system tested as much as the switches. A key sending nothing
//   cannot be tested, hatched; keys sending the same thing light together, marked ?
//   (model/NazgHostKeys.h). The strip has the layers: the board shows the one picked, or, while a
//   key received is down, the layer it comes from -- the one whose own key sends it -- its entry
//   in violet. VIA and Vial do not report the active layer: Nazg infers it. A key received counts
//   on every layer whose key sends it, so each layer's count is its own.
// - The switches, by choice: by position, whatever they send -- for a board just built. Until a key
//   is pressed a firmware refusing its matrix cannot be told from a board at rest -- both answer
//   all released -- so the first press decides: received by the computer while the matrix shows
//   nothing for two readings, the firmware refuses it, and the test goes back to the computer's
//   keys, saying why, Switches greyed. A board known not to report -- too old, too wide -- greys
//   them from the start; a locked Vial board goes back until unlocked. The board is opened while
//   the switches are tested, polled every 20 ms as VIA and Vial poll it, and closed when the test
//   stops -- so meanwhile nothing else may talk to the board: HID hands every open handle a copy
//   of each reply, and one left idle would read a stale one next.
//
// The test stops once another section is shown, or when Nazg needs the board -- WhileHidden() --
// and starts again, on the computer's keys, when the view shows again; its polling holds the
// header's board menu back no more than the diagnostics' does (IsPolling()).
//
// Wiring: hover a key and its row and its column light, with their keys joined; hover a ruler
// label and that whole row or column does. Click pins what is in focus, to move the mouse
// away; clicking it again lets go. What the board shows is ui/NazgMatrixDescription.h's.
//
// The firmware must consent (via-vial-commands.md): a Vial board only while unlocked, which
// the test checks first; a mainline VIA board only when built with VIA_INSECURE, or with
// SECURE_ENABLE and unlocked -- otherwise it answers every key released, which cannot be told
// from nothing pressed, so the panel says so while nothing has been seen.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgMatrixDescription.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class MatrixView : public Section
    {
    public:
        // The board at `path`, drawn by `keyboard`, which gives the legends -- layer 0's, or on the
        // computer's keys the layer shown.
        // `legends` is the legends' settings. The transport, the keyboard and the settings
        // outlive the view.
        MatrixView(HidTransport&         transport,
                   std::string           path,
                   bool                  isVial,
                   const Keyboard&       keyboard,
                   const LegendSettings& legends);

        MatrixView(const MatrixView&)            = delete;
        MatrixView& operator=(const MatrixView&) = delete;

        [[nodiscard]] std::string_view Name() const override { return "Matrix"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::ChartGridDots; }
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Tools; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;
        void                OnStripGroupChosen(size_t entry) override;   // a layer
        void                OnStripChoice(size_t entry) override;        // Switches, or the computer's keys

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        // A request in flight, which refers to the view.
        [[nodiscard]] bool IsBusy() const override;

        // The live test only reads, so its requests hold nothing back: whatever needs the board stops
        // it through WhileHidden() and waits for the reading in flight (ui/NazgSection.h).
        [[nodiscard]] bool IsPolling() const override { return IsBusy(); }

        // Stops the live test once its request is back: the next section, or whatever Nazg does with
        // the board next, must have it. Nothing starts in the frame it is called in.
        void WhileHidden() override;

        // The live test is reading the board. Its keys then type into Nazg, so none of them
        // may drive the UI -- the caller turns keyboard navigation off.
        [[nodiscard]] bool IsLiveTestRunning() const noexcept { return m_Live == Live::Running && !m_ShowWiring; }

        // A key of the computer's, pressed or released, as USB numbers it (model/NazgHostKeys.h): every
        // one, while the live test runs -- it tests the keys on the computer's keys, and on the
        // switches tells a firmware refusing its matrix from a board at rest.
        void OnHostKey(uint32_t usage, bool down) override;

        // The live test reads the computer's keys, or waits to know whether it must: the window takes
        // the keyboard meanwhile, so Win and Alt+Tab reach it and Alt+F4 does not close Nazg.
        [[nodiscard]] bool WantsComputerKeys() const noexcept override
        {
            return IsLiveTestRunning() && (m_Method == Method::Computer || !m_SwitchesConfirmed);
        }

    private:
        enum class Live
        {
            Off,        // wiring, or not shown
            Starting,   // opening the board and asking whether it will answer
            Running,
            Failed,     // m_LiveMessage says why; the board is closed
        };

        // How the live test reads the board, chosen at the strip's end.
        enum class Method
        {
            Computer,   // the keys the computer receives -- by default, or as m_Why says
            Switches,   // the board's switch matrix
        };

        // A fresh test on the computer's keys: what it saw forgotten, the layer picked kept.
        void ResetTest();

        // The live test on the computer's keys from now on, `why` said -- empty when chosen -- the
        // board closed once nothing is in flight. `refused`: the switches can never be read, greyed.
        void UseComputerKeys(std::string why, bool refused);

        // The layer the board shows: the one a key held came from, else the one picked; layer 0 on
        // the switches, which test positions.
        [[nodiscard]] uint8_t ShownLayer() const noexcept;

        // The layer the keys just received came from, now the board is known: m_Peek while one is held.
        void FollowReceived(const BoardDescription& board);

        // The board as the computer's keys test it, on the layer shown: what each key sends, received or held.
        void DescribeComputerKeys(BoardDescription& board);

        void DrawMethodLine();

        [[nodiscard]] MatrixFocus Focus() const { return m_Pinned ? *m_Pinned : m_Hovered; }

        // The strip is on Live test: the board and the panel show the test, not the wiring.
        [[nodiscard]] bool IsLiveShown() const noexcept { return !m_ShowWiring; }

        // Ends the live test and closes the board, for another request to use it; a failed one is
        // forgotten, to be tried again. Only when not IsBusy(); does nothing when no test runs.
        void StopLiveTest();

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

        Live                            m_Live = Live::Off;
        std::string                     m_LiveMessage;
        bool                            m_ShowWiring      = false;   // the strip's choice: Live test unless Wiring
        bool                            m_HiddenThisFrame = false;   // WhileHidden() was called: start nothing
        std::optional<HidDeviceChannel> m_Channel;                   // the board, open while the live test runs
        via::SwitchMatrixFormat         m_Format          = via::SwitchMatrixFormat::Paged;
        MatrixLive                      m_Readings;
        Task<void>                      m_Request;
        double                          m_LastPoll = 0.0;   // ImGui::GetTime() of the last poll sent

        Method      m_Method = Method::Computer;
        std::string m_Why;               // why the computer's keys when not chosen: "The board is locked..."
        std::string m_SwitchesRefused;   // why the switches cannot be chosen; empty: they can
        bool        m_SwitchesConfirmed = false;   // a press shown by the matrix: it reports its switches

        // Telling a refusal from rest, on the switches: since when a key of the computer's is down, and
        // how many readings sent after it came back with nothing pressed.
        std::optional<double> m_HostDownAt;
        int                   m_QuietReadings = 0;

        // The computer's keys, by the keycode names they come from: held now, and seen since the test began.
        std::set<std::string_view> m_HostHeld;
        std::set<std::string_view> m_HostSeen;

        // The layers: the one picked in the strip, and the one a key held came from (Rico, 2026-10-26).
        uint8_t                m_Layer = 0;
        std::optional<uint8_t> m_Peek;

        // The last key received, by the names that send it, and the layer it came from -- found once
        // the board is described (m_FindLayer) -- said in the panel.
        std::vector<std::string_view> m_Received;
        bool                          m_FindLayer = false;
        std::optional<uint8_t>        m_ReceivedFrom;
        std::string                   m_ReceivedLine;
    };
}
