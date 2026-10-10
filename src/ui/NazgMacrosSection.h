// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// MacrosSection - the board's macros (ui-design.md, "The Macros section", after the mockup
// ui-design/macros-section.html): the slots in the strip, the board marking the keys that play a
// macro, and in the panel the selected macro as a chain, left to right, over Keymap's picker.
//
// The chain is edited in place: a click selects a step, a click between two steps places a + where
// the next one goes, a key picked in the picker replaces the selected key or is added at the +.
// Text and waits are typed in their step. A long chain wraps, each row's end linked to the next
// row's start. Steps are text (typed with the host layout, ui/NazgMacroSteps.h), keys -- sent with
// modifiers or not -- presses and releases, paired for reading, and waits.
//
// Read from the board when the section is first shown, only as far as the macros go; written by
// Save, all of them, inside the firmware's guard, then read back. Revert drops the changes. A
// locked Vial board shows its macros and writes none.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "adapters/via/NazgViaMacro.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "model/NazgMacro.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgMacroSteps.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class MacrosSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`. `legends` is the legends' settings, whose
        // host layout text is typed with -- the panel may change it, as Settings does, and sets
        // `hostLayoutChosen` then; until it is set, text asks which layout the computer uses.
        // `lock` is a Vial board's lock, unset on VIA. All outlive the section -- ui/NazgSection.h.
        MacrosSection(HidTransport& transport, std::string path, Keyboard& keyboard, LegendSettings& legends,
                      bool& hostLayoutChosen, const std::optional<VialUnlockStatus>& lock, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Macros"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::PlayerPlay; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;

        [[nodiscard]] bool        HasUnsavedChanges() const override;
        [[nodiscard]] std::string UnsavedSummary() const override;
        void                      SaveChanges() override;
        void                      DiscardChanges() override;

    private:
        [[nodiscard]] MacroContext Context() const;
        [[nodiscard]] bool         IsLocked() const;
        [[nodiscard]] bool         IsChanged(size_t macro) const;

        // The board's macros read again through the host layout: after a load, a write, or a change
        // of host layout -- the macros not edited follow it.
        void ReadSteps();

        Task<void> Load();
        Task<void> Save(std::vector<uint8_t> bytes);

        // The panel's parts, top to bottom.
        void DrawMacroLine();
        void DrawHostQuestion();
        void DrawChain();
        void DrawProblems();
        void DrawTools();
        void DrawPicker();

        // Editing.
        void Select(std::optional<size_t> step);
        void Insert(std::vector<MacroStep> steps, size_t caretAfter);
        void Remove(size_t step);
        void Move(size_t step, int by);
        void Picked(const Keycode& keycode);


        HidTransport&                          m_Transport;
        std::string                            m_Path;
        Keyboard&                              m_Keyboard;
        LegendSettings&                        m_Legends;
        bool&                                  m_HostLayoutChosen;
        const std::optional<VialUnlockStatus>& m_Lock;
        const bool&                            m_AdvancedTools;

        // What the board holds, as read: its bytes, and each macro's actions.
        MacroFormat               m_Format     = MacroFormat::Via;
        uint16_t                  m_BufferSize = 0;
        std::vector<uint8_t>      m_Stored;
        std::vector<MacroActions> m_StoredActions;
        std::string               m_StepsFor;   // the host layout m_Saved was read with

        std::vector<Macro> m_Saved;    // the stored macros as steps
        std::vector<Macro> m_Macros;   // as edited

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        // The macro shown, and in it the step selected or the + -- where the next step goes.
        size_t                m_Macro = 0;
        std::optional<size_t> m_Selected;
        std::optional<size_t> m_Caret;

        // The selected text or wait as it is typed, and whether its field should take the keyboard.
        char m_Edit[512]  = {};
        bool m_FocusEdit  = false;
        std::optional<size_t> m_EditFor;

        KeycodePickerState m_Picker;
    };
}
