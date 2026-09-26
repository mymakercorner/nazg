// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgWorkspace.h"

#include <algorithm>
#include <string>

#include "imgui.h"

#include "ui/NazgBoardView.h"

namespace nazg
{
    namespace
    {
        constexpr float c_ColumnWidth   = 150.0f;   // pixels, before DPI scaling
        constexpr float c_BoardMaxShare = 0.6f;     // of the height left under the strip

        void DrawColumn(const std::vector<std::unique_ptr<Section>>& sections, size_t& active)
        {
            ImGui::BeginChild("sections", ImVec2(c_ColumnWidth * ImGui::GetStyle().FontScaleDpi, 0.0f),
                              ImGuiChildFlags_Borders);
            for (size_t index = 0; index < sections.size(); ++index)
            {
                const std::string name(sections[index]->Name());
                if (ImGui::Selectable(name.c_str(), index == active))
                    active = index;
            }
            ImGui::EndChild();
        }

        void DrawStrip(Section& section)
        {
            const Strip strip = section.DescribeStrip();
            if (strip.entries.empty())
                return;

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(strip.label.c_str());

            const float minWidth = ImGui::GetFrameHeight() * 1.4f;
            for (size_t index = 0; index < strip.entries.size(); ++index)
            {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(index));

                const bool isChosen = index == strip.chosen;
                if (isChosen)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

                const std::string& entry = strip.entries[index];
                const float width = std::max(minWidth, ImGui::CalcTextSize(entry.c_str()).x +
                                                           2 * ImGui::GetStyle().FramePadding.x);
                if (ImGui::Button(entry.c_str(), ImVec2(width, 0.0f)) && !isChosen)
                    section.OnStripChosen(index);

                if (isChosen)
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
    }

    void DrawSections(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, const Keyboard& keyboard)
    {
        if (sections.empty())
            return;
        active = std::min(active, sections.size() - 1);

        // The column: only the sections this board has, and none when there is only one.
        if (sections.size() > 1)
        {
            DrawColumn(sections, active);
            ImGui::SameLine();
        }

        Section& section = *sections[active];

        ImGui::BeginGroup();
        ImGui::PushID(&section);

        DrawStrip(section);

        BoardDescription board = DescribeKeyboard(keyboard);
        section.DescribeBoard(board);
        const BoardEvents events = DrawBoard(board, ImGui::GetContentRegionAvail().y * c_BoardMaxShare);
        section.OnBoardEvents(board, events);

        // The panel: always there, taking what the board leaves.
        ImGui::BeginChild("panel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        section.DrawPanel();
        ImGui::EndChild();

        ImGui::PopID();
        ImGui::EndGroup();
    }
}
