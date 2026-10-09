// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLayoutSection.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"   // RenderTextEllipsis(), to cut a group's name

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
        constexpr float c_NameWidth     = 130.0f;   // a choice's name, before its combo
        constexpr float c_DrawingWidth  = 300.0f;   // the tooltip's drawing, at most
        constexpr float c_DrawingHeight = 90.0f;
        constexpr float c_DrawingUnit   = 20.0f;    // 1u in the drawing, at most

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
        if (m_Preview)
            board = DescribeKeyboard(m_Keyboard, *m_Preview);

        // Layer 0's legends, so each key is recognised.
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
    }

    void LayoutSection::DrawPanel()
    {
        m_NextPreview.reset();

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

        // Shown from the next frame, which draws the board before the panel.
        m_Preview = m_NextPreview;
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

        // A choice: its name, cut to its width, then the combo.
        const float  scale = ImGui::GetStyle().FontScaleDpi;
        const float  width = c_NameWidth * scale;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::AlignTextToFramePadding();
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
        const ImVec2 extent = ImGui::CalcTextSize(options.name.c_str());
        const float  top    = start.y + (ImGui::GetFrameHeight() - extent.y) / 2.0f;
        const float  end    = start.x + width - ImGui::GetStyle().ItemSpacing.x;
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(start.x, top), ImVec2(end, top + extent.y), end,
                                  options.name.c_str(), nullptr, &extent);
        if (start.x + extent.x > end)
            ImGui::SetItemTooltip("%s", options.name.c_str());

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
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

        std::vector<uint8_t> preview = m_Keyboard.layoutSelection;
        preview.resize(m_Groups.size(), 0);
        preview[group] = choice;
        m_NextPreview  = std::move(preview);
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
