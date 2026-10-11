// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QmkSettingsSection - a Vial board's QMK Settings (ui-design.md, "The QMK Settings section", after
// the mockup ui-design/qmk-settings-section.html): the groups in the strip -- Tap-Hold, Auto Shift,
// One Shot Keys, Combos, Grave Escape, Magic, Mouse keys, Tap delays -- those the firmware has, one
// no key uses muted; the board lighting the keys the group concerns, or under the pointer what one
// line concerns -- QMK's guess of each key's hand for Chordal Hold, what a swapped key sends for
// Magic; in the panel the group's settings under QMK's names, what each does beside it, and what
// is wrong or worth knowing.
//
// Read when first shown, every setting the firmware lists, each at its own width. Each change is
// written at once and read back, as Layout's; a number when it is left or Enter pressed. Reset all
// asks first.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "model/NazgQmkSettings.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class QmkSettingsSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`. All outlive the section -- ui/NazgSection.h.
        QmkSettingsSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends);

        [[nodiscard]] std::string_view Name() const override { return "QMK Settings"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::AdjustmentsHorizontal; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;

    private:
        enum class Group : uint8_t
        {
            TapHold,
            AutoShift,
            OneShot,
            Combos,
            GraveEscape,
            Magic,
            Mouse,
            TapDelays,
        };

        // What the board lights: the keys a group or a line concerns.
        enum class Concern : uint8_t
        {
            None,
            TapHold,      // mod-taps, layer-taps, swap-hands taps
            TapToggle,    // TT layer keys
            Hands,        // every key, by QMK's guess of its hand
            AutoShift,    // the letters, numbers and symbols Auto Shift shifts
            OneShot,
            GraveEscape,
            Magic,        // the keys the swaps on change, tagged with what they send
            Mouse,
            CapsLock,     // Caps Lock, sent alone or as a tap-hold's tap
        };

        // The groups the firmware has, in the strip's order.
        [[nodiscard]] std::vector<Group> Groups() const;
        [[nodiscard]] bool               Has(uint16_t qsid) const { return m_Settings.count(qsid) != 0; }
        [[nodiscard]] uint32_t           Value(uint16_t qsid) const;
        [[nodiscard]] bool               Bit(uint16_t qsid, int bit) const { return QmkSettingBit(m_Settings, qsid, bit); }

        // A group's default concern, and whether no key of the board uses it -- muted in the strip.
        [[nodiscard]] static Concern ConcernOf(Group group);
        [[nodiscard]] bool           IsIdle(Group group) const;

        // The test a key's keycode passes to be lit for `concern`.
        [[nodiscard]] std::function<bool(const Keycode&)> TestOf(Concern concern) const;
        [[nodiscard]] bool AnyKey(const std::function<bool(const Keycode&)>& test) const;

        // The panel's parts.
        void DrawGroupLine(Group group);
        void DrawGroup(Group group);
        void DrawSays(Group group);

        // One line each: a number with its unit, a checkbox for a bit (`inverted`: QMK stores it
        // turned round), the tap-hold choice, Magic's boxes, the mouse table. `what` says what it
        // does, beside it; hovering the line lights `concern`.
        void NumberLine(uint16_t qsid, const char* name, const char* unit, uint32_t most, const char* what, Concern concern);
        void BitLine(uint16_t qsid, int bit, bool inverted, const char* name, const char* what, Concern concern,
                     bool enabled = true);
        void HoldLine();
        void MagicLines();
        void MouseLines();
        void Hovering(Concern concern, std::optional<int> magicBit = std::nullopt);

        // A number box for `qsid`: its value, or what is being typed; written once left, `name` saying what.
        void NumberBox(uint16_t qsid, uint32_t most, const std::string& name);

        Task<void> Load();
        Task<void> Write(QmkSettings writes, std::string what);
        Task<void> ResetAll();

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;

        QmkSettings m_Settings;   // as the board holds them, after each write

        bool        m_IsLoaded = false;
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        Group m_Group = Group::TapHold;

        // What the pointer was over in the panel last frame, and the one before it, for the board.
        Concern            m_Hovered = Concern::None;
        std::optional<int> m_HoveredMagicBit;
        Concern            m_HoveredNow = Concern::None;
        std::optional<int> m_HoveredMagicBitNow;

        std::map<uint16_t, uint32_t> m_Typing;   // a number box being edited, by qsid
        bool                         m_ConfirmReset = false;
    };
}
