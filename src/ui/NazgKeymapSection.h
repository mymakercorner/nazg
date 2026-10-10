// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeymapSection - the keymap as a section: layers in the strip, keycaps on the board, the
// keycode picker in the panel. The first implementation of the section contract
// (ui/NazgSection.h).
//
// Click a key, it is outlined; click a keycode, it is written -- encoded for the board's
// keycode version, set, and read back, so the board shows what the firmware really stored
// (adapters/via/NazgViaKeymap.h). The panel is the first-draft picker for now. Hovering a
// layer in the strip shows it, as choosing it would, until the mouse leaves: a peek.
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
        // The board at `path`, loaded into `keyboard`; writes update it. `legends` is the
        // legends' settings, read every frame so a change shows at once, as the two settings
        // are -- the Advanced tools one has the picker list every lighting key. All outlive the
        // section -- see ui/NazgSection.h.
        KeymapSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                      const bool& moveToNextKey, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Keymap"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Keyboard; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;
        void                OnStripHovered(std::optional<size_t> entry) override;

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

        // Everything it needs across its co_awaits is passed by value, the version too,
        // rather than read back from the keyboard afterwards.
        Task<void> WriteKey(uint8_t layer, Cell cell, Keycode keycode, QmkKeycodeVersion version);

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;
        const bool&           m_MoveToNextKey;   // the setting, read at every pick
        const bool&           m_AdvancedTools;

        // The board's cells in its order -- top to bottom, then left to right -- for "next key",
        // from the keys drawn this frame.
        std::vector<Cell> m_Order;

        // The key just written, flashing a moment so a write is seen where it landed.
        std::optional<Cell> m_Flash;
        double              m_FlashUntil = 0.0;

        // What the picker offers this board, built again when the legends' settings change: its
        // group titles are words in the host's modifier names.

        // The key line over the tabs (ui-design.md, "The key line"): where the key is, its keycode
        // in QMK's words -- editable, the Any entry -- and the composer, whose choices open in
        // popups and are written at once.
        void DrawKeyLine(const Keycode& current);
        void DrawComposer(const Keycode& current);

        // Writes `keycode` to the selected key, unless a write is still running. `advance`: a pick
        // or an expression entered, after which the next key is selected if the setting asks --
        // never a composer button, which shapes the key in place.
        void Write(const Keycode& keycode, bool advance = false);

        uint8_t                m_Layer = 0;
        std::optional<uint8_t> m_Peek;   // a layer hovered in the strip, shown meanwhile
        std::optional<Cell> m_Selected;
        KeycodePickerState  m_Picker;

        // What a click on the tile under the mouse would write, shown on the selected key meanwhile.
        std::optional<Keycode> m_Preview;

        // The expression box: what is typed, and what it was last filled from -- the selected key's
        // keycode, refilled when that changes and the box is not being typed in.
        char        m_Expression[128] = {};
        std::string m_ExpressionFor;
        std::string m_ExpressionError;   // why Enter wrote nothing, outlining the box
        bool        m_ExpressionEditing = false;   // the box had the keyboard last frame

        Task<void>  m_Write;
        bool        m_IsWriting = false;
        std::string m_Message;   // what the last write did
        bool        m_IsWarning = false;
    };
}
