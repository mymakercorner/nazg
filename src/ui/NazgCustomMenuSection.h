// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// CustomMenuSection - one of a board's own menus, from its VIA V3 definition (ui-design.md, "A
// board's own menus", after the mockup ui-design/custom-menus.html), drawn from the definition
// alone, as VIA draws it: under Board in the column.
//
// Its submenus are the strip's entries -- those whose condition holds; none with one. The panel
// holds the chosen one's lines, in two columns -- a line whose condition does not hold is hidden
// -- each a label and its control: a slider with its value, a combo, a box, a key, a colour as
// Lighting's two sliders, a button, a text. A key line selected, Keymap's picker under the lines
// sets it -- or a click on a key of the board, to what it sends.
//
// The board lights what the submenu concerns: its own key, by name -- "DKS 0 - DKS(0)" lights the
// keys holding DKS(0) -- the keys sending what its key lines choose, and on QMK's lighting
// channels the keys that change the lighting; a line under the pointer narrows them to its own.
//
// Read when shown: the chosen submenu's values and those its conditions and the strip's name, and
// again each time the section is shown -- the board's keys may change them. Written at once, as
// VIA writes them: set, then the channel stored; a slider while it moves, stored when let go, a
// slider it is tied to pushed along. A button sends its value at once and never asks (Rico,
// 2026-10-11, for now), then the submenu is read again.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "adapters/via/NazgViaCustomMenu.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class CustomMenuSection : public Section
    {
    public:
        // The definition's custom menu `menu` of the board at `path`, loaded into `keyboard`. All
        // outlive the section -- ui/NazgSection.h.
        CustomMenuSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                          const bool& advancedTools, size_t menu);

        [[nodiscard]] std::string_view Name() const override { return m_Name; }
        [[nodiscard]] Icon             ColumnIcon() const override;
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Board; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;
        void               WhileHidden() override;

    private:
        struct PendingWrite
        {
            std::vector<std::pair<const CustomControl*, std::vector<uint8_t>>> values;
            const CustomControl*                                               button = nullptr;
            bool                                                               store  = false;
        };

        [[nodiscard]] const CustomMenu&          Menu() const;
        [[nodiscard]] std::vector<size_t>        ShownSubmenus() const;
        [[nodiscard]] std::vector<const CustomControl*> ShownLines(const CustomSubmenu& submenu) const;
        [[nodiscard]] bool                       Holds(const std::vector<std::string>& conditions) const;

        // The control a value is read through: the first bound to `name` that is not a button.
        [[nodiscard]] const CustomControl* Binding(const std::string& name) const;

        // What the panel needs read: the strip's conditions' values, then the chosen submenu's --
        // its controls', what their conditions name and the sliders they are tied to.
        [[nodiscard]] std::vector<std::string> Needed() const;

        // A line's control, and the parts of the panel.
        void DrawHeader(const CustomSubmenu& submenu);
        void DrawLines(const std::vector<const CustomControl*>& lines);
        void DrawLine(const CustomControl& control);
        void DrawRange(const CustomControl& control);
        void DrawDropdown(const CustomControl& control);
        void DrawKey(const CustomControl& control);
        void DrawColour(const CustomControl& control);
        void DrawPicker(const CustomControl& control);

        // A key line's keycode, and setting it.
        [[nodiscard]] Keycode KeycodeOf(const CustomControl& control) const;
        void                  SetKeycode(const CustomControl& control, const Keycode& keycode);

        // Queues `names` for writing, as m_Values holds them: `store`, the channels written stored.
        void Change(const std::vector<std::string>& names, bool store);

        Task<void> Read(std::vector<std::string> names);
        Task<void> Write(PendingWrite write);

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;
        const bool&           m_AdvancedTools;
        size_t                m_MenuIndex = 0;
        std::string           m_Name;

        // Every range of the board's menus: a constraint may name any.
        std::vector<const CustomControl*> m_Ranges;

        CustomValues          m_Values;      // as read, or as being written
        std::set<std::string> m_Known;       // read, or the board did not answer
        std::set<std::string> m_Unanswered;  // the board refused to read them

        size_t m_Submenu = 0;   // into Menu().submenus
        bool   m_IsStale = false;

        Task<void>  m_Request;
        std::string m_ReadFailure;   // the last read's, said until a read succeeds
        std::string m_Message;       // the last write's
        bool        m_IsWarning = false;
        bool        m_IsWriting = false;

        // Waiting to be written: the values changed since the last write, whether to store, a
        // button clicked -- and the channels written since the last store.
        std::set<std::string>  m_Dirty;
        bool                   m_StoreWanted = false;
        const CustomControl*   m_Button      = nullptr;
        std::set<uint8_t>      m_Unsaved;

        // Sliders a move pushed along, and where to, for the message once let go.
        std::map<std::string, int> m_Pushed;

        // The key line the picker sets, and the line under the pointer last frame and this one.
        const CustomControl* m_Selected   = nullptr;
        const CustomControl* m_Hovered    = nullptr;
        const CustomControl* m_HoveredNow = nullptr;

        KeycodePickerState m_Picker;
    };
}
