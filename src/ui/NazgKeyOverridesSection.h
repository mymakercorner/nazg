// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyOverridesSection - a Vial board's key overrides (ui-design.md, "The Key Overrides section",
// after the mockup ui-design/key-overrides-section.html): the slots in the strip, numbered, one off
// struck through; the board lighting the key the selected override acts on, a tag under it saying
// what is held and what is sent, and a click on a key making it the override's key; in the panel
// the rule -- Held + Key -> Sends -- the layers, the modifiers not allowed and hidden, what the
// override does, what is wrong with it, the start and stop options folded, and Keymap's picker.
//
// Read when first shown, every slot -- Keymap draws nothing from key overrides; written by Save,
// the changed slots only, each read back. Revert drops the changes.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyOverride.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class KeyOverridesSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; `lock` is its lock state. All outlive the
        // section -- ui/NazgSection.h.
        KeyOverridesSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                            const std::optional<VialUnlockStatus>& lock, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Key Overrides"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Replace; }

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
        // The part of the override the tool line edits.
        enum class Part
        {
            Held,
            Key,
            Sends,
            NotHeld,
            Hidden,
        };

        [[nodiscard]] bool IsChanged(size_t slot) const;

        // Bit n for each layer the board has -- every bit past 16 layers.
        [[nodiscard]] uint16_t BoardLayers() const;

        [[nodiscard]] std::string NameOf(const Keycode& keycode) const;
        [[nodiscard]] std::string SlotName(size_t slot) const;

        // A modifier mask in words, by the host's names: "Shift", "Left Ctrl + Shift", or with
        // `any` "Ctrl or Alt".
        [[nodiscard]] std::string MaskWords(uint8_t mask, bool any = false) const;

        // Whether the firmware can send the keycode as a replacement.
        [[nodiscard]] bool CanSend(const Keycode& keycode) const;

        // The slot made usable before its first part is set: on, every layer, QMK's options.
        void Start(KeyOverride& keyOverride) const;

        // A modifier mask set; held changes hidden with it while the two are the same.
        void SetMask(Part part, uint8_t mask);

        // The key set: then the part to fill next -- the modifiers held while none are, else what
        // is sent.
        void SetKey(const Keycode& keycode);

        Task<void> Load();
        Task<void> Save(std::vector<size_t> slots);

        // The panel's parts, top to bottom.
        void DrawSlotLine();
        void DrawRule();
        void DrawConditions();
        void DrawSays();
        void DrawOptions();
        void DrawTools();
        void DrawPicker();

        HidTransport&                          m_Transport;
        std::string                            m_Path;
        Keyboard&                              m_Keyboard;
        const LegendSettings&                  m_Legends;
        const std::optional<VialUnlockStatus>& m_Lock;
        const bool&                            m_AdvancedTools;

        std::vector<KeyOverride> m_Overrides;   // as edited
        std::vector<KeyOverride> m_Stored;      // as the board holds them, after each write

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        size_t m_Slot = 0;
        Part   m_Part = Part::Key;

        KeycodePickerState m_Picker;
    };
}
