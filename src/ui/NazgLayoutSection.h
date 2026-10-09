// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// LayoutSection - the board's layout options as a section (ui-design.md, "The Layout section",
// after the mockup ui-design/layout-section.html): the board drawn with the stored choice and
// layer 0's legends; in the panel, one line per option group -- a checkbox for a toggle, a combo
// for a choice. Hovering an option shows its keys in a tooltip and previews it on the board.
//
// A choice is written at once, as Keymap writes a key: the whole packed value, then read back,
// and the board draws what was stored. The keymap is untouched -- keys an option hides keep their
// keycodes.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class LayoutSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; a write updates its layout choice.
        // `legends` is the legends' settings. All outlive the section -- see ui/NazgSection.h.
        LayoutSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends);

        [[nodiscard]] std::string_view Name() const override { return "Layout"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Layout; }

        void DescribeBoard(BoardDescription& board) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;

    private:
        // One group's line: a checkbox, or a name and a combo.
        void DrawGroup(size_t group, uint8_t chosen);

        // What hovering `choice` of `group` does: the tooltip with its keys, the board's preview.
        void OnOptionHovered(size_t group, uint8_t choice);

        // Everything it needs across its co_awaits is passed by value.
        Task<void> Write(size_t group, uint8_t choice);

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;

        std::vector<LayoutOptionGroup> m_Groups;   // from the definition's labels, once

        // The choice the board shows instead of the stored one, while an option is hovered:
        // found while the panel draws, shown from the next frame, which draws the board first.
        std::optional<std::vector<uint8_t>> m_Preview;
        std::optional<std::vector<uint8_t>> m_NextPreview;

        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;
    };
}
