// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgPlaceholderSection.h"

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    void PlaceholderSection::DrawPanel()
    {
        ImGui::TextUnformatted(m_Planned.name.c_str());
        ColouredText(PanelColour::Muted, "Not built yet.");
    }
}
