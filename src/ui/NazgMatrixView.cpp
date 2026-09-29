// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMatrixView.h"

#include <exception>
#include <utility>

#include "imgui.h"

#include "adapters/vial/NazgVialProtocol.h"
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
    }

    MatrixView::MatrixView(HidTransport& transport, std::string path, bool isVial, const Keyboard& keyboard,
                           const std::string& hostLayoutId)
        : m_Transport(transport), m_Path(std::move(path)), m_IsVial(isVial), m_Keyboard(keyboard),
          m_HostLayoutId(hostLayoutId)
    {
    }

    MatrixView::~MatrixView()
    {
        // Closing an id that never opened does nothing.
        m_Transport.Close(m_Device);
    }

    Strip MatrixView::DescribeStrip() const
    {
        Strip strip;
        strip.label   = "Matrix";
        strip.entries = { "Wiring", "Live test" };
        strip.chosen  = m_Live == Live::Off || m_WantsWiring ? 0 : 1;
        return strip;
    }

    void MatrixView::OnStripChosen(size_t entry)
    {
        if (entry == 0)
        {
            m_WantsWiring = true;   // done once no request is in flight, in DescribeBoard()
        }
        else if (!IsBusy())
        {
            m_WantsWiring = false;
            m_Readings    = {};
            m_LiveMessage.clear();
            m_Request = StartLive();
        }
    }

    bool MatrixView::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    void MatrixView::StopLiveTest()
    {
        if (IsBusy())
            return;

        m_Transport.Close(m_Device);
        m_Device      = c_InvalidDevice;
        m_Live        = Live::Off;
        m_WantsWiring = false;
    }

    void MatrixView::DescribeBoard(BoardDescription& board)
    {
        if (m_WantsWiring && !IsBusy())
            StopLiveTest();

        // Paced here, as the Vial unlock is: a reading goes out once the last one has come
        // back and the interval has passed since it was asked for.
        if (m_Live == Live::Running && !m_WantsWiring && !IsBusy() &&
            ImGui::GetTime() - m_LastPoll >= c_PollInterval)
        {
            m_LastPoll = ImGui::GetTime();
            m_Request  = Poll();
        }

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

        // The live test is a view of its own: only what the board reports, no wiring.
        const bool isLive = m_Live == Live::Running && !m_WantsWiring;
        m_Counts = DescribeMatrix(board, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns,
                                  IsLiveShown() ? MatrixFocus{} : Focus(), isLive ? &m_Readings : nullptr);
        m_KeyAt  = m_Counts.keyAt ? FormatKeycode(m_Keyboard.KeycodeFor(board.keys[*m_Counts.keyAt].geometry, 0))
                                  : std::string();
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

        if (m_Pinned && !IsLiveShown())
            ColouredText(PanelColour::Muted, "Pinned: click it again to let go.");

        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "Matrix: %d rows, %d columns. Layer 0's legends.",
                     m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);
        if (!m_Keyboard.layoutSelection.empty())
            ColouredText(PanelColour::Muted, "Drawn with the board's layout options; keys of the other choices are "
                                             "not shown.");

        ImGui::Spacing();
        if (ImGui::Button("Close"))
        {
            m_WantsWiring = true;
            m_IsClosed    = true;
        }
    }

    void MatrixView::DrawLivePanel()
    {
        switch (m_Live)
        {
        case Live::Off:
            return;

        case Live::Starting:
            ImGui::TextUnformatted("Opening the board...");
            return;

        case Live::Failed:
            ColouredText(PanelColour::Warning, "%s", m_LiveMessage.c_str());
            return;

        case Live::Running:
            break;
        }

        ImGui::Text("Seen %zu of %zu keys. Press every key once.", m_Counts.seenKeys, m_Counts.keys);
        ImGui::SameLine();
        if (ImGui::SmallButton("Start over"))
            m_Readings = {};

        if (m_Counts.seenKeys == m_Counts.keys && m_Counts.keys > 0)
            ColouredText(PanelColour::Success, "Every key works.");
        else
            ColouredText(PanelColour::Muted, "A row or column label stays grey until all its keys are seen: one "
                                             "that never turns green points at that trace.");

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

        if (m_Counts.seenKeys == 0 && !m_IsVial)
            ColouredText(PanelColour::Muted, "Nothing lights? A VIA firmware sends the matrix only when built with "
                                             "VIA_INSECURE, or with SECURE_ENABLE and unlocked; otherwise every key "
                                             "reads as released.");
    }

    void MatrixView::Fail(std::string message)
    {
        m_Transport.Close(m_Device);
        m_Device      = c_InvalidDevice;
        m_Live        = Live::Failed;
        m_LiveMessage = std::move(message);
    }

    // Opens the board, keeps it open, and finds out whether and how it will answer.
    Task<void> MatrixView::StartLive()
    {
        m_Live = Live::Starting;

        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;

        try
        {
            m_Device = co_await m_Transport.Open(m_Path);

            HidDeviceChannel channel(m_Transport, m_Device);
            VialProtocol     vial(channel);

            if (m_IsVial)
            {
                // A locked Vial board answers with the request echoed back -- every key
                // released -- so the lock is asked first rather than guessed from silence.
                const std::optional<VialIdentity> identity = co_await vial.Detect();
                if (identity && identity->protocolVersion < c_VialMatrixTesterProtocol)
                {
                    Fail("This firmware speaks Vial protocol " + std::to_string(identity->protocolVersion) +
                         "; the matrix tester came with protocol 3.");
                    co_return;
                }

                const VialUnlockStatus status = co_await vial.GetUnlockStatus();
                if (!status.unlocked)
                {
                    Fail("The board is locked, and Vial sends the matrix only once unlocked. Click Locked in the "
                         "header, then choose Live test again.");
                    co_return;
                }

                m_Format = SwitchMatrixFormat::Whole;
            }
            else
            {
                m_Format = SwitchMatrixFormatForVia(co_await vial.GetProtocolVersion());
            }

            if (m_Format == SwitchMatrixFormat::Whole && !WholeMatrixFits(rows, columns))
            {
                Fail("This firmware sends the matrix only when all of it fits in one reply, and " +
                     std::to_string(rows) + " rows of " + std::to_string(columns) + " columns do not.");
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
            HidDeviceChannel channel(m_Transport, m_Device);
            ViaProtocol      via(channel);

            const SwitchMatrixState reading = co_await via.GetSwitchMatrixState(
                m_Format, m_Keyboard.definition.matrixRows, m_Keyboard.definition.matrixColumns);
            AddReading(m_Readings, reading);
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The board stopped answering: ") + failure.what());
        }
    }
}
