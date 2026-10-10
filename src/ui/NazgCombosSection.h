// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// CombosSection - a Vial board's combos (ui-design.md, "The Combos section", after the mockup
// ui-design/combos-section.html): the slots in the strip, numbered; the board lighting the keys
// that send the selected combo's inputs, a tag between them saying what it sends, and a click on
// a key adding what it sends or taking it out; in the panel the chord -- the inputs joined by +,
// then what they send -- the combo term, what the combo does and costs, what is wrong with it,
// and Keymap's picker.
//
// Read when first shown, every slot -- Keymap draws nothing from combos, no key plays one -- with
// the combo term when the board's QMK Settings have it; written by Save, the changed slots only,
// each read back, then the term. Revert drops the changes.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgCombo.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class CombosSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`; `lock` is its lock state. All outlive the
        // section -- ui/NazgSection.h.
        CombosSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends,
                      const std::optional<VialUnlockStatus>& lock, const bool& advancedTools);

        [[nodiscard]] std::string_view Name() const override { return "Combos"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::ArrowsJoin; }

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
        // The part of the combo the picker sets: an input, 0 to 3, or the output.
        static constexpr size_t c_Output = 4;

        [[nodiscard]] bool IsLocked() const;
        [[nodiscard]] bool IsChanged(size_t combo) const;
        [[nodiscard]] bool IsTermChanged() const;

        [[nodiscard]] std::optional<Keycode>& Part();
        [[nodiscard]] std::string             NameOf(const Keycode& keycode) const;
        [[nodiscard]] std::string             SlotName(size_t combo) const;

        // A key clicked on the board: what it sends added to the inputs, or taken out.
        void ToggleInput(const Keycode& keycode);

        // After an input is set or taken: the next part to fill -- an input while fewer than two,
        // else the output while it is empty.
        void SelectNext();

        Task<void> Load();
        Task<void> Save(std::vector<size_t> combos, bool term);

        // The panel's parts, top to bottom.
        void DrawComboLine();
        void DrawChord();
        void DrawSays();
        void DrawTools();
        void DrawPicker();

        HidTransport&                          m_Transport;
        std::string                            m_Path;
        Keyboard&                              m_Keyboard;
        const LegendSettings&                  m_Legends;
        const std::optional<VialUnlockStatus>& m_Lock;
        const bool&                            m_AdvancedTools;

        std::vector<Combo> m_Combos;   // as edited
        std::vector<Combo> m_Stored;   // as the board holds them, after each write

        // The combo term, ms, for every combo: none when the board's QMK Settings lack it -- its
        // term is then the build's, which no client can read.
        std::optional<uint16_t> m_Term;
        std::optional<uint16_t> m_StoredTerm;

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        size_t m_Combo = 0;
        size_t m_Part  = 0;

        KeycodePickerState m_Picker;
    };
}
