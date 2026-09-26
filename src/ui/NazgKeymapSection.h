// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeymapSection - the keymap as a section: layers in the strip, keycaps on the board, the
// keycode picker in the panel. The first implementation of the section contract
// (ui/NazgSection.h).
//
// Click a key, it is outlined; click a keycode, it is written -- encoded for the board's
// keycode version, set, and read back, so the board shows what the firmware really stored
// (adapters/via/NazgViaKeymap.h). The panel is the first-draft picker for now.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class KeymapSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; writes update it. `hostLayoutId` is
        // the setting, read every frame so a change shows at once. All three outlive the
        // section -- see ui/NazgSection.h.
        KeymapSection(HidTransport& transport, std::string path, Keyboard& keyboard, const std::string& hostLayoutId);

        [[nodiscard]] std::string_view Name() const override { return "Keymap"; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;

    private:
        // A matrix cell -- the address a write needs.
        struct Cell
        {
            uint8_t row    = 0;
            uint8_t column = 0;
        };

        [[nodiscard]] const HostLayout& CurrentHostLayout() const;

        // Everything it needs across its co_awaits is passed by value, the version too,
        // rather than read back from the keyboard afterwards.
        Task<void> WriteKey(uint8_t layer, Cell cell, Keycode keycode, QmkKeycodeVersion version);

        HidTransport&      m_Transport;
        std::string        m_Path;
        Keyboard&          m_Keyboard;
        const std::string& m_HostLayoutId;

        uint8_t             m_Layer = 0;
        std::optional<Cell> m_Selected;
        KeycodePickerState  m_Picker;

        Task<void>  m_Write;
        bool        m_IsWriting = false;
        std::string m_Message;   // what the last write did
        bool        m_IsWarning = false;
    };
}
