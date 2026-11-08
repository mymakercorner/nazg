// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SwitchesSection - a Hall-effect Keychron's switch settings (ui-design.md, "The Switches section",
// after the mockup ui-design/switches-section.html): each key's Actuation Distance, Rapid Trigger with
// its (Press) Trigger Distance and (Release) Reset Distance, and the board's three profiles. The wire
// is keychron-commands.md, 0xA9.
//
// Rico's choices (2026-11-08): the profiles in the strip, picking one makes the board use it -- as
// Fn + P + Z/X/C does, and the section follows the board when the keyboard switches it; the
// Actuation Distance on every key, RT on those in Rapid Trigger; the selection edited, or every key
// when nothing is selected; selecting by a click, or by pressing the key on the board, Select All and
// Clear; the Launcher's own words. Every change written at once -- a slider once let go -- then the
// profile saved and read back. Keys are told apart by what they do, never by whether their own word
// is 0 (keychron-commands.md, "Rapid Trigger is written per key").
//
// Testing is asked for: Test keys follows the key pressed, its travel read live (0xA9 0x30), and its
// raw values on request; while testing, a press selects nothing.
//
// The board stays open while the section shows: its switches are read for the presses (VIA's matrix
// state, which every Keychron answers), the profile in use every second. One request at a time,
// started from the frame: a write waits for the reading in flight.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"
#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class SwitchesSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`. All outlive the section -- ui/NazgSection.h.
        SwitchesSection(HidTransport& transport, std::string path, const Keyboard& keyboard, const LegendSettings& legends);

        SwitchesSection(const SwitchesSection&)            = delete;
        SwitchesSection& operator=(const SwitchesSection&) = delete;

        [[nodiscard]] std::string_view Name() const override { return "Switches"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::ArrowBarToDown; }
        [[nodiscard]] SectionGroup     Group() const override { return SectionGroup::Board; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;
        [[nodiscard]] bool IsPolling() const override;
        void               WhileHidden() override;

    private:
        enum class Link
        {
            Closed,
            Open,
            Failed,   // m_Message says why; tried again when shown
        };

        // A key the board draws, with a switch in the matrix.
        struct Key
        {
            keychron::KeyPosition position;
            std::string           name;   // what it prints, or its position
        };

        // A change waiting to be written: planned when asked, against the profile as Nazg shows it --
        // the board's, with the changes before it applied.
        struct PendingChange
        {
            uint8_t                            profile = 0;
            std::vector<keychron::TravelWrite> writes;
        };

        void FindKeys(const BoardDescription& board);
        [[nodiscard]] std::optional<size_t> KeyAt(keychron::KeyPosition position) const;
        [[nodiscard]] std::vector<size_t>   Targets() const;
        [[nodiscard]] const keychron::Profile* Shown() const;
        [[nodiscard]] keychron::KeyConfig   ConfigOf(size_t key) const;
        [[nodiscard]] std::string           ProfileName(size_t profile) const;

        void Step();
        void Ask(keychron::TravelChange change);
        void PressSeen(size_t key);

        void DrawHeader();
        void DrawEditor();
        void DrawDistance(const char* label, const char* id, const char* tip, std::optional<uint8_t> value,
                          uint8_t maximum, int field);
        void DrawTest();
        void DrawTravelBar(const keychron::RealtimeTravel& travel, const keychron::KeyConfig& config);
        void DrawResetQuestion();

        Task<void> Open();
        Task<void> Poll(bool readProfileInUse);
        Task<void> Write(PendingChange change);
        Task<void> Select(uint8_t profile);
        Task<void> Reset(uint8_t profile);
        void       Fail(std::string message);

        HidTransport&         m_Transport;
        std::string           m_Path;
        const Keyboard&       m_Keyboard;
        const LegendSettings& m_Legends;

        Link                            m_Link = Link::Closed;
        std::optional<HidDeviceChannel> m_Channel;
        Task<void>                      m_Request;
        bool                            m_RequestIsPoll   = false;
        bool                            m_HiddenThisFrame = false;
        double                          m_LastPoll        = 0.0;
        double                          m_LastInUseRead   = 0.0;

        // What the board holds, read when the section is shown.
        keychron::ProfileGeometry      m_Geometry;
        std::vector<keychron::Profile> m_Profiles;
        size_t                         m_ShownProfile = 0;   // the one in use, since picking uses it
        uint8_t                        m_NameLayer    = 0;   // the board's default layer, for the keys' names

        // Full travel in 0.1 mm: the curve's last x, until a key read at the bottom measures it.
        uint8_t m_FullTravel     = 0;
        bool    m_TravelMeasured = false;
        int     m_ClampReadings  = 0;

        std::vector<Key>                   m_Keys;
        std::vector<std::optional<size_t>> m_BoardKeys;   // by board key: which of m_Keys it is
        std::vector<uint8_t>  m_KeysSelection;   // the layout the keys were found for
        uint8_t               m_KeysNameLayer = 0xFF;
        std::vector<bool>     m_Selected;        // by key
        std::vector<bool>     m_WasPressed;      // by key, at the last matrix reading
        bool                  m_MatrixRefused = false;

        // The key test.
        bool                                  m_Testing = false;
        bool                                  m_Raw     = false;
        std::optional<size_t>                 m_Followed;
        std::optional<keychron::RealtimeTravel>  m_Travel;
        std::optional<keychron::CalibratedValue> m_Calibration;   // of the followed key
        std::optional<size_t>                 m_CalibrationOf;

        // Writes waiting, one at a time.
        std::vector<PendingChange>   m_PendingChanges;
        std::optional<uint8_t>       m_PendingSelect;
        std::optional<uint8_t>       m_PendingReset;
        bool                         m_ResetAsked = false;

        // A slider being dragged: its field and value, shown, not written until let go.
        std::optional<int>     m_DraggedField;
        int                    m_DraggedValue = 0;

        std::string m_Message;
        bool        m_IsWarning = false;
    };
}
