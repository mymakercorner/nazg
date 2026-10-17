// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDiagnosticsSection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

#include "imgui.h"

#include "ui/NazgKeyLine.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // A level reading's verdicts are judged again at most this often: on a long recording a
        // judgment costs, most of all in a Debug build (KeyHealthTest prints what).
        constexpr double c_JudgeInterval = 0.5;   // seconds

        // The panel's two halves -- the keys, a key's levels -- never lower than this, scrolling
        // inside (ui-design.md, "Plugins", "Room": the panel keeps one height).
        constexpr float c_ListMinHeight = 150.0f;

        const char* const c_ModeNames[] = { "Key presses", "Signal levels", "Device" };

        // The mockups' sizes are at a 13 px font: everything drawn scales with the interface's.
        float Unit()
        {
            return ImGui::GetFontSize() / 13.0f;
        }

        ImU32 U32(PanelColour colour)
        {
            return ImGui::ColorConvertFloat4ToU32(ColourOf(colour));
        }

        ImU32 Faded(ImU32 colour, float strength)
        {
            const auto alpha = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF);
            return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::lround(alpha * strength)) << IM_COL32_A_SHIFT);
        }

        ImU32 RestColour() { return ImGui::GetColorU32(ImGuiCol_TextDisabled); }
        ImU32 PressColour() { return BoardColours::Highlighted(); }
        ImU32 TextColour() { return ImGui::GetColorU32(ImGuiCol_Text); }
        ImU32 GridColour() { return ImGui::GetColorU32(ImGuiCol_Border); }

        // Its colour on the panel, by verdict: the board's for the problems, muted for the rest.
        ImU32 VerdictColour(KeyVerdict verdict)
        {
            switch (verdict)
            {
            case KeyVerdict::Failing: return BoardColours::Condition(KeyCondition::Failing);
            case KeyVerdict::AtRisk:  return BoardColours::Condition(KeyCondition::AtRisk);
            case KeyVerdict::ToCheck: return BoardColours::Condition(KeyCondition::ToCheck);
            case KeyVerdict::ToDo:    return RestColour();
            case KeyVerdict::Good:    break;
            }
            return RestColour();
        }

        KeyCondition ConditionOf(KeyVerdict verdict)
        {
            switch (verdict)
            {
            case KeyVerdict::Failing: return KeyCondition::Failing;
            case KeyVerdict::AtRisk:  return KeyCondition::AtRisk;
            case KeyVerdict::ToCheck: return KeyCondition::ToCheck;
            case KeyVerdict::ToDo:    break;   // plain: the list's to-do lines name them
            case KeyVerdict::Good:    break;
            }
            return KeyCondition::None;
        }

        const char* WordOf(KeyVerdict verdict)
        {
            switch (verdict)
            {
            case KeyVerdict::Failing: return "failing";
            case KeyVerdict::AtRisk:  return "at risk";
            case KeyVerdict::ToCheck: return "to check";
            case KeyVerdict::ToDo:    return "to do";
            case KeyVerdict::Good:    break;
            }
            return "good";
        }

        int OrderOf(KeyVerdict verdict)
        {
            return static_cast<int>(verdict);   // failing first, good last
        }

        // The words lead with the effect and carry no numbers (Rico, 2026-10-17): those are in the
        // trace and the tooltip.
        std::string SentenceOf(KeyReason reason, KeyVerdict verdict)
        {
            switch (reason)
            {
            case KeyReason::TypesByItself:     return "Types by itself: seen pressed without being touched";
            case KeyReason::NeverReaches:      return "Does not work: its presses never reach its threshold";
            case KeyReason::WentPastUntouched: return "May type by itself: went past its threshold untouched";
            case KeyReason::RestsClose:        return "May type by itself: rests close to its threshold";
            case KeyReason::WandersAtRest:     return "May type by itself: its level wanders at rest";
            case KeyReason::OnlyJustPast:      return "May miss presses: even its firmest press is only just past its threshold";
            case KeyReason::NotPressedYet:     return "Not pressed yet";
            case KeyReason::PressAgain:        return "Press it again, firmly: not past its threshold yet";
            case KeyReason::HoldToMeasure:     return "Registered: hold it a moment to measure it";
            case KeyReason::TopOfRange:
                return verdict == KeyVerdict::ToCheck ? "To check: reads at the top of the range, its real level may be higher"
                                                      : "reads at the top of the range";
            }
            return {};
        }

        // The same problem shared by many keys, said once of the keyboard.
        std::string PluralOf(KeyReason reason)
        {
            switch (reason)
            {
            case KeyReason::TypesByItself:     return "were seen pressed without being touched -- they type by themselves";
            case KeyReason::NeverReaches:      return "never reach their threshold -- they do not work";
            case KeyReason::WentPastUntouched: return "went past their threshold untouched -- they may type by themselves";
            case KeyReason::RestsClose:        return "rest close to their threshold -- they may type by themselves";
            case KeyReason::WandersAtRest:     return "wander at rest -- they may type by themselves";
            case KeyReason::OnlyJustPast:      return "only just get past their threshold -- they may miss presses";
            default:                           break;
            }
            return SentenceOf(reason, KeyVerdict::AtRisk);
        }

        std::string ReasonsOf(const JudgedKey& judged, const char* separator)
        {
            std::string text;
            for (KeyReason reason : judged.reasons)
                text += (text.empty() ? "" : separator) + SentenceOf(reason, judged.verdict);
            return text;
        }

        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
        }

        // Toggles side by side, the one at `on` pressed, as the other panels draw a choice: the
        // index clicked, or -1.
        int Segments(std::initializer_list<const char*> labels, int on)
        {
            int clicked = -1;
            int index   = 0;
            for (const char* label : labels)
            {
                if (index > 0)
                    ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushID(index);
                if (ToggleButton(label, index == on))
                    clicked = index;
                ImGui::PopID();
                ++index;
            }
            return clicked;
        }

        float SegmentsWidth(std::initializer_list<const char*> labels)
        {
            float width = 0.0f;
            for (const char* label : labels)
                width += ButtonWidth(label);
            return width;
        }

        // On the line of the item before, so that `width` ends at the right edge -- never back over
        // that item.
        void RightAlign(float width)
        {
            ImGui::SameLine();
            const float room = ImGui::GetContentRegionAvail().x;
            if (room > width)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - width);
        }

        // A word on the drawing, with its tooltip when the pointer is over it.
        void TippedText(ImDrawList* drawList, ImVec2 at, ImU32 colour, const char* text, const char* tip)
        {
            drawList->AddText(at, colour, text);
            const ImVec2 size = ImGui::CalcTextSize(text);
            if (ImGui::IsMouseHoveringRect(at, ImVec2(at.x + size.x, at.y + size.y)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", tip);
        }

        void DashedLine(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImU32 colour, float dash, float gap, float thickness)
        {
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            for (float done = 0.0f; done < length; done += dash + gap)
            {
                const float t0 = done / length;
                const float t1 = std::min(length, done + dash) / length;
                drawList->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                                  ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1), colour, thickness);
            }
        }

        std::string Seconds(double seconds)
        {
            const long whole = std::lround(seconds);
            return whole < 2 ? "just now" : std::to_string(whole) + " s ago";
        }
    }

    DiagnosticsSection::DiagnosticsSection(HidTransport& transport, std::string path, const Keyboard& keyboard,
                                           const LegendSettings& legends, const bool& advancedTools,
                                           DeviceIdentity identity)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends),
          m_AdvancedTools(advancedTools), m_Identity(std::move(identity))
    {
    }

    // --- The strip --------------------------------------------------------------------------------

    Strip DiagnosticsSection::DescribeStrip() const
    {
        Strip strip;
        strip.label   = "Diagnostics";
        strip.entries = { c_ModeNames[0], c_ModeNames[1], c_ModeNames[2] };
        strip.chosen  = static_cast<size_t>(m_Mode);
        return strip;
    }

    void DiagnosticsSection::OnStripChosen(size_t entry)
    {
        m_Mode       = static_cast<Mode>(std::min<size_t>(entry, 2));
        m_EraseAsked = false;

        // Chosen again after a failure: tried again.
        if (m_Link == Link::Failed)
            m_Link = Link::Closed;
    }

    // --- The board's link -----------------------------------------------------------------------

    bool DiagnosticsSection::IsRequesting() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool DiagnosticsSection::IsBusy() const
    {
        return IsRequesting();
    }

    bool DiagnosticsSection::IsPolling() const
    {
        return IsRequesting() && !m_Leaving;
    }

    bool DiagnosticsSection::IsUnconnected() const
    {
        return !m_Info.thresholds.empty() && leydenjar::IsWiredToNoMatrix(m_Info);
    }

    void DiagnosticsSection::WhileHidden()
    {
        m_HiddenThisFrame = true;
        m_EraseAsked      = false;

        if (m_Link == Link::Failed)
            m_Link = Link::Closed;   // tried again when shown
        if (m_Link == Link::Open || m_Link == Link::Opening)
        {
            m_StopPolling = true;
            if (!IsRequesting())
                Close();
        }
    }

    // Closing the board turns key output on: the channel's farewell.
    void DiagnosticsSection::Close()
    {
        m_Channel.reset();
        m_Link = Link::Closed;
    }

    void DiagnosticsSection::Fail(std::string message)
    {
        m_Channel.reset();
        m_Link    = Link::Failed;
        m_Message = std::move(message);
    }

    // Opens the board and keeps it open: key output off first, then -- once -- what the controller
    // and Vial say of themselves.
    Task<void> DiagnosticsSection::Open()
    {
        m_Link = Link::Opening;
        m_Message.clear();

        try
        {
            m_Channel.emplace(co_await HidDeviceChannel::Open(m_Transport, m_Path));
            leydenjar::Protocol protocol(*m_Channel);
            co_await protocol.TurnKeyOutputOffUntilClosed();

            if (m_Info.thresholds.empty())
            {
                m_Info = co_await leydenjar::ReadControllerInfo(protocol);
                if (m_Keyboard.report.isVial)
                {
                    vial::Protocol vial(*m_Channel);
                    m_Vial           = co_await vial.Detect();
                    m_DefinitionSize = co_await vial.GetDefinitionSize();
                }

                const leydenjar::Details& details = m_Info.details;
                const bool beamSpring = details.technology == leydenjar::SwitchTechnology::BeamSpring;
                m_Scale.direction     = beamSpring ? -1 : 1;

                // The activation offset: a bin's threshold from its resting level. A firmware before
                // the bins reports no resting level: its defaults then, -30 and +10 (ui-design.md,
                // "The levels' window").
                std::vector<int> offsets;
                for (size_t bin = 0; bin < m_Info.referenceLevels.size() && bin < m_Info.thresholds.size(); ++bin)
                    offsets.push_back(std::abs(m_Info.thresholds[bin] - m_Info.referenceLevels[bin]));
                std::sort(offsets.begin(), offsets.end());
                m_Scale.offset = offsets.empty() ? (beamSpring ? 30 : 10) : std::max(1, offsets[offsets.size() / 2]);

                const size_t positions = size_t{ details.controllerColumns } * details.controllerRows;
                m_Traces.assign(positions, {});
                m_Tool.assign(positions, {});
                m_PressedNow.assign(details.logicalRows, 0);
                m_Seen.assign(details.logicalRows, 0);
                m_ControllerNow.assign(details.controllerColumns, 0);
                m_ControllerSeen.assign(details.controllerColumns, 0);
                m_Keys.clear();   // found again, wired
            }

            m_Link = Link::Open;
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The diagnostics could not start: ") + failure.what());
            co_return;
        }

        if (m_StopPolling)
            Close();
    }

    // Each reading asked as soon as the last one is back, as the Leyden Jar tool does -- no fixed
    // rate -- until the section is hidden or the mode no longer polls.
    Task<void> DiagnosticsSection::Poll()
    {
        try
        {
            leydenjar::Protocol       protocol(*m_Channel);
            const leydenjar::Details& details = m_Info.details;

            while (!m_StopPolling && IsPollingMode())
            {
                if (m_Mode == Mode::SignalLevels)
                    AddLevels(co_await protocol.ReadLevels(details.controllerColumns, details.controllerRows));
                else if (m_View == View::Controller)
                    AddControllerPresses(co_await protocol.ReadControllerMatrix(details.controllerColumns));
                else
                    AddKeyPresses(co_await protocol.ReadLogicalMatrix(details.logicalRows));
            }
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The board stopped answering: ") + failure.what());
            co_return;
        }

        if (m_StopPolling)
            Close();
    }

    // Not answered: the board leaves. Hotplug sees it go, and the list says so.
    Task<void> DiagnosticsSection::Leave(bool erase)
    {
        m_Leaving = true;
        try
        {
            leydenjar::Protocol protocol(*m_Channel);
            if (erase)
                co_await protocol.EraseEeprom();
            else
                co_await protocol.EnterBootloader();

            m_Link    = Link::Gone;
            m_Message = erase ? "The " + m_Keyboard.Name() + " was erased and is restarting -- it comes back in the list."
                              : "The " + m_Keyboard.Name() + " left for its bootloader -- it shows as a drive, RPI-RP2. "
                                "Flash it, and it comes back in the list.";
        }
        catch (const std::exception& failure)
        {
            Fail(std::string(erase ? "The EEPROM could not be erased: " : "The bootloader could not be entered: ") +
                 failure.what());
        }
        m_Leaving = false;
    }

    // --- The readings -----------------------------------------------------------------------------

    void DiagnosticsSection::AddKeyPresses(const std::vector<uint32_t>& rows)
    {
        const leydenjar::MatrixMapping& mapping = m_Info.mapping;
        std::fill(m_ControllerNow.begin(), m_ControllerNow.end(), uint8_t{ 0 });

        for (size_t row = 0; row < rows.size() && row < m_PressedNow.size(); ++row)
        {
            m_PressedNow[row] = rows[row];
            m_Seen[row] |= rows[row];

            // The same keys on the controller's grid.
            for (size_t column = 0; column < 32 && column < mapping.columns.size(); ++column)
                if ((rows[row] >> column & 1u) != 0 && row < mapping.rows.size() &&
                    mapping.columns[column] < m_ControllerNow.size() && mapping.rows[row] < 8)
                {
                    m_ControllerNow[mapping.columns[column]] |= static_cast<uint8_t>(1u << mapping.rows[row]);
                    m_ControllerSeen[mapping.columns[column]] |= static_cast<uint8_t>(1u << mapping.rows[row]);
                }
        }
    }

    // The comparators' bits as they come: set for a level above the threshold -- pressed on a Model
    // F, released on a Beam Spring.
    void DiagnosticsSection::AddControllerPresses(const std::vector<uint8_t>& columns)
    {
        const bool                      beamSpring = m_Scale.direction < 0;
        const uint8_t                   rowsMask   = static_cast<uint8_t>((1u << m_Info.details.controllerRows) - 1u);
        const leydenjar::MatrixMapping& mapping    = m_Info.mapping;

        for (size_t column = 0; column < columns.size() && column < m_ControllerNow.size(); ++column)
        {
            m_ControllerNow[column] = static_cast<uint8_t>((beamSpring ? ~columns[column] : columns[column]) & rowsMask);
            m_ControllerSeen[column] |= m_ControllerNow[column];
        }

        // The same keys in QMK's matrix.
        std::fill(m_PressedNow.begin(), m_PressedNow.end(), 0u);
        for (size_t row = 0; row < m_PressedNow.size() && row < mapping.rows.size(); ++row)
            for (size_t column = 0; column < mapping.columns.size() && column < 32; ++column)
            {
                const uint8_t controllerColumn = mapping.columns[column];
                const uint8_t controllerRow    = mapping.rows[row];
                if (controllerColumn < m_ControllerNow.size() && controllerRow < 8 &&
                    (m_ControllerNow[controllerColumn] >> controllerRow & 1u) != 0)
                {
                    m_PressedNow[row] |= 1u << column;
                    m_Seen[row] |= 1u << column;
                }
            }
    }

    void DiagnosticsSection::AddLevels(const std::vector<std::vector<uint16_t>>& levels)
    {
        const leydenjar::Details& details = m_Info.details;
        for (size_t column = 0; column < levels.size() && column < details.controllerColumns; ++column)
            for (size_t row = 0; row < levels[column].size() && row < details.controllerRows; ++row)
            {
                const size_t   position = column * details.controllerRows + row;
                const uint16_t level    = levels[column][row];
                m_Traces[position].push_back(level);

                const uint8_t bin = column < m_Info.bins.size() && row < m_Info.bins[column].size() ? m_Info.bins[column][row] : 0;
                if (bin < m_Info.thresholds.size())
                    m_Tool[position].Add(level, m_Info.thresholds[bin]);
            }
        m_ReadingTimes.push_back(ImGui::GetTime());
    }

    void DiagnosticsSection::Judge()
    {
        std::vector<KeyLevels> levels;
        levels.reserve(m_Keys.size());
        const size_t rows = m_Info.details.controllerRows;
        for (const Key& key : m_Keys)
        {
            KeyLevels one;
            if (key.isMapped)
            {
                one.trace     = m_Traces[size_t{ key.controllerColumn } * rows + key.controllerRow];
                one.threshold = key.threshold;
                one.column    = key.controllerColumn;
            }
            levels.push_back(one);
        }
        m_Health         = JudgeBoard(levels, m_Scale, m_Info.details.controllerColumns);
        m_JudgedReadings = m_ReadingTimes.size();
        m_JudgedAt       = ImGui::GetTime();
    }

    void DiagnosticsSection::ClearReadings()
    {
        for (std::vector<uint16_t>& trace : m_Traces)
            trace.clear();
        for (ToolLevels& tool : m_Tool)
            tool.Clear();
        m_ReadingTimes.clear();
        m_Health         = {};
        m_JudgedReadings = 0;
        m_Selected.reset();
        m_Filter.reset();
    }

    bool DiagnosticsSection::IsPressedNow(size_t key) const
    {
        const Key& k = m_Keys[key];
        if (!k.isMapped)
            return false;
        const std::vector<uint16_t>& trace = m_Traces[size_t{ k.controllerColumn } * m_Info.details.controllerRows + k.controllerRow];
        if (trace.empty())
            return false;
        return m_Scale.direction > 0 ? trace.back() >= k.threshold : trace.back() <= k.threshold;
    }

    // --- The keys ---------------------------------------------------------------------------------

    // Each switch once, by its cell -- switches wired in parallel are one -- named by what it
    // prints, and wired: its controller position, its bin's threshold.
    void DiagnosticsSection::FindKeys(const BoardDescription& board)
    {
        m_Keys.clear();
        m_KeysSelection = m_Keyboard.layoutSelection;
        m_Health        = {};
        m_JudgedReadings = 0;
        m_Selected.reset();

        const leydenjar::MatrixMapping& mapping = m_Info.mapping;
        const leydenjar::Details&       details = m_Info.details;
        for (const BoardKey& described : board.keys)
        {
            const via::DefinitionKey& geometry = described.geometry;
            if (!geometry.IsSwitch() || geometry.row >= m_Keyboard.definition.matrixRows ||
                geometry.column >= m_Keyboard.definition.matrixColumns || KeyAt(geometry.row, geometry.column))
                continue;

            Key key;
            key.row    = geometry.row;
            key.column = geometry.column;
            if (const auto* legend = std::get_if<KeycapLegend>(&described.legends))
                key.name = CaptionOf(*legend);
            if (key.name.empty())
                key.name = "R" + std::to_string(key.row) + " C" + std::to_string(key.column);

            if (key.column < mapping.columns.size() && key.row < mapping.rows.size() &&
                mapping.columns[key.column] < details.controllerColumns && mapping.rows[key.row] < details.controllerRows)
            {
                key.isMapped         = true;
                key.controllerColumn = mapping.columns[key.column];
                key.controllerRow    = mapping.rows[key.row];

                const uint8_t bin = m_Info.bins[key.controllerColumn][key.controllerRow];
                key.bin           = bin < m_Info.thresholds.size() ? bin : 0;
                key.threshold     = m_Info.thresholds.empty() ? 0 : m_Info.thresholds[key.bin];
            }
            m_Keys.push_back(std::move(key));
        }
    }

    std::optional<size_t> DiagnosticsSection::KeyAt(uint8_t row, uint8_t column) const
    {
        for (size_t index = 0; index < m_Keys.size(); ++index)
            if (m_Keys[index].row == row && m_Keys[index].column == column)
                return index;
        return std::nullopt;
    }

    std::optional<size_t> DiagnosticsSection::KeyAtController(uint8_t column, uint8_t row) const
    {
        for (size_t index = 0; index < m_Keys.size(); ++index)
            if (m_Keys[index].isMapped && m_Keys[index].controllerColumn == column && m_Keys[index].controllerRow == row)
                return index;
        return std::nullopt;
    }

    std::optional<size_t> DiagnosticsSection::KeyOf(const BoardKey& described) const
    {
        const via::DefinitionKey& geometry = described.geometry;
        return geometry.IsSwitch() ? KeyAt(geometry.row, geometry.column) : std::nullopt;
    }

    // --- The board --------------------------------------------------------------------------------

    void DiagnosticsSection::DescribeBoard(BoardDescription& board)
    {
        // Nothing starts in a frame Nazg told the section to stop in.
        if (!std::exchange(m_HiddenThisFrame, false))
        {
            if (m_Link == Link::Closed)
            {
                m_StopPolling = false;
                m_Request     = Open();
            }
            else if (m_Link == Link::Open && !IsRequesting() && IsPollingMode() && !IsUnconnected())
            {
                m_StopPolling = false;
                m_Request     = Poll();
            }
        }

        DescribeLegends(board, m_Keyboard, 0, m_Legends);
        if (m_Keys.empty() || m_KeysSelection != m_Keyboard.layoutSelection)
            FindKeys(board);

        if (m_Mode == Mode::SignalLevels && m_ReadingTimes.size() > m_JudgedReadings &&
            (m_JudgedReadings == 0 || ImGui::GetTime() - m_JudgedAt >= c_JudgeInterval))
            Judge();

        // Device draws the board, as the tool does; Signal levels has the board and the controller.
        View view = m_View;
        if (m_Mode == Mode::Device || (m_Mode == Mode::SignalLevels && view == View::QmkMatrix))
            view = View::Board;

        if (view == View::Board)
        {
            m_BoardKeys.clear();
            for (const BoardKey& key : board.keys)
                m_BoardKeys.push_back(KeyOf(key));
        }
        else
        {
            DescribeGrid(board, view == View::Controller);
        }

        if (m_Mode == Mode::Device || m_Link == Link::Gone || IsUnconnected())
            return;
        if (m_Mode == Mode::KeyPresses)
            DescribeKeyPresses(board);
        else
            DescribeLevels(board);
    }

    // A matrix as a plain grid: QMK's, by the definition's rows and columns, or the controller's
    // connectors -- both ruled R0... and C0... (Rico, 2026-10-17). Each cell wears its key's legends;
    // a cell with none is faint.
    void DiagnosticsSection::DescribeGrid(BoardDescription& board, bool controller)
    {
        const leydenjar::Details& details = m_Info.details;
        const uint8_t rows    = controller ? details.controllerRows : m_Keyboard.definition.matrixRows;
        const uint8_t columns = controller ? details.controllerColumns : m_Keyboard.definition.matrixColumns;

        // Each key's legends and class, by its cell, from the board as the definition draws it.
        std::vector<const BoardKey*> keyAt(m_Keys.size(), nullptr);
        for (const BoardKey& described : board.keys)
            if (const std::optional<size_t> key = KeyOf(described); key && keyAt[*key] == nullptr)
                keyAt[*key] = &described;

        BoardDescription grid;
        m_BoardKeys.clear();
        for (uint8_t row = 0; row < rows; ++row)
            for (uint8_t column = 0; column < columns; ++column)
            {
                BoardKey cell;
                cell.geometry.x      = column;
                cell.geometry.y      = row;
                cell.geometry.row    = row;
                cell.geometry.column = column;

                const std::optional<size_t> key = controller ? KeyAtController(column, row) : KeyAt(row, column);
                if (key && keyAt[*key] != nullptr)
                {
                    cell.legends = keyAt[*key]->legends;
                    cell.fill    = keyAt[*key]->fill;
                }
                else
                {
                    cell.marks |= Mark::Dimmed;
                }
                grid.keys.push_back(std::move(cell));
                m_BoardKeys.push_back(key);
            }

        std::vector<int> weak;
        for (const WeakColumn& column : m_Health.columns)
            weak.push_back(column.column);

        for (uint8_t column = 0; column < columns; ++column)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Top;
            label.text = "C" + std::to_string(column);
            label.keys.push_back(column);
            if (controller && m_Mode == Mode::SignalLevels && std::find(weak.begin(), weak.end(), column) != weak.end())
                label.marks |= Mark::Highlighted;
            grid.labels.push_back(std::move(label));
        }
        for (uint8_t row = 0; row < rows; ++row)
        {
            EdgeLabel label;
            label.edge = BoardEdge::Left;
            label.text = "R" + std::to_string(row);
            label.keys.push_back(size_t{ row } * columns);
            grid.labels.push_back(std::move(label));
        }

        board = std::move(grid);
    }

    // Down now, and seen since: as the matrix view's live test.
    void DiagnosticsSection::DescribeKeyPresses(BoardDescription& board)
    {
        const bool controller = m_View == View::Controller;
        for (BoardKey& described : board.keys)
        {
            const via::DefinitionKey& geometry = described.geometry;
            bool pressed = false;
            bool seen    = false;
            if (controller)
            {
                if (geometry.column < m_ControllerNow.size())
                {
                    pressed = (m_ControllerNow[geometry.column] >> geometry.row & 1u) != 0;
                    seen    = (m_ControllerSeen[geometry.column] >> geometry.row & 1u) != 0;
                }
            }
            else if (geometry.IsSwitch() && geometry.row < m_PressedNow.size() && geometry.column < 32)
            {
                pressed = (m_PressedNow[geometry.row] >> geometry.column & 1u) != 0;
                seen    = (m_Seen[geometry.row] >> geometry.column & 1u) != 0;
            }
            if (pressed)
                described.marks |= Mark::Pressed;
            else if (seen)
                described.marks |= Mark::Checked;
        }
    }

    // Coloured by verdict -- only the problems, the keys to test plain (Rico, 2026-10-17: the dashed
    // keys to test were dropped), those pressed ticked -- a key down lit, the
    // selected one outlined; with Advanced tools and Levels, the tool's numbers instead of legends.
    void DiagnosticsSection::DescribeLevels(BoardDescription& board)
    {
        const bool numbers    = m_AdvancedTools && m_LevelsOnKeys;
        const bool controller = m_View == View::Controller;
        const size_t rows     = m_Info.details.controllerRows;

        for (size_t index = 0; index < board.keys.size() && index < m_BoardKeys.size(); ++index)
        {
            BoardKey&                   described = board.keys[index];
            const std::optional<size_t> key       = m_BoardKeys[index];

            if (key && *key < m_Health.keys.size())
            {
                described.condition = ConditionOf(m_Health.keys[*key].verdict);

                // A key with a press registered is ticked, as in Key presses -- the board answers
                // "pressed or not" (Rico, 2026-10-17); a problem keeps its own colour.
                if (described.condition == KeyCondition::None && m_Health.keys[*key].stats.seen)
                    described.marks |= Mark::Checked;
                if (IsPressedNow(*key))
                    described.marks |= Mark::Pressed;
                if (m_Selected == key)
                    described.marks |= Mark::Selected;
                if (m_HoveredKey == key ||
                    (m_HoveredColumn && m_Keys[*key].controllerColumn == *m_HoveredColumn &&
                     m_Health.keys[*key].stats.pressMargin))
                    described.marks |= Mark::Highlighted;
            }

            if (!numbers)
                continue;

            // Max top left, now middle left, min bottom left, the bin bottom right with several.
            std::optional<size_t> position;
            if (controller)
                position = size_t{ described.geometry.column } * rows + described.geometry.row;
            else if (key && m_Keys[*key].isMapped)
                position = size_t{ m_Keys[*key].controllerColumn } * rows + m_Keys[*key].controllerRow;

            described.legends = SlotLegends{};
            if (!position || *position >= m_Tool.size())
                continue;
            const ToolLevels& tool = m_Tool[*position];
            const auto        put  = [&described](LegendSlot slot, std::optional<uint16_t> value)
            {
                if (value)
                    described[slot] = { std::to_string(*value), LegendRole::Value };
            };
            put(LegendSlot::TopLeft, tool.Max());
            put(LegendSlot::MiddleLeft, tool.Now());
            put(LegendSlot::BottomLeft, tool.Min());
            if (m_Info.thresholds.size() > 1)
            {
                const size_t column = *position / rows;
                const size_t row    = *position % rows;
                put(LegendSlot::BottomRight, m_Info.bins[column][row]);
            }
        }
    }

    void DiagnosticsSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (m_Mode == Mode::Device || m_Link == Link::Gone || !events.hoveredKey || *events.hoveredKey >= m_BoardKeys.size())
            return;

        const std::optional<size_t> key = m_BoardKeys[*events.hoveredKey];
        const via::DefinitionKey&   geometry = board.keys[*events.hoveredKey].geometry;

        if (m_Mode == Mode::SignalLevels && key && *key < m_Health.keys.size())
            ImGui::SetTooltip("%s", TooltipOf(*key).c_str());
        else if (key)
        {
            const Key& k = m_Keys[*key];
            if (k.isMapped)
                ImGui::SetTooltip("%s  ·  row %d, column %d  ·  connectors R%d C%d", k.name.c_str(), k.row, k.column,
                                  k.controllerRow, k.controllerColumn);
            else
                ImGui::SetTooltip("%s  ·  row %d, column %d", k.name.c_str(), k.row, k.column);
        }
        else if (m_View != View::Board)
            ImGui::SetTooltip("No key at R%d C%d", geometry.row, geometry.column);

        if (m_Mode == Mode::SignalLevels && events.clickedKey && key)
            m_Selected = m_Selected == key ? std::nullopt : key;
    }

    std::string DiagnosticsSection::TooltipOf(size_t key) const
    {
        const Key&       k      = m_Keys[key];
        const JudgedKey& judged = m_Health.keys[key];
        const KeyStats&  st     = judged.stats;

        std::string text = k.name + "  ·  row " + std::to_string(k.row) + ", column " + std::to_string(k.column);
        if (m_Info.thresholds.size() > 1)
            text += "  ·  bin " + std::to_string(k.bin);
        text += "\n" + std::string(WordOf(judged.verdict));
        if (!judged.reasons.empty())
            text += ": " + ReasonsOf(judged, "; ");
        text += "\nthreshold " + std::to_string(k.threshold) + "  ·  at rest " +
                (st.restMedian ? std::to_string(*st.restMedian) : "-") + "  ·  firmest hold " +
                (st.pressMargin ? std::to_string(*st.pressMargin) + " past" : "-");
        return text;
    }

    // --- The panel --------------------------------------------------------------------------------

    void DiagnosticsSection::DrawPanel()
    {
        ImGui::PushTextWrapPos(0.0f);

        if (m_Link == Link::Gone)
        {
            ImGui::TextUnformatted(m_Message.c_str());
        }
        else if (m_Link == Link::Failed)
        {
            ColouredText(PanelColour::Warning, "%s", m_Message.c_str());
            ColouredText(PanelColour::Muted, "Choose a mode in the strip to try again.");
        }
        else if (m_Info.thresholds.empty())
        {
            ImGui::TextUnformatted("Opening the board...");
        }
        else
        {
            switch (m_Mode)
            {
            case Mode::KeyPresses:   DrawKeyPressesPanel(); break;
            case Mode::SignalLevels: DrawLevelsPanel(); break;
            case Mode::Device:       DrawDevicePanel(); break;
            }
        }

        ImGui::PopTextWrapPos();
    }

    // Off from the section's opening to its leaving, every mode, as the tool keeps it off while
    // connected (Rico, 2026-10-17).
    void DiagnosticsSection::DrawKeyOutputLine()
    {
        ColouredText(PanelColour::Warning, "Key output off: the keyboard types nothing while Diagnostics shows");
    }

    // A controller wired to no matrix: one sentence instead of the analysis (Rico, 2026-10-17).
    void DiagnosticsSection::DrawUnconnected(const char* title, const char* what)
    {
        ImGui::TextUnformatted(title);
        ImGui::Separator();
        ImGui::TextUnformatted("This controller is not connected to a keyboard matrix.");
        ColouredText(PanelColour::Muted,
                     "At start-up its firmware found no switch, and set every threshold to %d so that nothing ever "
                     "registers. %s It measures again each time the keyboard is plugged in.",
                     leydenjar::UnconnectedThreshold(m_Info.details.technology), what);
        ImGui::Separator();
        DrawKeyOutputLine();
    }

    void DiagnosticsSection::DrawKeyPressesPanel()
    {
        if (IsUnconnected())
        {
            DrawUnconnected("Key presses", "There are no key presses to show.");
            return;
        }

        ImGui::TextUnformatted("Key presses");
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "press keys on the keyboard: each lights while it is down");

        RightAlign(SegmentsWidth({ "Board", "QMK matrix", "Controller" }));
        if (const int chosen = Segments({ "Board", "QMK matrix", "Controller" }, static_cast<int>(m_View)); chosen >= 0)
            m_View = static_cast<View>(chosen);
        ImGui::Separator();

        size_t seen = 0;
        for (const Key& key : m_Keys)
            if (key.row < m_Seen.size() && key.column < 32 && (m_Seen[key.row] >> key.column & 1u) != 0)
                ++seen;

        DrawKeyOutputLine();
        char count[64];
        std::snprintf(count, sizeof(count), "seen %zu of %zu keys", seen, m_Keys.size());
        RightAlign(ImGui::CalcTextSize(count).x + ImGui::GetStyle().ItemSpacing.x + ButtonWidth("Clear"));
        ColouredText(PanelColour::Muted, "%s", count);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            std::fill(m_Seen.begin(), m_Seen.end(), 0u);
            std::fill(m_ControllerSeen.begin(), m_ControllerSeen.end(), uint8_t{ 0 });
        }
    }

    // --- Signal levels ----------------------------------------------------------------------------

    void DiagnosticsSection::DrawLevelsPanel()
    {
        if (IsUnconnected())
        {
            DrawUnconnected("Signal levels", "There are no levels to show.");
            return;
        }

        const float u = Unit();

        // The first line: the instruction, then Levels -- with Advanced tools -- and the view.
        ImGui::TextUnformatted("Signal levels");
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "press each key down firmly and hold it a moment, one after the other");

        // Levels, before Board / Controller, only with Advanced tools and off by default, so the
        // default view stays as it is (Rico, 2026-10-17).
        const bool  levels = m_AdvancedTools;
        const float width  = SegmentsWidth({ "Board", "Controller" }) +
                            (levels ? ButtonWidth("Levels") + ImGui::GetStyle().ItemSpacing.x : 0.0f);
        RightAlign(width);
        if (levels)
        {
            if (ToggleButton("Levels", m_LevelsOnKeys))
                m_LevelsOnKeys = !m_LevelsOnKeys;
            ImGui::SetItemTooltip("The Leyden Jar tool's numbers on the keys: the highest reading above the threshold,\n"
                                  "the reading now, the lowest below it, and the bin");
            ImGui::SameLine();
        }
        if (const int chosen = Segments({ "Board", "Controller" }, m_View == View::Controller ? 1 : 0); chosen >= 0)
            m_View = chosen == 1 ? View::Controller : View::Board;
        ImGui::Separator();

        DrawSummary();
        ImGui::Separator();

        // The list and a key's levels share one height, the list scrolling inside it.
        const float lineHeight = ImGui::GetFrameHeightWithSpacing();
        const float height     = std::max(c_ListMinHeight * u, ImGui::GetContentRegionAvail().y - lineHeight - 4 * u);
        const float listWidth  = std::floor(ImGui::GetContentRegionAvail().x * 0.56f);

        ImGui::BeginChild("keys", ImVec2(listWidth, height));
        ImGui::PushTextWrapPos(0.0f);
        DrawKeyList();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("levels", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        DrawKeyLevels();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();

        DrawKeyOutputLine();
    }

    // Good, at risk, failing, to check -- only when there is one -- and to do: the counts, each but
    // good a filter on the list (Rico, 2026-10-17).
    void DiagnosticsSection::DrawSummary()
    {
        size_t counts[5] = {};
        for (const JudgedKey& judged : m_Health.keys)
            ++counts[static_cast<int>(judged.verdict)];
        if (m_Health.keys.empty())
            counts[static_cast<int>(KeyVerdict::ToDo)] = m_Keys.size();

        const float       u        = Unit();
        const KeyVerdict  order[]  = { KeyVerdict::Good, KeyVerdict::AtRisk, KeyVerdict::Failing, KeyVerdict::ToCheck,
                                       KeyVerdict::ToDo };
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        bool              first    = true;

        for (KeyVerdict verdict : order)
        {
            const size_t count = counts[static_cast<int>(verdict)];
            if (verdict == KeyVerdict::ToCheck && count == 0)
                continue;
            if (!first)
                ImGui::SameLine(0.0f, 14 * u);
            first = false;

            char text[48];
            std::snprintf(text, sizeof(text), "%zu %s", count, WordOf(verdict));
            const float  box   = 9 * u;
            const ImVec2 size(box + 5 * u + ImGui::CalcTextSize(text).x, ImGui::GetTextLineHeight());
            const ImVec2 at      = ImGui::GetCursorScreenPos();
            const bool   filters = verdict != KeyVerdict::Good && count > 0;

            ImGui::PushID(static_cast<int>(verdict));
            if (filters)
            {
                if (ImGui::Selectable("##filter", m_Filter == verdict, 0, size))
                {
                    m_Filter = m_Filter == verdict ? std::nullopt : std::optional(verdict);
                    m_Tab    = ListTab::Keys;
                }
                ImGui::SetItemTooltip("Show only these keys");
            }
            else
            {
                ImGui::Dummy(size);
            }
            ImGui::PopID();

            const ImVec2 b0(at.x, at.y + (size.y - box) / 2);
            const ImVec2 b1(b0.x + box, b0.y + box);
            if (verdict == KeyVerdict::Good)
                drawList->AddRectFilled(b0, b1, BoardColours::Fill(KeyFill::Alpha, 0.0f).face, 2 * u);
            else if (verdict != KeyVerdict::ToDo)
                drawList->AddRectFilled(b0, b1, BoardColours::ConditionTint(ConditionOf(verdict)), 2 * u);
            drawList->AddRect(b0, b1, verdict == KeyVerdict::Good ? BoardColours::Outline() : VerdictColour(verdict), 2 * u);
            drawList->AddText(ImVec2(b1.x + 5 * u, at.y), TextColour(), text);
        }

        RightAlign(ButtonWidth("Clear"));
        if (ImGui::Button("Clear"))
            ClearReadings();
        ImGui::SetItemTooltip("Start a new pass: every reading so far forgotten");
    }

    // The list: problems first -- failing, then a weak controller column folded into one row, then
    // at risk and to check -- the weakest first within each.
    std::vector<DiagnosticsSection::ListRow> DiagnosticsSection::ListRows() const
    {
        std::vector<int> weak;
        for (const WeakColumn& column : m_Health.columns)
            weak.push_back(column.column);

        // A key of a weak column whose one problem is its weak press is said by the column's row.
        const auto inWeakColumn = [&](size_t key)
        {
            const JudgedKey& judged = m_Health.keys[key];
            return judged.verdict == KeyVerdict::AtRisk && judged.reasons == std::vector<KeyReason>{ KeyReason::OnlyJustPast } &&
                   std::find(weak.begin(), weak.end(), m_Keys[key].controllerColumn) != weak.end();
        };

        std::vector<size_t> loose;
        for (size_t key = 0; key < m_Health.keys.size(); ++key)
        {
            const KeyVerdict verdict = m_Health.keys[key].verdict;
            if (verdict != KeyVerdict::Good && verdict != KeyVerdict::ToDo && !inWeakColumn(key))
                loose.push_back(key);
        }
        std::stable_sort(loose.begin(), loose.end(), [this](size_t a, size_t b)
        {
            const JudgedKey& x = m_Health.keys[a];
            const JudgedKey& y = m_Health.keys[b];
            return OrderOf(x.verdict) != OrderOf(y.verdict) ? OrderOf(x.verdict) < OrderOf(y.verdict) : x.health < y.health;
        });

        std::vector<ListRow> rows;
        for (size_t key : loose)
            if (m_Health.keys[key].verdict == KeyVerdict::Failing)
                rows.push_back({ key, std::nullopt });
        for (const WeakColumn& column : m_Health.columns)
            rows.push_back({ std::nullopt, column.column });
        for (size_t key : loose)
            if (m_Health.keys[key].verdict != KeyVerdict::Failing)
                rows.push_back({ key, std::nullopt });

        if (!m_Filter)
            return rows;
        std::vector<ListRow> filtered;
        for (const ListRow& row : rows)
            if (row.column ? *m_Filter == KeyVerdict::AtRisk : m_Health.keys[*row.key].verdict == *m_Filter)
                filtered.push_back(row);
        return filtered;
    }

    void DiagnosticsSection::DrawKeyList()
    {
        const float u = Unit();
        m_HoveredKey.reset();
        m_HoveredColumn.reset();

        // Keys and Whole board: two answers to where the problems are, sharing one place.
        if (const int chosen = Segments({ "Keys", "Whole board" }, m_Tab == ListTab::WholeBoard ? 1 : 0); chosen >= 0)
            m_Tab = chosen == 1 ? ListTab::WholeBoard : ListTab::Keys;
        ImGui::SameLine();

        if (m_Tab == ListTab::WholeBoard)
        {
            ColouredText(PanelColour::Muted, "one dot per key: hover for its name, click to select it");
            ImGui::BeginChild("dots");
            ImGui::PushTextWrapPos(0.0f);
            DrawWholeBoard();
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            return;
        }

        const std::vector<ListRow> rows = ListRows();
        ColouredText(PanelColour::Muted, rows.empty() ? "the weakest first" : "the weakest first, hover a dot for its numbers");

        // When a quarter of the keys share one problem it is the keyboard's: said once.
        if (!m_Filter)
            for (const BoardNote& note : m_Health.notes)
                ColouredText(PanelColour::Warning, "%zu of %zu keys %s.", note.keys, m_Health.judged,
                             PluralOf(note.reason).c_str());

        if (!rows.empty())
            DrawBarsHeader();

        ImGui::BeginChild("rows");
        ImGui::PushTextWrapPos(0.0f);
        for (size_t index = 0; index < rows.size(); ++index)
        {
            const ListRow& row = rows[index];
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 at       = ImGui::GetCursorScreenPos();
            const float  rowWidth = ImGui::GetContentRegionAvail().x;
            const float  rowH     = std::max(ImGui::GetTextLineHeight(), 16 * u) + 4 * u;

            if (row.column)
            {
                const WeakColumn* weak = nullptr;
                for (const WeakColumn& column : m_Health.columns)
                    if (column.column == *row.column)
                        weak = &column;

                // One row for the column: its keys light on the board under the pointer, not listed
                // one by one, which would push the other problems out of sight.
                if (ImGui::Selectable("##column", false, 0, ImVec2(rowWidth, rowH)))
                    m_View = View::Controller;
                if (ImGui::IsItemHovered())
                    m_HoveredColumn = *row.column;
                char name[48];
                std::snprintf(name, sizeof(name), "Column C%d  ·  %zu keys", *row.column, weak ? weak->keys : size_t{ 0 });
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddText(ImVec2(at.x + 4 * u, at.y + 2 * u), VerdictColour(KeyVerdict::AtRisk), name);
                drawList->AddText(ImVec2(at.x + 158 * u, at.y + 2 * u), RestColour(),
                                  "its keys register more weakly than the rest of the keyboard");
            }
            else
            {
                const size_t     key    = *row.key;
                const JudgedKey& judged = m_Health.keys[key];
                if (ImGui::Selectable("##key", m_Selected == key, 0, ImVec2(rowWidth, rowH)))
                    m_Selected = m_Selected == key ? std::nullopt : std::optional(key);
                const bool hovered = ImGui::IsItemHovered();
                if (hovered)
                    m_HoveredKey = key;

                ImDrawList*  drawList = ImGui::GetWindowDrawList();
                const ImVec2 nameAt(at.x + 4 * u, at.y + 2 * u);
                drawList->PushClipRect(nameAt, ImVec2(at.x + 150 * u, at.y + rowH), true);
                drawList->AddText(nameAt, VerdictColour(judged.verdict), m_Keys[key].name.c_str());
                drawList->PopClipRect();
                if (hovered && ImGui::GetIO().MousePos.x < at.x + 150 * u)
                    ImGui::SetTooltip("%s", ReasonsOf(judged, "\n").c_str());

                DrawBar(key, ImVec2(at.x + 158 * u, at.y + 2 * u), std::max(0.0f, rowWidth - 158 * u));
            }
            ImGui::PopID();
        }

        // Keys not pressed yet are a to-do, not a problem: one line each kind, plain on the board.
        const bool showToDo = !m_Filter || *m_Filter == KeyVerdict::ToDo;
        bool       anyToDo  = false;
        if (showToDo)
        {
            const std::pair<KeyToDo, const char*> kinds[] = { { KeyToDo::PressAgain, "Press again, firmly" },
                                                               { KeyToDo::Hold, "Hold a moment to measure" },
                                                               { KeyToDo::Press, "Not pressed yet" } };
            for (const auto& [kind, words] : kinds)
            {
                std::vector<std::string> names;
                if (m_Health.keys.empty() && kind == KeyToDo::Press)
                    for (const Key& key : m_Keys)
                        names.push_back(key.name);
                for (size_t key = 0; key < m_Health.keys.size(); ++key)
                    if (m_Health.keys[key].verdict == KeyVerdict::ToDo && m_Health.keys[key].toDo == kind)
                        names.push_back(m_Keys[key].name);
                if (names.empty())
                    continue;
                anyToDo = true;

                std::string line = std::string(words) + ": ";
                for (size_t index = 0; index < names.size() && index < 8; ++index)
                    line += (index == 0 ? "" : ", ") + names[index];
                if (names.size() > 8)
                    line += " and " + std::to_string(names.size() - 8) + " more";
                ColouredText(PanelColour::Muted, "%s.", line.c_str());
            }
        }
        if (rows.empty() && !(showToDo && anyToDo))
            ColouredText(PanelColour::Muted, "No key looks at risk.");

        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    namespace
    {
        // The bars' geometry (key-health.html, barSvg()): the threshold at a fixed place, the resting
        // side left of it scaled to the offset, the pressed side right of it to most keys' press.
        struct BarScale
        {
            float u = 1.0f, width = 300.0f, x0 = 110.0f, left = 102.0f, right = 180.0f;
            int   offset = 10, typical = 1;

            float Rest(float margin) const { return u * (x0 - std::min(left, margin / (offset * 1.6f) * left)); }
            float Press(float margin) const
            {
                return u * (x0 + std::clamp(margin / (std::max(1, typical) * 1.15f) * right, -left, right));
            }
        };

        BarScale BarScaleOf(float u, int offset, int typical)
        {
            BarScale scale;
            scale.u       = u;
            scale.left    = scale.x0 - 8;
            scale.right   = scale.width - scale.x0 - 10;
            scale.offset  = std::max(1, offset);
            scale.typical = typical;
            return scale;
        }

        bool IsNear(ImVec2 point, ImVec2 centre, float radius)
        {
            return std::hypot(point.x - centre.x, point.y - centre.y) <= radius;
        }
    }

    // The bars' header, on their own geometry: each part named where it sits, what it tells in a
    // tooltip on its name (Rico, 2026-10-17: a legend of symbols was unreadable).
    void DiagnosticsSection::DrawBarsHeader()
    {
        const float    u     = Unit();
        const BarScale scale = BarScaleOf(u, m_Scale.offset, m_Health.typical);
        const ImVec2   start = ImGui::GetCursorScreenPos();
        const ImVec2   at(start.x + 158 * u, start.y);
        ImDrawList*    drawList = ImGui::GetWindowDrawList();
        const float    x0       = at.x + scale.x0 * u;
        const float    xu       = at.x + scale.Press(static_cast<float>(scale.typical));

        const char* rest  = "at rest";
        const char* press = "pressed";
        drawList->AddCircleFilled(ImVec2(x0 - 16 * u - ImGui::CalcTextSize(rest).x, at.y + 8 * u), 4 * u, RestColour());
        TippedText(drawList, ImVec2(x0 - 8 * u - ImGui::CalcTextSize(rest).x, at.y + 1 * u), RestColour(), rest,
                   "How far the key stays from its threshold when untouched, the band how much it wanders.\n"
                   "Further is safer: closer, it may type by itself.");
        TippedText(drawList, ImVec2(x0 + 8 * u, at.y + 1 * u), PressColour(), press,
                   "How far past its threshold the key usually gets when pressed, the band how much its presses vary.\n"
                   "Further is safer: closer, it may miss presses.");
        drawList->AddCircleFilled(ImVec2(x0 + 16 * u + ImGui::CalcTextSize(press).x, at.y + 8 * u), 4.5f * u, PressColour());

        const char* threshold = "threshold";
        const char* most      = "most keys";
        drawList->AddLine(ImVec2(x0, at.y + 2 * u), ImVec2(x0, at.y + 16 * u), GridColour());
        TippedText(drawList, ImVec2(x0 - ImGui::CalcTextSize(threshold).x / 2, at.y + 16 * u), TextColour(), threshold,
                   "The level at which the controller counts the key as pressed.");
        drawList->AddLine(ImVec2(x0, at.y + 30 * u), ImVec2(x0, at.y + 35 * u), TextColour(), 1.5f * u);
        TippedText(drawList, ImVec2(xu - ImGui::CalcTextSize(most).x / 2, at.y + 16 * u), RestColour(), most,
                   "Where most keys of this keyboard usually get to when pressed.");
        DashedLine(drawList, ImVec2(xu, at.y + 30 * u), ImVec2(xu, at.y + 35 * u), RestColour(), 2 * u, 2 * u, 1.0f);

        ImGui::Dummy(ImVec2(0.0f, 36 * u));
    }

    // A key's two margins on one track: its usual level at rest, the band how much it wanders; its
    // usual hold, the band how much its holds vary -- or, never across, a hollow dot where it stopped.
    void DiagnosticsSection::DrawBar(size_t key, ImVec2 at, float room)
    {
        const float      u      = Unit();
        const BarScale   scale  = BarScaleOf(u, m_Scale.offset, m_Health.typical);
        const JudgedKey& judged = m_Health.keys[key];
        const KeyStats&  st     = judged.stats;
        const float      cy     = at.y + 8 * u;
        const float      width  = std::min(scale.width * u, room);
        ImDrawList*      drawList = ImGui::GetWindowDrawList();
        const ImVec2     mouse    = ImGui::GetIO().MousePos;
        const bool       inWindow = ImGui::IsWindowHovered();

        drawList->PushClipRect(at, ImVec2(at.x + width, at.y + 16 * u), true);
        drawList->AddLine(ImVec2(at.x + 4 * u, cy), ImVec2(at.x + width - 4 * u, cy), GridColour(), 4 * u);
        const float xu = at.x + scale.Press(static_cast<float>(scale.typical));
        DashedLine(drawList, ImVec2(xu, at.y + 3 * u), ImVec2(xu, at.y + 13 * u), RestColour(), 2 * u, 2 * u, 1.0f);
        drawList->AddLine(ImVec2(at.x + scale.x0 * u, at.y), ImVec2(at.x + scale.x0 * u, at.y + 16 * u), TextColour(), 1.5f * u);

        std::string tip;
        if (st.medianMargin)
        {
            const float a      = at.x + scale.Rest(static_cast<float>(*st.medianMargin));
            const float spread = std::min(scale.left, st.restSpread / (scale.offset * 1.6f) * scale.left) * u;
            drawList->AddRectFilled(ImVec2(a - spread / 2, cy - 3 * u), ImVec2(a - spread / 2 + std::max(2 * u, spread), cy + 3 * u),
                                    Faded(RestColour(), 0.35f), 3 * u);

            std::optional<float> p;
            if (st.pressMedianMargin)
            {
                const float x1 = at.x + scale.Press(static_cast<float>(st.pressBand->first));
                const float x2 = at.x + scale.Press(static_cast<float>(st.pressBand->second));
                drawList->AddRectFilled(ImVec2(std::min(x1, x2), cy - 3 * u),
                                        ImVec2(std::min(x1, x2) + std::max(2 * u, std::abs(x2 - x1)), cy + 3 * u),
                                        Faded(PressColour(), 0.3f), 3 * u);
                p = at.x + scale.Press(static_cast<float>(*st.pressMedianMargin));
            }
            else if (st.closest)
            {
                p = at.x + scale.Press(static_cast<float>(-*st.closest));
            }

            if (p)
            {
                const ImU32 line = judged.verdict == KeyVerdict::Failing ? VerdictColour(KeyVerdict::Failing)
                                   : judged.verdict == KeyVerdict::AtRisk ? VerdictColour(KeyVerdict::AtRisk)
                                                                           : PressColour();
                drawList->AddLine(ImVec2(a, cy), ImVec2(*p, cy), line, 2 * u);
                if (st.pressMedianMargin)
                    drawList->AddCircleFilled(ImVec2(*p, cy), 4.5f * u, PressColour());
                else
                {
                    drawList->AddCircleFilled(ImVec2(*p, cy), 4.5f * u, ImGui::GetColorU32(ImGuiCol_ChildBg) | IM_COL32_A_MASK);
                    drawList->AddCircle(ImVec2(*p, cy), 4.5f * u, VerdictColour(KeyVerdict::Failing), 0, 2 * u);
                }

                if (inWindow && IsNear(mouse, ImVec2(*p, cy), 6 * u))
                {
                    if (st.pressMedianMargin)
                    {
                        tip = "Pressed: usually " + std::to_string(*st.pressMedianMargin) + " past its threshold";
                        if (st.pressBand->second > st.pressBand->first)
                            tip += ", its presses from " + std::to_string(st.pressBand->first) + " to " +
                                   std::to_string(st.pressBand->second);
                        tip += " -- most keys " + std::to_string(m_Health.typical);
                    }
                    else
                    {
                        tip = *st.closest <= 0 ? "Pressed: touched its threshold, never held past it"
                                               : "Pressed: never got past its threshold -- stopped " +
                                                     std::to_string(*st.closest) + " short";
                    }
                }
            }

            drawList->AddCircleFilled(ImVec2(a, cy), 4 * u, RestColour());
            if (inWindow && IsNear(mouse, ImVec2(a, cy), 6 * u))
                tip = "At rest: " + std::to_string(*st.medianMargin) + " from its threshold" +
                      (st.restSpread ? ", wandering by " + std::to_string(st.restSpread) : std::string());
        }
        else if (st.stuck)
        {
            const ImVec2 dot(at.x + scale.Press(scale.typical * 0.8f), cy);
            drawList->AddCircleFilled(dot, 4.5f * u, VerdictColour(KeyVerdict::Failing));
            if (inWindow && IsNear(mouse, dot, 6 * u))
                tip = "Seen pressed without being touched";
        }
        drawList->PopClipRect();

        if (!tip.empty())
            ImGui::SetTooltip("%s", tip.c_str());
    }

    // One dot per key, two rows -- at rest, pressed -- the threshold between them, each row on its
    // own scale; the orange zones are where a dot means at risk (Rico, 2026-10-17: it replaced the
    // histograms, whose bars said neither what they counted nor which keys they held).
    void DiagnosticsSection::DrawWholeBoard()
    {
        const float  u        = Unit();
        const float  w        = std::max(300 * u, std::min(500 * u, ImGui::GetContentRegionAvail().x));
        const float  rowH     = 46 * u;
        const float  lineH    = ImGui::GetTextLineHeight();
        const ImVec2 at       = ImGui::GetCursorScreenPos();
        ImDrawList*  drawList = ImGui::GetWindowDrawList();

        // The rows' names in a gutter of their own, the hints on a line above the rows: no dot ever
        // lands on a word.
        const char* restName  = "at rest";
        const char* pressName = "pressed";
        const float gutter    = std::max(ImGui::CalcTextSize(restName).x, ImGui::CalcTextSize(pressName).x) + 12 * u;
        const float left      = at.x + gutter;   // the dots' area
        const float right     = at.x + w;
        const float x0        = left + (right - left) * 0.4f;   // the threshold, absolute

        float restMax  = m_Scale.offset * 1.6f;
        float pressMax = m_Health.typical * 1.6f;
        for (const JudgedKey& judged : m_Health.keys)
        {
            restMax  = std::max(restMax, static_cast<float>(judged.stats.medianMargin.value_or(0)));
            pressMax = std::max(pressMax, static_cast<float>(judged.stats.pressMargin.value_or(0)));
        }
        pressMax = std::max(pressMax, 1.0f);

        // Each side on its own scale, kept inside the dots' area -- a key past its threshold untouched
        // stops at the right edge.
        const float inset = 8 * u;
        const auto  xr    = [&](float margin)
        {
            return std::clamp(x0 - margin / restMax * (x0 - left - inset), left + inset, right - inset);
        };
        const auto xp = [&](float margin)
        {
            return std::clamp(x0 + std::clamp(margin / pressMax, -1.0f, 1.0f) * (right - x0 - inset), left + inset,
                              right - inset);
        };
        const float top    = at.y + lineH + 4 * u;   // under the hints
        const float yRest  = top + rowH / 2;
        const float yPress = top + rowH + 10 * u + rowH / 2;
        const float bottom = yPress + rowH / 2;
        const ImU32 zone   = BoardColours::ConditionTint(KeyCondition::AtRisk);
        const ImU32 red    = VerdictColour(KeyVerdict::Failing);

        drawList->AddRectFilled(ImVec2(xr(m_Scale.offset * 0.5f), yRest - rowH / 2), ImVec2(x0, yRest + rowH / 2), zone);
        drawList->AddRectFilled(ImVec2(x0, yPress - rowH / 2), ImVec2(xp(static_cast<float>(c_PressSafe)), bottom), zone);
        drawList->AddLine(ImVec2(x0, top), ImVec2(x0, bottom + 4 * u), red, 1.5f * u);

        const char* threshold = "threshold";
        drawList->AddText(ImVec2(x0 - ImGui::CalcTextSize(threshold).x / 2, bottom + 6 * u), red, threshold);
        drawList->AddText(ImVec2(at.x, yRest - lineH / 2), RestColour(), restName);
        drawList->AddText(ImVec2(at.x, yPress - lineH / 2), RestColour(), pressName);

        const char* restSafer  = "<- further from it is safer";
        const char* pressSafer = "further past it is safer ->";
        drawList->AddText(ImVec2(std::max(left, x0 - 8 * u - ImGui::CalcTextSize(restSafer).x), at.y), RestColour(), restSafer);
        drawList->AddText(ImVec2(x0 + 8 * u, at.y), RestColour(), pressSafer);

        const ImVec2              mouse = ImGui::GetIO().MousePos;
        std::optional<size_t>     under;
        float                     nearest = 7 * u;
        for (size_t key = 0; key < m_Health.keys.size(); ++key)
        {
            const JudgedKey& judged = m_Health.keys[key];
            const float      jitter = static_cast<float>(static_cast<int>(key * 37 % 13) - 6) * 2.6f * u;
            const bool       good   = judged.verdict == KeyVerdict::Good;
            const bool       chosen = m_Selected == key;
            const float      r      = (chosen ? 6.0f : good ? 3.0f : 4.5f) * u;
            const ImU32      colour = good ? Faded(RestColour(), 0.55f) : VerdictColour(judged.verdict);

            const auto dot = [&](float x, float y, bool hollow)
            {
                const ImVec2 centre(x, y + jitter * (good ? 1.0f : 0.4f));
                drawList->AddCircleFilled(centre, r, hollow ? (ImGui::GetColorU32(ImGuiCol_ChildBg) | IM_COL32_A_MASK) : colour);
                drawList->AddCircle(centre, r, chosen ? BoardColours::Selected() : colour, 0, (chosen ? 2.5f : 1.5f) * u);
                const float distance = std::hypot(mouse.x - centre.x, mouse.y - centre.y);
                if (distance < nearest)
                {
                    nearest = distance;
                    under   = key;
                }
            };

            const KeyStats& st = judged.stats;
            if (st.stuck)
            {
                dot(std::min(x0 + 30 * u, right - inset), yRest, false);
                continue;
            }
            if (st.medianMargin)
                dot(xr(static_cast<float>(*st.medianMargin)), yRest, false);
            if (st.pressMargin)
                dot(xp(static_cast<float>(*st.pressMargin)), yPress, false);
            else if (st.closest)
                dot(xp(-static_cast<float>(std::max(0, *st.closest))) - (*st.closest <= 0 ? 6 * u : 0.0f), yPress, true);
        }

        ImGui::InvisibleButton("##dots", ImVec2(w, bottom + 8 * u + lineH - at.y));
        if (under && ImGui::IsItemHovered())
        {
            const JudgedKey& judged = m_Health.keys[*under];
            ImGui::SetTooltip("%s  ·  %s%s%s", m_Keys[*under].name.c_str(), WordOf(judged.verdict),
                              judged.reasons.empty() ? "" : ": ", ReasonsOf(judged, "; ").c_str());
            m_HoveredKey = *under;
            if (ImGui::IsItemClicked())
                m_Selected = m_Selected == under ? std::nullopt : under;
        }
        ColouredText(PanelColour::Muted, "Healthy keys crowd far from the threshold on both rows. A dot in the orange zone "
                                         "is at risk; a hollow dot on the pressed row never got past the threshold.");
    }

    // A key's levels: its own last presses, side by side, whenever they were -- a window of the last
    // seconds would lose a key pressed a minute ago -- each saying what it held past the threshold,
    // too short to measure, or not across; at their left its resting readings (Rico, 2026-10-17).
    void DiagnosticsSection::DrawKeyLevels()
    {
        if (!m_Selected || *m_Selected >= m_Health.keys.size() || !m_Keys[*m_Selected].isMapped)
        {
            ImGui::TextUnformatted("A key's levels");
            ColouredText(PanelColour::Muted, "Click a key, on the board or in the list, to see its levels.");
            return;
        }

        const float      u      = Unit();
        const Key&       k      = m_Keys[*m_Selected];
        const JudgedKey& judged = m_Health.keys[*m_Selected];
        const KeyStats&  st     = judged.stats;
        const std::vector<uint16_t>& trace =
            m_Traces[size_t{ k.controllerColumn } * m_Info.details.controllerRows + k.controllerRow];
        const size_t n = std::min(trace.size(), m_JudgedReadings);   // the readings judged

        ImGui::TextUnformatted(k.name.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, judged.verdict == KeyVerdict::Good ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                                                : VerdictColour(judged.verdict));
        ImGui::Text("·  %s", WordOf(judged.verdict));
        ImGui::PopStyleColor();

        // The scale: every reading of the key, its threshold and its bin's resting level.
        int lo = k.threshold;
        int hi = k.threshold;
        for (size_t i = 0; i < n; ++i)
            if (trace[i] > 0)
            {
                lo = std::min<int>(lo, trace[i]);
                hi = std::max<int>(hi, trace[i]);
            }
        if (k.bin < m_Info.referenceLevels.size())
        {
            lo = std::min<int>(lo, m_Info.referenceLevels[k.bin]);
            hi = std::max<int>(hi, m_Info.referenceLevels[k.bin]);
        }
        lo -= 5;
        hi += 5;

        const float  H = 128 * u, top = 14 * u, bottom = 24 * u;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const auto   Y  = [&](int v) { return at.y + top + (1.0f - static_cast<float>((v > 0 ? v : lo) - lo) / (hi - lo)) * (H - top - bottom); };
        ImDrawList*  drawList = ImGui::GetWindowDrawList();
        const ImU32  red      = VerdictColour(KeyVerdict::Failing);

        // At rest: its resting readings' range as a band, its usual level as a line.
        std::vector<bool> inPress(n);
        for (const PressAttempt& press : st.presses)
            for (size_t x = press.first; x <= press.last && x < n; ++x)
                inPress[x] = true;
        std::optional<int> rLo, rHi;
        for (size_t i = 0; i < n; ++i)
            if (!inPress[i])
            {
                rLo = std::min<int>(rLo.value_or(trace[i]), trace[i]);
                rHi = std::max<int>(rHi.value_or(trace[i]), trace[i]);
            }
        drawList->AddText(ImVec2(at.x + 4 * u, at.y), RestColour(), "at rest");
        if (rLo && st.restMedian)
        {
            drawList->AddRectFilled(ImVec2(at.x + 10 * u, std::min(Y(*rHi), Y(*rLo))),
                                    ImVec2(at.x + 38 * u, std::max(Y(*rHi), Y(*rLo)) + 2 * u), Faded(RestColour(), 0.35f));
            drawList->AddLine(ImVec2(at.x + 10 * u, Y(*st.restMedian)), ImVec2(at.x + 38 * u, Y(*st.restMedian)), RestColour(), 2 * u);
            const std::string range = std::to_string(*rLo) + "-" + std::to_string(*rHi);
            drawList->AddText(ImVec2(at.x + 24 * u - ImGui::CalcTextSize(range.c_str()).x / 2, at.y + H - 18 * u), RestColour(),
                              range.c_str());
        }
        DashedLine(drawList, ImVec2(at.x + 6 * u, Y(k.threshold)), ImVec2(at.x + 42 * u, Y(k.threshold)), red, 5 * u, 3 * u, 1.0f);

        static const std::vector<PressAttempt> c_None;
        const std::vector<PressAttempt>&       presses = st.stuck ? c_None : st.presses;
        if (presses.empty())
        {
            drawList->AddText(ImVec2(at.x + 60 * u, at.y + H / 2 - 7 * u), RestColour(),
                              st.stuck ? "Seen pressed all the time: no press to show" : "Not pressed yet");
        }
        else
        {
            const size_t first = presses.size() > 4 ? presses.size() - 4 : 0;
            const float  pw = 76 * u, gap = 6 * u, x0 = 52 * u;
            const double now = m_ReadingTimes.empty() ? 0.0 : m_ReadingTimes[std::min(n, m_ReadingTimes.size()) - 1];
            for (size_t j = first; j < presses.size(); ++j)
            {
                const PressAttempt& press = presses[j];
                const size_t        a     = press.first >= 2 ? press.first - 2 : 0;
                const size_t        z     = std::min(n - 1, press.last + 2);
                const float         left  = at.x + x0 + static_cast<float>(j - first) * (pw + gap);
                const auto          X     = [&](size_t i) { return left + static_cast<float>(i - a) / std::max<size_t>(1, z - a) * pw; };

                drawList->AddRect(ImVec2(left - 2 * u, at.y + top - 4 * u), ImVec2(left + pw + 2 * u, at.y + H - bottom + 4 * u),
                                  GridColour(), 4 * u);
                DashedLine(drawList, ImVec2(left - 2 * u, Y(k.threshold)), ImVec2(left + pw + 2 * u, Y(k.threshold)), red,
                           5 * u, 3 * u, 1.0f);

                std::vector<ImVec2> points;
                for (size_t i = a; i <= z; ++i)
                    points.emplace_back(X(i), Y(trace[i]));
                drawList->AddPolyline(points.data(), static_cast<int>(points.size()),
                                      press.registered ? PressColour() : RestColour(), 0, 1.6f * u);
                for (size_t i = a; i <= z; ++i)
                    if (std::find(st.outliers.begin(), st.outliers.end(), i) != st.outliers.end())
                        drawList->AddCircle(ImVec2(X(i), Y(trace[i])), 4 * u, BoardColours::HighlightedSecond(), 0, 1.5f * u);

                std::string what = !press.registered ? "not across"
                                   : press.held ? "held " + std::to_string(m_Scale.direction * (*press.held - k.threshold)) + " past"
                                                : "too short to measure";
                const ImU32 colour = press.registered ? ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Success)) : RestColour();
                drawList->AddText(ImVec2(left + pw / 2 - ImGui::CalcTextSize(what.c_str()).x / 2, at.y + H - bottom + 6 * u),
                                  colour, what.c_str());
                const std::string ago = press.first < m_ReadingTimes.size() ? Seconds(now - m_ReadingTimes[press.first]) : "";
                drawList->AddText(ImVec2(left + pw / 2 - ImGui::CalcTextSize(ago.c_str()).x / 2, at.y + H - 6 * u), RestColour(),
                                  ago.c_str());
            }
        }
        ImGui::Dummy(ImVec2(0.0f, H + 8 * u));

        std::string caption = ReasonsOf(judged, ". ");
        if (!caption.empty())
            caption += ". ";
        if (!presses.empty())
            caption += "The dashed line: its threshold, " + std::to_string(k.threshold) +
                       ". The band at rest: its resting readings, the line its usual level.";
        if (!st.outliers.empty())
            caption += " Circled: the spring's overshoot, left out.";
        ColouredText(PanelColour::Muted, "%s", caption.c_str());
    }

    // --- Device -----------------------------------------------------------------------------------

    void DiagnosticsSection::DrawDevicePanel()
    {
        ImGui::TextUnformatted("Leyden Jar controller");
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "its firmware's own commands, beside Vial's");
        ImGui::Separator();
        DrawKeyOutputLine();
        ImGui::Separator();

        const leydenjar::ControllerInfo& info    = m_Info;
        const leydenjar::Details&        details = info.details;
        char                             text[160];

        const auto line = [&text](const char* name, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ColouredText(PanelColour::Muted, "%s", name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
            (void)text;
        };

        // Unwrapped: in a column sized to fit, wrapping would fit it to one character.
        ImGui::PushTextWrapPos(-1.0f);
        if (ImGui::BeginTable("device", 3, ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Leyden Jar");
            if (ImGui::BeginTable("controller", 2, ImGuiTableFlags_SizingFixedFit))
            {
                std::snprintf(text, sizeof(text), "%u.%u.%u", info.version.major, info.version.mid, info.version.minor);
                line("Protocol", text);
                line("Switches", details.technology == leydenjar::SwitchTechnology::BeamSpring ? "Beam Spring" : "Model F");
                const char* wiring = info.mapping.wiring == static_cast<uint8_t>(leydenjar::MatrixWiring::LeydenJar) ? "the Leyden Jar's own"
                                     : info.mapping.wiring == static_cast<uint8_t>(leydenjar::MatrixWiring::XWhatsit) ? "an xwhatsit's"
                                     : info.mapping.wiring == static_cast<uint8_t>(leydenjar::MatrixWiring::Wcass)    ? "a Wcass's"
                                                                                                                      : "unknown";
                line("Wiring", wiring);
                std::snprintf(text, sizeof(text), "%u rows x %u columns", details.logicalRows, details.logicalColumns);
                line("QMK matrix", text);
                std::snprintf(text, sizeof(text), "%u rows x %u columns", details.controllerRows, details.controllerColumns);
                line("Controller", text);
                if (info.version >= leydenjar::c_SplitVersion)
                    line("Half", info.isLeftHalf ? "left" : "right");

                std::string calibration;
                for (size_t bin = 0; bin < info.thresholds.size(); ++bin)
                {
                    if (!calibration.empty())
                        calibration += "\n";
                    if (info.thresholds.size() > 1)
                        calibration += "bin " + std::to_string(bin) + ": ";
                    calibration += "threshold " + std::to_string(info.thresholds[bin]);
                    if (bin < info.referenceLevels.size())
                        calibration += ", at rest " + std::to_string(info.referenceLevels[bin]);
                }
                line("Calibration", calibration);
                ImGui::EndTable();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m_Keyboard.report.isVial ? "Vial" : "VIA");
            if (ImGui::BeginTable("vial", 2, ImGuiTableFlags_SizingFixedFit))
            {
                if (m_Keyboard.report.isVial)
                {
                    line("Protocol", std::to_string(m_Keyboard.report.vialProtocol));
                    if (m_Vial)
                    {
                        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(m_Vial->keyboardUid));
                        line("Keyboard UID", text);
                    }
                }
                line("VIA protocol", std::to_string(m_Keyboard.report.viaProtocol));
                if (m_Keyboard.report.isVial)
                    line("Definition", std::to_string(m_DefinitionSize) + " bytes, from the board");
                ImGui::EndTable();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("USB");
            if (ImGui::BeginTable("usb", 2, ImGuiTableFlags_SizingFixedFit))
            {
                std::snprintf(text, sizeof(text), "%04X : %04X", m_Identity.vendorId, m_Identity.productId);
                line("Vendor : product", text);
                line("Manufacturer", m_Identity.manufacturer);
                std::snprintf(text, sizeof(text), "%X.%02X", m_Identity.release >> 8, m_Identity.release & 0xFF);
                line("Release", text);
                line("Interface", "usage page FF60, usage 61");
                ImGui::EndTable();
            }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();

        ImGui::Separator();
        ImGui::TextUnformatted("Actions");

        // Enter bootloader at once, as the tool; Erase EEPROM asks first (Rico, 2026-10-17).
        const bool canAct = m_Link == Link::Open && !IsRequesting();
        if (ImGui::BeginTable("actions", 2))
        {
            // The buttons as wide as they are, what they do wrapping in the rest.
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!canAct);
            if (ImGui::Button("Enter bootloader"))
                m_Request = Leave(false);
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ColouredText(PanelColour::Muted, "to flash a new firmware: the board leaves Nazg and shows as a drive");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!canAct);
            ImGui::PushStyleColor(ImGuiCol_Text, ColourOf(PanelColour::Error));
            if (ImGui::Button("Erase EEPROM"))
                m_EraseAsked = true;
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ColouredText(PanelColour::Muted, "every layer, macro, tap dance, combo, override and setting back to the "
                                             "firmware's own -- the board restarts");
            ImGui::EndTable();
        }

        DrawEraseQuestion();
    }

    // In a dialog, naming what is lost.
    void DiagnosticsSection::DrawEraseQuestion()
    {
        const std::string title = "Erase the EEPROM of the " + m_Keyboard.Name() + "?###erase";
        if (m_EraseAsked && !ImGui::IsPopupOpen(title.c_str()))
            ImGui::OpenPopup(title.c_str());

        ImGui::SetNextWindowSize(ImVec2(430 * Unit(), 0.0f));
        if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ImGui::TextWrapped("Every layer, macro, tap dance, combo, key override and QMK setting goes back to the "
                           "firmware's own. It cannot be undone. The board restarts and comes back in the list.");
        ImGui::Spacing();

        const float width = ButtonWidth("Cancel") + ButtonWidth("Erase") + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        if (ImGui::Button("Cancel") || !m_EraseAsked)
        {
            m_EraseAsked = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ColourOf(PanelColour::Error));
        if (ImGui::Button("Erase"))
        {
            m_EraseAsked = false;
            ImGui::CloseCurrentPopup();
            if (m_Link == Link::Open && !IsRequesting())
                m_Request = Leave(true);
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}
