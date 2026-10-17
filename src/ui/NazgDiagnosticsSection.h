// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DiagnosticsSection - the Leyden Jar controller's own diagnostics (ui-design.md, "Plugins"), in the
// column's Tools group on every board that answers its protocol: what Rico's Leyden Jar Diagnostic
// Tool does, inside Nazg. The two mockups settle every detail: ui-design/diagnostics-section.html
// (the modes, Device, Key presses) and ui-design/key-health.html (Signal levels).
//
// Three modes in the strip, opening on Key presses -- what an owner hunting a dead key needs first:
//
//   - Key presses: the keys down now, and every key seen since the mode opened, counted -- on the
//     board, QMK's matrix or the controller's, the choice on the panel's first line;
//   - Signal levels: which keys may not be recognised, now and then or at all -- the board coloured
//     by verdict, the keys to look at as bars or the whole board as one dot per key, a key's last
//     presses side by side (model/NazgKeyHealth.h); the threshold margins the verdicts are judged by,
//     and a Levels button putting the tool's numbers on the keys -- max, now, min and the bin -- for
//     firmware work. All of it without Advanced tools: Diagnostics is an expert's section itself
//     (Rico, 2026-10-17);
//   - Device: the controller's, Vial's and USB's information, Enter bootloader, Erase EEPROM.
//
// Key output is off for as long as the section shows, every mode: the keys pressed while testing
// send nothing to the computer. The firmware keeps it in RAM; on again is the channel's farewell
// (leydenjar::Protocol::TurnKeyOutputOffUntilClosed()), so every path that closes the board turns
// it on -- another section, Settings, another board, a reload, quitting, the transport's shutdown.
//
// The board stays open while the section shows, and a mode polling asks again as soon as its last
// reading is back -- no fixed rate: a level reading is the firmware's sweep, 136 ms, then a read
// per controller column; a scan and its rows some 20 ms. Each reading goes to the transport as one
// series (DeviceChannel::RequestSeries()), so it takes the board's time and not one frame per
// round trip. Polling holds nothing back (Section::IsPolling()): leaving stops it.
//
// A controller wired to no matrix -- every threshold at the firmware's sentinel -- is said in one
// sentence in both monitors, the board plain; Device stays as it is.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "adapters/leydenjar/NazgLeydenJarProtocol.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "library/NazgDefinitionChoice.h"
#include "model/NazgKeyHealth.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class DiagnosticsSection : public Section
    {
    public:
        // The board at `path`, drawn by `keyboard`, which gives the legends -- layer 0's -- and
        // `identity`, its USB description. The transport, the keyboard and the settings outlive the
        // section.
        DiagnosticsSection(HidTransport&         transport,
                           std::string           path,
                           const Keyboard&       keyboard,
                           const LegendSettings& legends,
                           SafeMargins&          safeMargins,
                           DeviceIdentity        identity);

        DiagnosticsSection(const DiagnosticsSection&)            = delete;
        DiagnosticsSection& operator=(const DiagnosticsSection&) = delete;

        [[nodiscard]] std::string_view Name() const override { return "Diagnostics"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Activity; }
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Tools; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;
        [[nodiscard]] bool IsPolling() const override;

        // Stops polling once the reading in flight is back, and closes the board: key output on.
        void WhileHidden() override;

        // Sent to its bootloader, or erased and restarting.
        [[nodiscard]] std::string BoardLeft() const override { return m_Link == Link::Gone ? m_Message : std::string(); }

    private:
        enum class Mode
        {
            KeyPresses,
            SignalLevels,
            Device,
        };

        // What the board region draws: the definition's keys, or a plain grid of a matrix.
        enum class View
        {
            Board,
            QmkMatrix,
            Controller,
        };

        enum class Link
        {
            Closed,
            Opening,   // opened, the controller's information read, key output turned off
            Open,
            Failed,    // m_Message says why; the board is closed
            Gone,      // sent to its bootloader or erased: it is leaving
        };

        // One key of the board, as the monitors see it: its matrix cell and the controller's
        // position it is wired to, its threshold. One per cell: switches wired in parallel are one.
        struct Key
        {
            uint8_t     row              = 0;   // QMK's matrix
            uint8_t     column           = 0;
            uint8_t     controllerRow    = 0;
            uint8_t     controllerColumn = 0;
            bool        isMapped         = false;   // the controller has a position for it
            uint16_t    threshold        = 0;
            uint8_t     bin              = 0;
            std::string name;   // what it prints, or its position
        };

        // A row of the Signal levels' list: a key, or a weak controller column.
        struct ListRow
        {
            std::optional<size_t> key;   // into m_Keys
            std::optional<int>    column;
        };

        enum class ListTab
        {
            Keys,
            WholeBoard,
        };

        [[nodiscard]] bool IsPollingMode() const noexcept { return m_Mode != Mode::Device; }
        [[nodiscard]] bool IsRequesting() const;
        [[nodiscard]] bool IsUnconnected() const;

        // The keys, again whenever the layout options change: from the board as the definition draws
        // it, its legends filled.
        void FindKeys(const BoardDescription& board);
        [[nodiscard]] std::optional<size_t> KeyAt(uint8_t row, uint8_t column) const;
        [[nodiscard]] std::optional<size_t> KeyAtController(uint8_t column, uint8_t row) const;

        // The board region: the definition's keys, or a matrix as a grid with its rulers.
        void DescribeGrid(BoardDescription& board, bool controller);
        void DescribeKeyPresses(BoardDescription& board);
        void DescribeLevels(BoardDescription& board);
        [[nodiscard]] std::optional<size_t> KeyOf(const BoardKey& key) const;

        // The section's work on the board.
        Task<void> Open();
        Task<void> Poll();
        Task<void> Leave(bool erase);
        void       Fail(std::string message);
        void       Close();

        void AddKeyPresses(const std::vector<uint32_t>& rows);
        void AddControllerPresses(const std::vector<uint8_t>& columns);
        void AddLevels(const std::vector<std::vector<uint16_t>>& levels);
        void Judge();
        void ClearReadings();

        // The panel, by mode.
        void DrawKeyOutputLine();
        void DrawUnconnected(const char* title, const char* what);
        void DrawKeyPressesPanel();
        void DrawLevelsPanel();
        void DrawSummary();
        void DrawKeyList();
        void DrawBarsHeader();
        void DrawBar(size_t key, ImVec2 at, float room);
        void DrawWholeBoard();
        void DrawKeyLevels();
        void DrawDevicePanel();
        void DrawEraseQuestion();

        [[nodiscard]] std::vector<ListRow> ListRows() const;
        [[nodiscard]] std::string          TooltipOf(size_t key) const;
        [[nodiscard]] bool                 IsPressedNow(size_t key) const;

        HidTransport&         m_Transport;
        std::string           m_Path;
        const Keyboard&       m_Keyboard;
        const LegendSettings& m_Legends;
        SafeMargins&          m_SafeMargins;   // App's, for the session
        SafeMargins           m_JudgedMargins;  // those m_Health was judged by
        DeviceIdentity        m_Identity;

        Mode m_Mode          = Mode::KeyPresses;
        View m_View          = View::Board;   // Signal levels has no QMK matrix: it shows the board there
        bool m_LevelsOnKeys  = false;         // the Levels button
        bool m_HiddenThisFrame = false;       // WhileHidden() was called: nothing starts this frame

        // The board, open while the section shows.
        Link                            m_Link = Link::Closed;
        std::string                     m_Message;
        std::optional<HidDeviceChannel> m_Channel;
        Task<void>                      m_Request;
        bool                            m_StopPolling = false;
        bool                            m_Leaving     = false;   // the bootloader or the erase, being sent

        // What the controller and Vial said of themselves.
        leydenjar::ControllerInfo     m_Info;
        std::optional<vial::Identity> m_Vial;
        uint32_t                      m_DefinitionSize = 0;
        LevelScale                    m_Scale;

        std::vector<Key>     m_Keys;
        std::vector<uint8_t> m_KeysSelection;   // the layout options m_Keys were found for

        // Key presses: by QMK's matrix row, bit n column n; by controller column, bit n row n.
        std::vector<uint32_t> m_PressedNow;
        std::vector<uint32_t> m_Seen;
        std::vector<uint8_t>  m_ControllerNow;
        std::vector<uint8_t>  m_ControllerSeen;

        // Signal levels: every reading since the mode opened, by controller position
        // ([column * rows + row]), and when each came.
        std::vector<std::vector<uint16_t>> m_Traces;
        std::vector<ToolLevels>            m_Tool;
        std::vector<double>                m_ReadingTimes;
        BoardHealth                        m_Health;   // parallel to m_Keys once judged
        size_t                             m_JudgedReadings = 0;
        double                             m_JudgedAt       = 0.0;

        std::optional<size_t>     m_Selected;   // into m_Keys
        std::optional<size_t>     m_HoveredKey;
        std::optional<int>        m_HoveredColumn;
        std::optional<KeyVerdict> m_Filter;
        ListTab                   m_Tab = ListTab::Keys;
        bool                      m_EraseAsked = false;

        // The board region's keys, by index into the description: which of m_Keys each is.
        std::vector<std::optional<size_t>> m_BoardKeys;
    };
}
