// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SwitchesSection - a Hall-effect Keychron's switch settings (ui-design.md, "The Switches section",
// after the mockups ui-design/switches-section.html and switches-advanced-keys.html): each key's mode
// -- Regular, Rapid Trigger, or an advanced one -- with its Actuation Distance, (Press) Trigger
// Distance and (Release) Reset Distance, and the board's three profiles. The wire is
// keychron-commands.md, 0xA9.
//
// The advanced modes are modes of the key (Rico, 2026-11-23), the firmware giving it one: One Key
// Multiple Commands (DKS), edited by ui/NazgDksEditor.h; Long-Press Switch, the firmware's toggle;
// Gamepad Analog, its controls assigned on ui/NazgGamepadView.h, with the gamepad's settings, the whole
// board's. Test keys shows the gamepad too, as the computer receives it. With nothing selected, "every key" leaves the advanced ones out: a
// click on Regular would otherwise wipe a whole gamepad. A mode Nazg does not know is said, and left.
//
// Rico's choices (2026-11-08): the profiles in the strip, picking one makes the board use it -- as
// Fn + P + Z/X/C does, and the section follows the board when the keyboard switches it; the
// Actuation Distance on every key, RT on those in Rapid Trigger; the selection edited, or every key
// when nothing is selected; selecting by a click, or by pressing the key on the board, Select All and
// Clear; the Launcher's own words. Every change written at once -- a slider once let go -- then the
// profile saved and read back. Keys are told apart by what they do, never by whether their own word
// is 0 (keychron-commands.md, "Rapid Trigger is written per key").
//
// SOCD, beside the modes as in the Launcher (Rico, 2026-11-23): the profile's pairs on a line -- Add pair,
// or two keys clicked or pressed -- and the chosen one's four choices under it (ui/NazgSocdEditor.h). A
// pair is apart from its keys' modes: from SOCD a mode button goes back to the keys, and changes nothing.
// Its test shows what the computer receives, no command answering what is left after SOCD.
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

#include "adapters/keychron/NazgKeychronAdvanced.h"
#include "adapters/keychron/NazgKeychronProtocol.h"
#include "adapters/keychron/NazgKeychronSocd.h"
#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgDksEditor.h"
#include "ui/NazgGamepadView.h"
#include "ui/NazgKeycodePicker.h"
#include "ui/NazgSection.h"
#include "ui/NazgSocdEditor.h"

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
        void                OnStripAction() override;   // Reset profile...

        void DescribeBoard(BoardDescription& board) override;
        void OnBoardEvents(const BoardDescription& board, const BoardEvents& events) override;

        void DrawPanel() override;

        [[nodiscard]] bool WantsGameController() const override;
        void               OnGameController(const GameControllerReading& reading) override;

        [[nodiscard]] bool WantsComputerKeys() const override;   // a pair tested: the keys drive nothing else
        void               OnHostKey(uint32_t usage, bool down) override;

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
            std::string           name;       // what it prints, or its position
            via::DefinitionKey    geometry;   // for what the keymap has there
        };

        // A change waiting to be written: planned when asked, against the profile as Nazg shows it --
        // the board's, with the changes before it applied.
        struct PendingChange
        {
            uint8_t                              profile = 0;
            std::vector<keychron::AdvancedWrite> advanced;   // before the travels
            std::vector<keychron::TravelWrite>   writes;
            std::vector<keychron::SocdWrite>     socd;
        };

        // What a key does, its mode in the firmware's sense: one of these at a time.
        enum class Mode : uint8_t
        {
            Regular,
            RapidTrigger,
            Dks,         // the Launcher's One Key Multiple Commands
            LongPress,   // the Launcher's Long-Press Switch, the firmware's toggle
            Gamepad,
            Unknown,     // an advanced mode in no published source
        };

        void FindKeys(const BoardDescription& board);
        [[nodiscard]] std::optional<size_t> KeyAt(keychron::KeyPosition position) const;
        [[nodiscard]] std::vector<size_t>   Targets() const;
        [[nodiscard]] const keychron::Profile* Shown() const;
        [[nodiscard]] keychron::KeyConfig   ConfigOf(size_t key) const;
        [[nodiscard]] std::string           ProfileName(size_t profile) const;

        void Step();
        [[nodiscard]] static Mode ModeOf(const keychron::KeyConfig& config);
        [[nodiscard]] static bool UsesActuation(const keychron::KeyConfig& config);
        [[nodiscard]] static bool DoSame(const keychron::KeyConfig& a, const keychron::KeyConfig& b);
        [[nodiscard]] std::string Briefly(const keychron::KeyConfig& config) const;
        [[nodiscard]] std::string Says(const keychron::KeyConfig& config) const;
        [[nodiscard]] std::string MarkOf(const keychron::KeyConfig& config) const;
        [[nodiscard]] std::string KeycodeName(uint16_t keycode) const;
        [[nodiscard]] std::string DksPointSays(const keychron::DksSlot& slot, size_t point) const;

        void Ask(keychron::TravelChange change);
        void AskMode(Mode mode);
        void AskDks(size_t key, uint8_t slot, const keychron::DksSlot& settings);
        [[nodiscard]] std::optional<size_t> OneSelected() const;
        void PressSeen(size_t key);

        // SOCD: the pair shown -- the one chosen, else the first -- by its slot, and its keys as they are
        // named; what a key clicked or pressed does while the pairs show; going there and back.
        [[nodiscard]] std::optional<uint8_t> ShownPair() const;
        [[nodiscard]] SocdPairView           PairView(const keychron::SocdSlot& pair) const;
        [[nodiscard]] std::string            NameAt(keychron::KeyPosition position) const;
        [[nodiscard]] std::string            PairWarning(const keychron::SocdSlot& pair) const;
        void AskPair(const keychron::SocdWrite& write);
        void PairKeyChosen(size_t key);
        void ForgetPair();
        void ShowPairs();
        void ShowKeys();
        void DrawPairsLine();
        void DrawPairs();
        void DrawPairTest();
        [[nodiscard]] bool IsTestingPair() const;
        void PairRead(const keychron::SocdSlot& pair, const keychron::RealtimeTravel& first,
                      const keychron::RealtimeTravel& second);
        [[nodiscard]] std::optional<std::string_view> NameSent(keychron::KeyPosition position) const;

        void DrawHead();
        void DrawEditor();
        void DrawDistances(const std::vector<size_t>& targets, Mode mode);
        void DrawDifferences(const std::vector<size_t>& targets);
        void DrawDks(size_t key);
        void DrawGamepad(const std::vector<size_t>& targets);
        void DrawGamepadSettings();
        void ChooseControl(uint8_t control);
        [[nodiscard]] bool HasGamepadKeys() const;
        void DrawDksPicker(const keychron::DksSlot& settings);
        void PickForCommand(uint16_t keycode);
        void PickFromBoard(size_t key);
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
        Task<void> WriteGamepadMode(uint8_t mask, uint8_t value);
        void       Fail(std::string message);
        void       Say(std::string message, bool isWarning);

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

        // One Key Multiple Commands: the editor's drags, the command whose key is being picked -- on the
        // board, or from the picker in a popup -- and the followed key's slot played from its travel.
        DksEditorState            m_DksState;
        std::optional<size_t>     m_PickCommand;
        bool                      m_OpenPicker = false;
        KeycodePickerState        m_Picker;
        std::vector<CatalogueTab> m_BasicTab;
        std::string               m_BasicTabFor;
        keychron::DksPlayer       m_Player;

        // Gamepad Analog: the controls shown before one is chosen -- the mode needs one -- for the selection
        // they were opened on; the mode byte, none where the board has none, and whether its firmware can
        // keep from being seen as a controller; the control under the pointer, whose keys light.
        bool                          m_GamepadOpen = false;
        std::vector<bool>             m_GamepadOpenFor;
        std::optional<uint8_t>        m_GamepadMode;
        bool                          m_HasRecognition = false;
        std::optional<uint8_t>        m_HoveredControl;
        struct ModeChange
        {
            uint8_t mask  = 0;
            uint8_t value = 0;
        };
        std::optional<ModeChange>     m_PendingMode;

        // What the computer receives, while testing a profile with gamepad keys.
        GameControllerReading               m_Controller;
        std::optional<keychron::GamepadLevels> m_Levels;
        GamepadTrail                        m_Trail;

        // SOCD: whether the pairs show instead of the keys, the slot chosen, and a pair being added with
        // the first of its keys; the key under the pointer, whose partner lights.
        bool                   m_ShowPairs  = false;
        std::optional<uint8_t> m_Pair;
        bool                   m_AddingPair = false;
        std::optional<size_t>  m_PairFirst;
        std::optional<size_t>  m_HoveredKey;

        // The pair tested: the keys the computer holds down, what is sent -- as it receives it, else as the
        // firmware plays it -- and the last seconds.
        bool                                    m_HostDown[2] = {};
        std::optional<keychron::RealtimeTravel> m_PairTravel[2];
        keychron::SocdPlayer                    m_PairPlayer;
        keychron::SocdPlayer::Sent              m_PairSent;
        SocdTrail                               m_PairTrail;

        // Writes waiting, one at a time.
        std::vector<PendingChange>   m_PendingChanges;
        std::optional<uint8_t>       m_PendingSelect;
        std::optional<uint8_t>       m_PendingReset;
        bool                         m_ResetAsked = false;

        // A slider being dragged: its field and value, shown, not written until let go.
        std::optional<int>     m_DraggedField;
        int                    m_DraggedValue = 0;

        // What was last written, at the strip's end: a few seconds, or until the next when a warning.
        std::string m_Message;
        bool        m_IsWarning = false;
        double      m_MessageAt = 0.0;
    };
}
