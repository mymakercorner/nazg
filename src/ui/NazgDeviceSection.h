// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DeviceSection - what is done to the board as a whole, on every VIA and Vial board (ui-design.md,
// "The Device section", after the mockup ui-design/board-actions.html): enter its bootloader, put
// back the keymap its firmware was built with, empty its macros. In the column's Tools group.
//
// The firmware decides what can be done (via-vial-commands.md). Only a secure Vial build enters its
// bootloader on command, and only unlocked; elsewhere the section says how instead -- the key
// holding QK_BOOT, lit on the board, or the reset button. The two resets work everywhere. Each of
// the three asks first. After a reset Nazg loads the board again (TakeBoardChanged()); after the
// bootloader the board is gone, and the list says where (BoardLeft()). A Leyden Jar board keeps
// its own in Diagnostics, beside this (Rico, 2026-10-18).
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class DeviceSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; `lock` its Vial lock, none without one. All
        // outlive the section -- ui/NazgSection.h.
        DeviceSection(HidTransport& transport, std::string path, const Keyboard& keyboard, const LegendSettings& legends,
                      const std::optional<vial::UnlockStatus>& lock);

        [[nodiscard]] std::string_view Name() const override { return "Device"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Cpu; }
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Tools; }

        void DescribeBoard(BoardDescription& board) override;
        void DrawPanel() override;

        [[nodiscard]] bool        IsBusy() const override;
        [[nodiscard]] std::string BoardLeft() const override { return m_Left; }
        [[nodiscard]] bool        TakeBoardChanged() override;

    private:
        enum class Action
        {
            None,
            Bootloader,
            ResetKeymap,
            EmptyMacros,
        };

        // The firmware enters its bootloader on command: a secure Vial build. Locked, it must be
        // unlocked first.
        [[nodiscard]] bool CanJump() const;
        [[nodiscard]] bool IsLocked() const;

        // Where QK_BOOT sits, for a board that cannot jump: the first layer holding it, -1 for none.
        [[nodiscard]] int BootLayer() const;

        void DrawBootloaderLine();
        void DrawQuestion();

        Task<void> Run(Action action);

        HidTransport&                            m_Transport;
        std::string                              m_Path;
        const Keyboard&                          m_Keyboard;
        const LegendSettings&                    m_Legends;
        const std::optional<vial::UnlockStatus>& m_Lock;

        Action      m_Asked = Action::None;   // the question shown
        Task<void>  m_Request;
        std::string m_Message;                // what failed
        bool        m_Changed = false;        // a reset done: the board is loaded again
        std::string m_Left;                   // the bootloader entered: what the list says
    };
}
