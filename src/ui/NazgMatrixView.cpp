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
#include "ui/NazgKeycodePicker.h"
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
        if (!IsLiveShown())
            return strip;

        // The test, chosen at the end (Rico, 2026-10-26): the computer's keys in the warning's colour,
        // as their tag was -- they cannot see what sends nothing.
        strip.choiceLabel   = "Test";
        strip.choices       = { "Switches", "Computer's keys" };
        strip.choiceChosen  = m_Method == Method::Switches ? 0 : 1;
        strip.choiceWarning = { false, true };
        if (!m_SwitchesRefused.empty())
            strip.choiceDisabled = { m_SwitchesRefused };

        // The layers, on the computer's keys: the one shown chosen, in violet when a key held shows it.
        const uint8_t layers = m_Keyboard.keymap.Layers();
        if (m_Method == Method::Computer && layers > 1)
        {
            strip.groupLabel = "Layer";
            for (uint8_t layer = 0; layer < layers; ++layer)
                strip.group.push_back(std::to_string(layer));
            strip.groupChosen = ShownLayer();
            if (m_Peek && *m_Peek != m_Layer)
                strip.groupMarked = *m_Peek;
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

    void MatrixView::OnStripGroupChosen(size_t entry)
    {
        m_Layer = static_cast<uint8_t>(entry);
        m_Peek.reset();
    }

    // Each test keeps what it saw while the other runs. The switches open the board in DescribeBoard().
    void MatrixView::OnStripChoice(size_t entry)
    {
        if (entry == 0)
        {
            m_Method            = Method::Switches;
            m_SwitchesConfirmed = false;
            m_Why.clear();
            m_HostDownAt.reset();
            m_QuietReadings = 0;
            if (m_Live == Live::Failed)
                m_Live = Live::Running;   // tried again
        }
        else
        {
            UseComputerKeys({}, false);
        }
    }

    uint8_t MatrixView::ShownLayer() const noexcept
    {
        return m_Method == Method::Computer ? m_Peek.value_or(m_Layer) : 0;
    }

    void MatrixView::ResetTest()
    {
        m_Method            = Method::Computer;
        m_SwitchesConfirmed = false;
        m_Why.clear();
        m_Readings = {};
        m_HostDownAt.reset();
        m_QuietReadings = 0;
        m_HostHeld.clear();
        m_HostSeen.clear();
        m_Peek.reset();
        m_Received.clear();
        m_FindLayer = false;
        m_ReceivedFrom.reset();
        m_ReceivedLine.clear();
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
        // as soon as it shows, on the computer's keys, which need nothing from the board. The
        // switches open it, and are paced here, as the Vial unlock is: a reading goes out once the
        // last one has come back and the interval has passed since it was asked for.
        if (!std::exchange(m_HiddenThisFrame, false) && !m_ShowWiring && !IsBusy())
        {
            if (m_Live == Live::Off)
            {
                ResetTest();
                m_LiveMessage.clear();
                m_Live = Live::Running;
            }

            if (m_Method == Method::Computer)
            {
                m_Channel.reset();   // nothing to read from the board
                if (m_Live == Live::Failed)
                    m_Live = Live::Running;
            }
            else if (m_Live == Live::Running && !m_Channel)
            {
                m_Request = StartLive();
            }
            else if (m_Live == Live::Running && ImGui::GetTime() - m_LastPoll >= c_PollInterval)
            {
                m_LastPoll = ImGui::GetTime();
                m_Request  = Poll();
            }
        }

        // The layer a key just received came from, before the legends are taken from it.
        if (IsLiveTestRunning() && m_Method == Method::Computer)
            FollowReceived(board);

        // The layer shown's legends: layer 0's on the switches and in Wiring, so each key is
        // recognised; on the computer's keys, the layer whose keys are tested.
        DescribeLegends(board, m_Keyboard, IsLiveShown() ? ShownLayer() : 0, m_Legends);

        // The live test is a view of its own: only what the board reports, no wiring.
        const bool isLive = IsLiveTestRunning();
        if (isLive && m_Method == Method::Computer)
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
        if (m_Method == Method::Switches && !m_SwitchesConfirmed)
            return;
        ImGui::Spacing();

        // On the computer's keys, each layer's count is its own.
        const uint8_t layer = ShownLayer();
        if (m_Method == Method::Computer && layer > 0)
            ImGui::Text("Seen %zu of %zu keys of layer %d. Press every key once.", m_Counts.seenKeys, m_Counts.keys, layer);
        else
            ImGui::Text("Seen %zu of %zu keys. Press every key once.", m_Counts.seenKeys, m_Counts.keys);
        ImGui::SameLine();
        if (ImGui::SmallButton("Start over"))
        {
            m_Readings = {};
            m_HostSeen.clear();
            m_Received.clear();
            m_ReceivedFrom.reset();
            m_ReceivedLine.clear();
        }
        if (m_Method == Method::Computer && !m_ReceivedLine.empty())
            ImGui::TextUnformatted(m_ReceivedLine.c_str());

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
        case Method::Switches:
            ColouredText(PanelColour::Success, "Testing the switches");
            ColouredText(PanelColour::Muted, "The board reports which of its switches are pressed, whatever they send%s",
                         m_SwitchesConfirmed ? "." : " -- if it reports them: press any key.");
            return;
        case Method::Computer:
            ColouredText(PanelColour::Warning, "Testing what the computer receives");
            if (m_Why.empty())
                ColouredText(PanelColour::Muted, "What each key sends, on each layer -- choose Switches at the strip's end "
                                                 "to test the switches themselves, by position.");
            else
                ColouredText(PanelColour::Muted, "%s.", m_Why.c_str());
            ColouredText(PanelColour::Muted, "A key lights when the computer receives what it sends. While a key is held, "
                                             "the board shows the layer it comes from, in violet in the strip; keys a "
                                             "layer leaves transparent are faint and not counted. A key sending "
                                             "nothing -- a layer key, an empty one -- cannot be tested this way, and is "
                                             "hatched; keys sending the same thing light together, marked ?. Keys the "
                                             "system keeps may never arrive.");
            return;
        }
    }

    void MatrixView::UseComputerKeys(std::string why, bool refused)
    {
        m_Method = Method::Computer;
        m_Why    = std::move(why);
        m_Live   = Live::Running;
        if (refused)
            m_SwitchesRefused = m_Why;
    }

    void MatrixView::OnHostKey(uint32_t usage, bool down)
    {
        if (!IsLiveTestRunning() || (m_Method == Method::Switches && m_SwitchesConfirmed))
            return;

        // On the switches, any key, tested or not, tells a refusal from rest.
        if (m_Method == Method::Switches)
        {
            if (down && !m_HostDownAt)
            {
                m_HostDownAt    = ImGui::GetTime();
                m_QuietReadings = 0;
            }
            else if (!down)
                m_HostDownAt.reset();
        }

        const std::vector<std::string_view> names = KeycodesSending(usage, m_Keyboard.keycodeVersion);
        for (const std::string_view name : names)
        {
            if (down)
            {
                m_HostHeld.insert(name);
                m_HostSeen.insert(name);
            }
            else
                m_HostHeld.erase(name);
        }

        // The layer it came from is found with the board, in DescribeBoard(); released, the one picked.
        if (down && !names.empty())
        {
            m_Received  = names;
            m_FindLayer = true;
        }
        if (m_HostHeld.empty())
            m_Peek.reset();
    }

    // The layer a key received came from, as Nazg can tell -- VIA and Vial do not report it: the layer
    // whose own key sends it, not one passing it through a transparent key -- so S after Fn is let go
    // is layer 0 again. Several layers sending it: the one shown if it is one, else the lowest.
    void MatrixView::FollowReceived(const BoardDescription& board)
    {
        if (!std::exchange(m_FindLayer, false) || m_Received.empty())
            return;

        const qmk::KeycodeVersion version = m_Keyboard.keycodeVersion;
        const auto                sends   = [&](const Keycode& keycode)
        {
            const std::optional<std::string_view> name = TestedName(keycode, version);
            return name && std::find(m_Received.begin(), m_Received.end(), *name) != m_Received.end();
        };

        std::vector<uint8_t> owners;
        for (uint8_t layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            for (const BoardKey& key : board.keys)
                if (key.geometry.IsSwitch() && sends(m_Keyboard.KeycodeFor(key.geometry, layer)))
                {
                    owners.push_back(layer);
                    break;
                }

        const uint8_t shown = ShownLayer();
        m_ReceivedFrom.reset();
        if (!owners.empty())
            m_ReceivedFrom = std::find(owners.begin(), owners.end(), shown) != owners.end() ? shown : owners.front();

        // Only while it is held: a tap released before this frame leaves the layer picked.
        if (m_ReceivedFrom && !m_HostHeld.empty())
            m_Peek = *m_ReceivedFrom == m_Layer ? std::nullopt : m_ReceivedFrom;

        // Said in the panel: "Received F1: 1 on layer 1."
        const std::string name = KeycodeLabel(NamedKey{ m_Received.front() }, version);
        if (!m_ReceivedFrom)
        {
            m_ReceivedLine = "Received " + name + ": no key of this board sends it.";
            return;
        }
        std::string keys;
        for (const BoardKey& key : board.keys)
        {
            if (!key.geometry.IsSwitch())
                continue;
            const ResolvedKey resolved = ResolveKey(m_Keyboard, key.geometry, *m_ReceivedFrom);
            if (resolved.fallthrough != Fallthrough::Disabled && sends(resolved.keycode))
                keys += (keys.empty() ? "" : ", ") + KeycodeLabel(m_Keyboard.KeycodeFor(key.geometry, 0), version);
        }
        m_ReceivedLine = "Received " + name + ": " + keys + " on layer " + std::to_string(*m_ReceivedFrom) + ".";
    }

    void MatrixView::DescribeComputerKeys(BoardDescription& board)
    {
        const uint8_t             rows    = m_Keyboard.definition.matrixRows;
        const uint8_t             columns = m_Keyboard.definition.matrixColumns;
        const qmk::KeycodeVersion version = m_Keyboard.keycodeVersion;
        const uint8_t             layer   = ShownLayer();

        // What a key sends on the layer shown, through the transparent keys below it; nothing for one
        // KC_NO disables.
        const auto testedBy = [&](const std::optional<ResolvedKey>& resolved) -> std::optional<std::string_view>
        {
            if (!resolved || resolved->fallthrough == Fallthrough::Disabled)
                return std::nullopt;
            return TestedName(resolved->keycode, version);
        };
        const auto turnOf = [&](const via::DefinitionKey& key, bool clockwise)
        { return testedBy(ResolveTurn(m_Keyboard, key.encoderIndex, clockwise, layer)); };

        // What each key is tested by, and how many keys share it: a switch by its keycode, a Vial
        // knob's direction key by its turn. A key the layer leaves transparent is tested -- it sends
        // what the layer below does -- but not counted: drawn faint, it is not the layer's own.
        std::vector<std::optional<std::string_view>> tested(board.keys.size());
        std::vector<bool>                            passes(board.keys.size(), false);
        std::map<std::string_view, int>              sharing;
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const via::DefinitionKey& key = board.keys[index].geometry;
            if (key.IsSwitch())
            {
                const ResolvedKey resolved = ResolveKey(m_Keyboard, key, layer);
                tested[index] = testedBy(resolved);
                passes[index] = layer > 0 && resolved.fallthrough == Fallthrough::Transparent;
            }
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
            if (key.IsSwitch() && key.row < rows && key.column < columns && !passes[index])
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

    // Opens the board for the switches, keeps it open, and finds out whether it will answer -- back to
    // the computer's keys where it cannot, saying why.
    Task<void> MatrixView::StartLive()
    {
        m_Live              = Live::Starting;
        m_SwitchesConfirmed = false;
        m_HostDownAt.reset();
        m_QuietReadings = 0;

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
                                        ", older than Vial's matrix test: it cannot report its switches",
                                    true);
                    co_return;
                }

                // Locked: back to the computer's keys, the switches still there for once it is unlocked.
                const vial::UnlockStatus status = co_await vial.GetUnlockStatus();
                if (!status.unlocked)
                {
                    UseComputerKeys("The board is locked, and Vial reports its switches only once unlocked -- click "
                                    "Locked in the header, then Switches",
                                    false);
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
                                    std::to_string(rows) + " rows of " + std::to_string(columns) + " columns do not",
                                true);
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

            // The first press decides: shown by the matrix, the switches are read; heard by the
            // computer alone through readings sent while it was down, the firmware refuses its matrix.
            if (m_Method == Method::Switches && !m_SwitchesConfirmed)
            {
                if (AnyPressed(reading))
                    m_SwitchesConfirmed = true;
                else if (m_HostDownAt && sent > *m_HostDownAt && ++m_QuietReadings >= c_QuietReadings)
                    UseComputerKeys(m_IsVial ? "The board did not report that press: this firmware does not report "
                                               "its switches"
                                             : "The board did not report that press: this firmware does not report its "
                                               "switches -- it was built without VIA_INSECURE",
                                    true);
            }
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The board stopped answering: ") + failure.what());
        }
    }
}
