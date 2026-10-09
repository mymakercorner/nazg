// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgPlaceholderSection.h"

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    void PlaceholderSection::DrawPanel()
    {
        ImGui::TextUnformatted(m_Planned.name.empty() ? "(unnamed menu)" : m_Planned.name.c_str());
        ColouredText(PanelColour::Muted, "Not built yet.");

        if (m_Planned.kind != SectionKind::CustomMenu)
            return;

        ImGui::Spacing();
        ImGui::TextWrapped("One of this board's own menus, from its definition. Its sections:");
        for (const std::string& section : m_Planned.subsections)
            ImGui::BulletText("%s", section.empty() ? "(unnamed)" : section.c_str());
        if (m_Planned.subsections.empty())
            ColouredText(PanelColour::Muted, "None.");
    }
}
