// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// AltRepeatKeySection - a Vial board's alt repeat keys (ui-design.md, "The Alt Repeat Key
// section", after the mockup ui-design/alt-repeat-key-section.html): the slots in the strip,
// numbered, one off struck through; the board lighting the selected entry's last key, a tag under
// it saying what Alt Repeat then sends -- the alt key too when both ways -- and a click on a key
// setting the selected one; in the panel the rule -- Last key -> Alt Repeat sends, its arrow
// turning both ways -- the modifiers that may be held too, two options in words, what the entry
// does, what is wrong with it, and Keymap's picker.
//
// Read when first shown, every slot -- Keymap draws nothing from them; written by Save, the
// changed slots only, each read back. Revert drops the changes.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgAltRepeatKey.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class AltRepeatKeySection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; `lock` is its lock state. All outlive the
        // section -- ui/NazgSection.h.
        AltRepeatKeySection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                            const std::optional<VialUnlockStatus>& lock, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Alt Repeat Key"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Repeat; }

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
        // The part of the entry the tool line edits.
        enum class Part
        {
            LastKey,
            Sends,
            Allowed,
        };

        [[nodiscard]] bool IsChanged(size_t slot) const;
        [[nodiscard]] bool IsLocked() const;

        [[nodiscard]] std::string SlotName(size_t slot) const;

        // Which sides of a key's modifiers its name says.
        enum class Sides
        {
            None,    // left and right alike: "Ctrl+Z"
            Right,   // a right one only, what is sent wrong: "Ctrl+Y", "Right Ctrl+Y"
            Both,    // the side matters: "Left Ctrl+Z"
        };

        // A key in words, its modifiers first: "Ctrl+Z", "Right Ctrl+Z".
        [[nodiscard]] std::string NameOf(const Keycode& keycode, Sides sides) const;

        // A modifier mask in words, by the host's names: "Ctrl+Shift", or with `any` "Left Ctrl or
        // Alt" -- both sides of one is that one, and with `alike` no side is said.
        [[nodiscard]] std::string MaskWords(uint8_t mask, bool alike, bool any) const;

        // The slot made usable before its first part is set: on, left and right alike.
        void Start(AltRepeatKey& altRepeatKey) const;

        // The selected key set: the last key -- a tap-hold key as its tap -- then what is sent.
        void SetKey(const Keycode& keycode);

        Task<void> Load();
        Task<void> Save(std::vector<size_t> slots);

        // The panel's parts, top to bottom.
        void DrawSlotLine();
        void DrawRule();
        void DrawConditions();
        void DrawSays();
        void DrawTools();
        void DrawPicker();

        HidTransport&                          m_Transport;
        std::string                            m_Path;
        Keyboard&                              m_Keyboard;
        const LegendSettings&                  m_Legends;
        const std::optional<VialUnlockStatus>& m_Lock;
        const bool&                            m_AdvancedTools;

        std::vector<AltRepeatKey> m_Entries;   // as edited
        std::vector<AltRepeatKey> m_Stored;    // as the board holds them, after each write

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        size_t m_Slot = 0;
        Part   m_Part = Part::LastKey;

        KeycodePickerState m_Picker;
    };
}
