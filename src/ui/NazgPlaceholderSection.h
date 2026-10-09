// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// PlaceholderSection - a section the board has that Nazg does not build yet: its row in the
// column, the board as its definition draws it, and a panel that says so -- a custom menu's
// listing the sections its definition declares. So the column shows every section a board
// should have before each is built (ui/NazgSectionPlan.h).
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <utility>

#include "ui/NazgSection.h"
#include "ui/NazgSectionPlan.h"

namespace nazg
{
    class PlaceholderSection : public Section
    {
    public:
        explicit PlaceholderSection(PlannedSection planned) : m_Planned(std::move(planned)) {}

        [[nodiscard]] std::string_view Name() const override { return m_Planned.name; }
        [[nodiscard]] Icon             ColumnIcon() const override { return m_Planned.icon; }
        [[nodiscard]] SectionGroup     Group() const override { return m_Planned.group; }

        void DescribeBoard(BoardDescription& /*board*/) override {}
        void DrawPanel() override;

    private:
        PlannedSection m_Planned;
    };
}
