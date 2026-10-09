// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLayoutSection.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>
#include <utility>

#include "imgui.h"

#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeyShape.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // Pixels, before DPI scaling.
        constexpr float c_ColumnWidth   = 320.0f;   // a group's line; as many as the panel holds
        constexpr size_t c_NameLength   = 20;       // characters of a choice's name shown, at most (Rico)
        constexpr float c_DrawingWidth  = 390.0f;   // the tooltip's drawing, at most
        constexpr float c_DrawingHeight = 120.0f;
        constexpr float c_DrawingUnit   = 26.0f;    // 1u in the drawing, at most

        // `text` cut to `length` characters, the last of them "..." when it is longer. Characters,
        // not bytes: a UTF-8 sequence is never split.
        std::string Shortened(const std::string& text, size_t length)
        {
            size_t characters = 0;
            for (size_t at = 0; at < text.size(); ++at)
            {
                if ((static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
                    continue;
                if (++characters == length && at + 1 < text.size())
                {
                    // Is there more than this last character? Then it gives way to "...".
                    size_t next = at + 1;
                    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
                        ++next;
                    if (next < text.size())
                        return text.substr(0, at) + "\u2026";
                }
            }
            return text;
        }

        std::string OptionName(const LayoutOptionGroup& group, uint8_t choice)
        {
            if (group.options.empty())
                return choice != 0 ? "On" : "Off";
            return choice < group.options.size() ? group.options[choice] : "option " + std::to_string(choice);
        }

        // A key's outline in key units -- inset by `inset`, before it is turned: InsetContour()
        // needs the key's edges square -- then turned as the board turns it.
        std::vector<ContourPoint> TurnedContour(const DefinitionKey& key, float inset)
        {
            std::vector<ContourPoint> contour = inset != 0.0f ? InsetContour(KeyContour(key), inset) : KeyContour(key);
            if (key.rotation == 0.0f)
                return contour;

            // Clockwise on screen, since y grows downward -- as the board view turns it.
            const float radians = key.rotation * 3.14159265358979f / 180.0f;
            const float cosine  = std::cos(radians);
            const float sine    = std::sin(radians);
            for (ContourPoint& point : contour)
            {
                const float x = point.x - key.rotationX;
                const float y = point.y - key.rotationY;
                point         = { key.rotationX + x * cosine - y * sine, key.rotationY + x * sine + y * cosine };
            }
            return contour;
        }

        // The tooltip of an option: the group's keys in that option, every option of the group
        // at one scale so they compare, and its name. ImGui keeps a tooltip inside the window.
        void DrawOptionTooltip(const KeyboardDefinition& definition, size_t group, const LayoutOptionGroup& options,
                               uint8_t choice)
        {
            std::vector<DefinitionKey> shown;
            float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
            for (size_t each = 0; each < options.Count(); ++each)
                for (const DefinitionKey& key : OptionKeys(definition, group, static_cast<uint8_t>(each)))
                {
                    for (const ContourPoint& point : TurnedContour(key, 0.0f))
                    {
                        x0 = std::min(x0, point.x);
                        y0 = std::min(y0, point.y);
                        x1 = std::max(x1, point.x);
                        y1 = std::max(y1, point.y);
                    }
                    if (each == choice)
                        shown.push_back(key);
                }

            ImGui::BeginTooltip();
            if (x1 > x0 && y1 > y0)
            {
                const float scale = ImGui::GetStyle().FontScaleDpi;
                const float unit  = scale * std::min({ c_DrawingUnit, c_DrawingWidth / (x1 - x0), c_DrawingHeight / (y1 - y0) });
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2((x1 - x0) * unit, (y1 - y0) * unit));

                ImDrawList* const          list    = ImGui::GetWindowDrawList();
                const BoardColours::Keycap colours = BoardColours::Fill(KeyFill::Alpha, 0.0f);
                const float                gap     = std::max(1.0f, unit * 0.06f);
                for (const DefinitionKey& key : shown)
                {
                    // Inset by the gap between keys, as the board draws them.
                    for (const ContourPoint& point : TurnedContour(key, gap / unit))
                        list->PathLineTo(ImVec2(origin.x + (point.x - x0) * unit, origin.y + (point.y - y0) * unit));
                    ImVector<ImVec2> path = list->_Path;
                    list->PathFillConcave(colours.face);
                    list->AddPolyline(path.Data, path.Size, BoardColours::Outline(), ImDrawFlags_Closed, std::max(1.0f, scale));
                }
            }
            ImGui::TextUnformatted(OptionName(options, choice).c_str());
            ImGui::EndTooltip();
        }
    }

    LayoutSection::LayoutSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                 const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends),
          m_Groups(ParseLayoutGroups(keyboard.definition.layoutLabels))
    {
    }

    bool LayoutSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    void LayoutSection::DescribeBoard(BoardDescription& board)
    {
        // Layer 0's legends, so each key is recognised.
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
    }

    void LayoutSection::DrawPanel()
    {
        const float scale   = ImGui::GetStyle().FontScaleDpi;
        const int   columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (c_ColumnWidth * scale)));

        ImGui::BeginDisabled(IsBusy());
        if (ImGui::BeginTable("options", columns))
        {
            for (size_t group = 0; group < m_Groups.size(); ++group)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(group));
                DrawGroup(group, group < m_Keyboard.layoutSelection.size() ? m_Keyboard.layoutSelection[group] : 0);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Muted, "%s", m_Message.c_str());
        else
            ColouredText(PanelColour::Muted, "Each choice is written to the board at once. The keymap is untouched.");
    }

    void LayoutSection::DrawGroup(size_t group, uint8_t chosen)
    {
        const LayoutOptionGroup& options = m_Groups[group];

        // A toggle: its checkbox; hovered, what a click gives.
        if (options.options.empty())
        {
            bool isOn = chosen != 0;
            if (ImGui::Checkbox(options.name.c_str(), &isOn) && !IsBusy())
                m_Request = Write(group, isOn ? 1 : 0);
            if (ImGui::IsItemHovered())
                OnOptionHovered(group, chosen != 0 ? 0 : 1);
            return;
        }

        // A choice: its name, then the combo right after it, as a checkbox's label follows its box
        // (Rico, 2026-10-09) -- the name whole up to 20 characters, then cut with "...", whatever
        // room the line has; the combo takes the rest.
        const std::string shown = Shortened(options.name, c_NameLength);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(shown.c_str());
        if (shown != options.name)
            ImGui::SetItemTooltip("%s", options.name.c_str());

        // As wide as its longest option -- its text, the frame's padding and the arrow's square --
        // but no wider than what the line has left.
        float longest = 0.0f;
        for (const std::string& option : options.options)
            longest = std::max(longest, ImGui::CalcTextSize(option.c_str()).x);
        ImGui::SameLine();
        const float wanted = longest + 2.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::min(wanted, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##choice", OptionName(options, chosen).c_str()))
        {
            for (uint8_t choice = 0; choice < options.options.size(); ++choice)
            {
                if (ImGui::Selectable(options.options[choice].c_str(), choice == chosen) && choice != chosen && !IsBusy())
                    m_Request = Write(group, choice);
                if (ImGui::IsItemHovered())
                    OnOptionHovered(group, choice);
            }
            ImGui::EndCombo();
        }
    }

    void LayoutSection::OnOptionHovered(size_t group, uint8_t choice)
    {
        DrawOptionTooltip(m_Keyboard.definition, group, m_Groups[group], choice);
    }

    Task<void> LayoutSection::Write(size_t group, uint8_t choice)
    {
        m_Message.clear();
        m_IsWarning = false;

        std::vector<uint8_t> wanted = m_Keyboard.layoutSelection;
        wanted.resize(m_Groups.size(), 0);
        wanted[group]         = choice;
        const uint32_t value  = EncodeLayoutOptions(wanted, m_Groups);
        const std::string what = m_Groups[group].name + ": " + OptionName(m_Groups[group], choice);

        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);

            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            co_await via.SetKeyboardValue(ViaKeyboardValue::LayoutOptions, value);
            const uint32_t stored = co_await via.GetKeyboardValue(ViaKeyboardValue::LayoutOptions);

            // The board draws what was stored, whatever was asked.
            m_Keyboard.layoutOptions   = stored;
            m_Keyboard.layoutSelection = DecodeLayoutOptions(stored, m_Groups);

            if (stored == value)
            {
                m_Message = "stored " + what;
            }
            else
            {
                m_Message   = "asked for " + what + ", but the board kept another layout -- its firmware may store "
                              "fewer bits of layout options than this definition needs";
                m_IsWarning = true;
            }
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }

        // Outside the catch: closing an id that never opened does nothing.
        m_Transport.Close(device);
    }
}
