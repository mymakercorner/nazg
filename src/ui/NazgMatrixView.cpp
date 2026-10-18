// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMatrixView.h"

#include <algorithm>
#include <exception>
#include <map>
#include <utility>   // std::exchange, std::move

#include "imgui.h"

#include "adapters/vial/NazgVialProtocol.h"
#include "model/NazgHostKeys.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // As VIA's and Vial's matrix testers wait between two readings.
        constexpr double c_PollInterval = 0.020;   // seconds

        // The first Vial protocol with the matrix tester (via-vial-commands.md).
        constexpr uint32_t c_VialMatrixTesterProtocol = 3;

        // Readings, sent while a key of the computer's is down, that show nothing pressed: enough to
        // say the firmware refuses its matrix. More than one: a reading may land just after a release
        // the computer has not reported yet.
        constexpr int c_QuietReadings = 2;

        bool AnyPressed(const via::SwitchMatrixState& state)
        {
            for (uint8_t row = 0; row < state.rows; ++row)
                for (uint8_t column = 0; column < state.columns; ++column)
                    if (state.IsPressed(row, column))
                        return true;
            return false;
        }

        // As IsPressed() reads it: rows of ceil(columns / 8) bytes, column 0 the last byte's bit 0.
        void SetPressed(via::SwitchMatrixState& state, uint8_t row, uint8_t column)
        {
            if (row >= state.rows || column >= state.columns)
                return;
            const size_t rowBytes = (state.columns + 7u) / 8u;
            state.bytes[row * rowBytes + (rowBytes - 1 - column / 8u)] |= static_cast<uint8_t>(1u << (column % 8u));
        }

        via::SwitchMatrixState Empty(uint8_t rows, uint8_t columns)
        {
            return { rows, columns, std::vector<uint8_t>(rows * ((columns + 7u) / 8u), 0) };
        }

        // "R4 C10", as the rulers say it.
        std::string PositionOf(const via::DefinitionKey& key)
        {
            return "R" + std::to_string(key.row) + " C" + std::to_string(key.column);
        }
    }

    MatrixView::MatrixView(HidTransport& transport, std::string path, bool isVial, const Keyboard& keyboard,
                           const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_IsVial(isVial), m_Keyboard(keyboard), m_Legends(legends)
    {
    }

    Strip MatrixView::DescribeStrip() const
    {
        Strip strip;
        strip.label   = "Matrix";
        strip.entries = { "Live test", "Wiring" };
        strip.chosen  = m_ShowWiring ? 1 : 0;
        if (IsLiveShown() && m_Live == Live::Running && m_Method == Method::Switches)
            strip.tag = "Switches";
        else if (IsLiveShown() && m_Live == Live::Running && m_Method == Method::Computer)
        {
            strip.tag        = "Computer's keys";
            strip.tagWarning = true;
        }
        return strip;
    }

    // The live test starts in DescribeBoard(), once nothing is in flight; Wiring stops it there.
    void MatrixView::OnStripChosen(size_t entry)
    {
        m_ShowWiring = entry == 1;
        if (!m_ShowWiring && m_Live == Live::Failed)
            m_Live = Live::Off;   // chosen again: tried again
    }

    bool MatrixView::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    // Stopped, a failure included -- tried again when shown: a locked board may be unlocked by then.
    void MatrixView::WhileHidden()
    {
        m_HiddenThisFrame = true;
        StopLiveTest();
    }

    void MatrixView::StopLiveTest()
    {
        if (IsBusy() || m_Live == Live::Off)
            return;

        m_Channel.reset();
        m_Live = Live::Off;
    }

    void MatrixView::DescribeBoard(BoardDescription& board)
    {
        if (m_ShowWiring)
            StopLiveTest();

        // Nothing starts in a frame Nazg told the view to stop in. Otherwise the live test starts
        // as soon as it shows, and is paced here, as the Vial unlock is: a reading goes out once
        // the last one has come back and the interval has passed since it was asked for.
        if (!std::exchange(m_HiddenThisFrame, false) && !m_ShowWiring && !IsBusy())
        {
            if (m_Live == Live::Off)
            {
                m_Readings = {};
                m_LiveMessage.clear();
                m_Request = StartLive();
            }
            else if (m_Live == Live::Running && m_Method == Method::Computer)
            {
                m_Channel.reset();   // nothing more to read from the board
            }
            else if (m_Live == Live::Running && m_Channel && ImGui::GetTime() - m_LastPoll >= c_PollInterval)
            {
                m_LastPoll = ImGui::GetTime();
                m_Request  = Poll();
            }
        }

        // Layer 0's legends, so each key is recognised; the keymap itself is not the point here.
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The live test is a view of its own: only what the board reports, no wiring.
        const bool isLive = IsLiveTestRunning();
        // Before the first press too, the keys the computer could not hear hatched (Rico, 2026-10-18):
        // the board most likely does not report its switches; if it does, the hatching goes.
        if (isLive && m_Method != Method::Switches)
            DescribeComputerKeys(board);
        else
            m_Counts = DescribeMatrix(board, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns,
                                      IsLiveShown() ? MatrixFocus{} : Focus(), isLive ? &m_Readings : nullptr);
        m_KeyAt  = m_Counts.keysAt.empty()
                       ? std::string()
                       : FormatKeycode(m_Keyboard.KeycodeFor(board.keys[m_Counts.keysAt.front()].geometry, 0));

        // What the definition says about its matrix, by position, for the panel.
        const MatrixFindings findings =
            FindInDefinition(board, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);

        m_Outside.clear();
        for (size_t key : findings.outside)
            m_Outside += (m_Outside.empty() ? "" : ", ") + PositionOf(board.keys[key].geometry);

        m_Stacked.clear();
        for (const auto& [below, above] : findings.stacked)
            m_Stacked += (m_Stacked.empty() ? "" : "; ") + PositionOf(board.keys[below].geometry) + " under " +
                         PositionOf(board.keys[above].geometry);
    }

    void MatrixView::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        // No focus in the live test; what Wiring had pinned waits for it.
        if (IsLiveShown())
        {
            m_Hovered = {};
            return;
        }

        MatrixFocus hovered;
        if (events.hoveredKey && board.keys[*events.hoveredKey].geometry.IsSwitch())
        {
            const via::DefinitionKey& key = board.keys[*events.hoveredKey].geometry;
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

        // Some messages are long: wrapped at the panel's edge.
        ImGui::PushTextWrapPos(0.0f);

        if (IsLiveShown())
        {
            DrawLivePanel();
        }
        else if (focus.IsEmpty())
        {
            ImGui::TextUnformatted("Hover a key, or a row or column label. Click to pin.");
        }
        else if (focus.row && focus.column)
        {
            // Several keys on one position are switches wired in parallel: a design, not a fault.
            if (m_Counts.keysAt.size() > 1)
                ImGui::Text("%zu keys at row %d, column %d: wired in parallel, they act as one switch, %s.",
                            m_Counts.keysAt.size(), *focus.row, *focus.column, m_KeyAt.c_str());
            else if (!m_Counts.keysAt.empty())
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

        if (m_Pinned && !IsLiveShown())
            ColouredText(PanelColour::Muted, "Pinned: click it again to let go.");

        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "Matrix: %d rows, %d columns. Layer 0's legends.",
                     m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);
        if (!m_Keyboard.layoutSelection.empty())
            ColouredText(PanelColour::Muted, "Drawn with the board's layout options; keys of the other choices are "
                                             "not shown.");

        // What the definition says about its matrix: reported, never refused.
        if (!m_Outside.empty())
            ColouredText(PanelColour::Warning, "Outside the %d x %d matrix: %s. The firmware has no such position: "
                                               "these keys can never be read, remapped or tested.",
                         m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns, m_Outside.c_str());
        if (!m_Stacked.empty())
            ColouredText(PanelColour::Muted, "Drawn exactly on top of each other: %s. The key below can never be "
                                             "seen or clicked -- probably a copy left in the definition.",
                         m_Stacked.c_str());

        ImGui::PopTextWrapPos();
    }

    void MatrixView::DrawLivePanel()
    {
        switch (m_Live)
        {
        case Live::Off:   // for a frame, until DescribeBoard() starts it
        case Live::Starting:
            ImGui::TextUnformatted("Opening the board...");
            return;

        case Live::Failed:
            ColouredText(PanelColour::Warning, "%s", m_LiveMessage.c_str());
            return;

        case Live::Running:
            break;
        }

        DrawMethodLine();
        if (m_Method == Method::Unknown)
            return;
        ImGui::Spacing();

        ImGui::Text("Seen %zu of %zu keys. Press every key once.", m_Counts.seenKeys, m_Counts.keys);
        ImGui::SameLine();
        if (ImGui::SmallButton("Start over"))
        {
            m_Readings = {};
            m_HostSeen.clear();
        }

        if (m_Counts.seenKeys == m_Counts.keys && m_Counts.keys > 0)
            ColouredText(PanelColour::Success, m_Method == Method::Switches ? "Every key works."
                                                                              : "Every key the computer can hear works.");
        else
            ColouredText(PanelColour::Muted, "A row or column label stays grey until all its keys are seen: one "
                                             "that never turns green points at that trace.");

        if (!m_Outside.empty())
            ColouredText(PanelColour::Muted, "Not counted: %s, outside the matrix, can never be seen.",
                         m_Outside.c_str());

        if (!m_Counts.pressedWithoutKey.empty())
        {
            std::string positions;
            for (const auto& [row, column] : m_Counts.pressedWithoutKey)
                positions += (positions.empty() ? "" : ", ") + std::string("R") + std::to_string(row) + " C" +
                             std::to_string(column);
            ColouredText(PanelColour::Warning, "Pressed where no key is drawn: %s -- a key of another layout "
                                               "choice, or ghosting from a missing diode.",
                         positions.c_str());
        }

    }

    // Always said (Rico, 2026-10-18): which way the board is read, and what that way cannot see.
    void MatrixView::DrawMethodLine()
    {
        switch (m_Method)
        {
        case Method::Unknown:
            ImGui::TextUnformatted("Press any key.");
            ColouredText(PanelColour::Muted, "Nazg reads the board's switches if it reports them, else the keys the "
                                             "computer receives -- it says which once a key is pressed. Hatched: keys "
                                             "sending nothing the computer hears, which only the switches can test.");
            return;
        case Method::Switches:
            ColouredText(PanelColour::Success, "Testing the switches");
            ColouredText(PanelColour::Muted, "The board reports which of its switches are pressed, whatever they send.");
            return;
        case Method::Computer:
            ColouredText(PanelColour::Warning, "Testing what the computer receives");
            ColouredText(PanelColour::Muted, "%s, so a key lights when the computer receives what it sends. A key sending "
                                             "nothing -- a layer key, an empty one -- cannot be tested this way, and is "
                                             "hatched; keys sending the same thing light together, marked ?. Keys the "
                                             "system keeps may never arrive.",
                         m_Why.c_str());
            return;
        }
    }

    void MatrixView::UseComputerKeys(std::string why)
    {
        m_Method = Method::Computer;
        m_Why    = std::move(why);
        m_Live   = Live::Running;
    }

    void MatrixView::OnHostKey(uint32_t usage, bool down)
    {
        if (!IsLiveTestRunning() || m_Method == Method::Switches)
            return;

        // Any key, tested or not, tells a refusal from rest.
        if (m_Method == Method::Unknown)
        {
            if (down && !m_HostDownAt)
            {
                m_HostDownAt    = ImGui::GetTime();
                m_QuietReadings = 0;
            }
            else if (!down)
                m_HostDownAt.reset();
        }

        for (const std::string_view name : KeycodesSending(usage, m_Keyboard.keycodeVersion))
        {
            if (down)
            {
                m_HostHeld.insert(name);
                m_HostSeen.insert(name);
            }
            else
                m_HostHeld.erase(name);
        }
    }

    void MatrixView::DescribeComputerKeys(BoardDescription& board)
    {
        const uint8_t             rows    = m_Keyboard.definition.matrixRows;
        const uint8_t             columns = m_Keyboard.definition.matrixColumns;
        const qmk::KeycodeVersion version = m_Keyboard.keycodeVersion;

        // What each key is tested by, and how many keys share it: a switch by its keycode on the base
        // layer, a Vial knob's direction key by its turn.
        const auto turnOf = [&](const via::DefinitionKey& key, bool clockwise) -> std::optional<std::string_view>
        {
            if (m_Keyboard.encoderMap.empty() || key.encoderIndex < 0 ||
                static_cast<size_t>(key.encoderIndex) >= m_Keyboard.encoderMap[0].size())
                return std::nullopt;
            const KnobTurns& turns = m_Keyboard.encoderMap[0][static_cast<size_t>(key.encoderIndex)];
            return TestedName(clockwise ? turns.clockwise : turns.counterClockwise, version);
        };

        std::vector<std::optional<std::string_view>> tested(board.keys.size());
        std::map<std::string_view, int>              sharing;
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const via::DefinitionKey& key = board.keys[index].geometry;
            if (key.IsSwitch())
                tested[index] = TestedName(m_Keyboard.KeycodeFor(key, 0), version);
            else if (key.encoderIndex >= 0 && key.encoderDirection >= 0)
                tested[index] = turnOf(key, key.encoderDirection == 1);
            if (tested[index])
                ++sharing[*tested[index]];
        }

        // Readings as the switches would give them: a key received is seen, one held is pressed. A key
        // that cannot be tested counts as seen, so its row and its column can still be complete.
        MatrixLive live{ Empty(rows, columns), Empty(rows, columns) };
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const via::DefinitionKey& key = board.keys[index].geometry;
            if (!key.IsSwitch())
                continue;
            if (!tested[index] || m_HostSeen.count(*tested[index]) > 0)
                SetPressed(live.seen, key.row, key.column);
            if (tested[index] && m_HostHeld.count(*tested[index]) > 0)
                SetPressed(live.pressed, key.row, key.column);
        }
        m_Counts = DescribeMatrix(board, rows, columns, MatrixFocus{}, &live);

        // The keys counted are those that can be tested; the marks say which cannot, and which were
        // heard but share what they send.
        size_t keys = 0, seen = 0;
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            BoardKey&                 described = board.keys[index];
            const via::DefinitionKey& key       = described.geometry;

            if (key.encoderIndex >= 0 && key.encoderDirection < 0)
            {
                // A VIA knob turned both ways: lit once both turns are heard, outlined after one.
                const auto ccw   = turnOf(key, false), cw = turnOf(key, true);
                const int  heard = (ccw && m_HostSeen.count(*ccw) ? 1 : 0) + (cw && m_HostSeen.count(*cw) ? 1 : 0);
                if (heard == 2)
                    described.marks |= Mark::Checked;
                else if (heard == 1)
                    described.marks |= Mark::Highlighted;
                if (!key.IsSwitch())
                    continue;
            }

            if (!tested[index])
            {
                if (key.IsSwitch())
                {
                    described.marks = static_cast<uint8_t>((described.marks & ~Mark::Checked) | Mark::Dimmed);
                    described.hatched = true;
                }
                continue;
            }

            const bool heard = m_HostSeen.count(*tested[index]) > 0;
            if (key.IsSwitch() && key.row < rows && key.column < columns)
            {
                ++keys;
                seen += heard ? 1 : 0;
            }
            else if (heard)
                described.marks |= Mark::Checked;   // a Vial knob's direction key

            if (heard && sharing[*tested[index]] > 1)
            {
                described.marks     = static_cast<uint8_t>(described.marks & ~Mark::Checked);
                described.condition = KeyCondition::Ambiguous;
            }
        }
        m_Counts.keys     = keys;
        m_Counts.seenKeys = seen;
    }

    void MatrixView::Fail(std::string message)
    {
        m_Channel.reset();
        m_Live        = Live::Failed;
        m_LiveMessage = std::move(message);
    }

    // Opens the board, keeps it open, and finds out whether and how it will answer.
    Task<void> MatrixView::StartLive()
    {
        m_Live          = Live::Starting;
        m_Method        = Method::Unknown;
        m_Why.clear();
        m_HostDownAt.reset();
        m_QuietReadings = 0;
        m_HostHeld.clear();
        m_HostSeen.clear();

        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;

        try
        {
            m_Channel.emplace(co_await HidDeviceChannel::Open(m_Transport, m_Path));
            vial::Protocol vial(*m_Channel);

            if (m_IsVial)
            {
                // A locked Vial board answers with the request echoed back -- every key
                // released -- so the lock is asked first rather than guessed from silence.
                const std::optional<vial::Identity> identity = co_await vial.Detect();
                if (identity && identity->protocolVersion < c_VialMatrixTesterProtocol)
                {
                    UseComputerKeys("This firmware speaks Vial protocol " + std::to_string(identity->protocolVersion) +
                                    ", older than Vial's matrix test");
                    co_return;
                }

                const vial::UnlockStatus status = co_await vial.GetUnlockStatus();
                if (!status.unlocked)
                {
                    UseComputerKeys("The board is locked, and Vial reports its switches only once unlocked -- click "
                                    "Locked in the header to test them");
                    co_return;
                }

                m_Format = via::SwitchMatrixFormat::Whole;
            }
            else
            {
                m_Format = via::SwitchMatrixFormatFor(co_await vial.GetProtocolVersion());
            }

            if (m_Format == via::SwitchMatrixFormat::Whole && !via::WholeMatrixFits(rows, columns))
            {
                UseComputerKeys("This firmware reports its switches only when all of them fit in one reply, and " +
                                std::to_string(rows) + " rows of " + std::to_string(columns) + " columns do not");
                co_return;
            }

            m_Live     = Live::Running;
            m_LastPoll = 0.0;
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The live test could not start: ") + failure.what());
        }
    }

    Task<void> MatrixView::Poll()
    {
        try
        {
            via::Protocol via(*m_Channel);
            const double  sent = ImGui::GetTime();

            const via::SwitchMatrixState reading = co_await via.GetSwitchMatrixState(
                m_Format, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);
            AddReading(m_Readings, reading);

            // The first press decides: shown by the matrix, the switches; heard by the computer alone
            // through readings sent while it was down, the firmware refuses its matrix.
            if (m_Method == Method::Unknown)
            {
                if (AnyPressed(reading))
                    m_Method = Method::Switches;
                else if (m_HostDownAt && sent > *m_HostDownAt && ++m_QuietReadings >= c_QuietReadings)
                    UseComputerKeys(m_IsVial ? "This firmware does not report its switches"
                                             : "This firmware does not report its switches -- it was built without "
                                               "VIA_INSECURE");
            }
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The board stopped answering: ") + failure.what());
        }
    }
}
