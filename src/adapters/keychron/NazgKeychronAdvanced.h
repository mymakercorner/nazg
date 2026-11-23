// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A Hall-effect Keychron's advanced keys (docs/research_material/keychron-commands.md, "Advanced
// modes"): what a key does instead of a plain press -- One Key Multiple Commands, the firmware's
// DKS; Long-Press Switch, its toggle; Gamepad Analog. No board here, only the profile read: what a
// change writes, what it makes of the profile, and how the settings read.
//
// A key has ONE mode: an advanced one replaces Regular or Rapid Trigger, whose fields the firmware
// keeps underneath (update_key_config()).

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"
#include "model/NazgGameController.h"

namespace nazg::keychron
{
    // Whether Nazg knows what the mode is. A K10 HE V3 may hold others -- the Launcher's Turbocharged
    // and PDO are in no published source -- and those are left as they are.
    [[nodiscard]] bool IsKnown(AdvancedMode mode) noexcept;

    // What a write makes of the profile Nazg keeps, as the firmware does it: the key's advanced
    // fields, and for DKS its slot. Taking a key out of DKS leaves its slot filled.
    void ApplyAdvancedWrite(Profile& profile, uint8_t columns, const AdvancedWrite& write);

    // Whether `profile` holds what `write` asked.
    [[nodiscard]] bool Holds(const Profile& profile, uint8_t columns, const AdvancedWrite& write);

    // The first DKS slot no key of the profile refers to: none when all are taken. Never told by the
    // slot being empty -- clearing a key leaves its slot filled.
    [[nodiscard]] std::optional<uint8_t> FreeDksSlot(const Profile& profile);
    [[nodiscard]] size_t                 DksSlotsInUse(const Profile& profile);

    // ---- gamepad ----
    //
    // The firmware's numbers (game_controller_common.h): 0-11 half-axes, 12 nothing, 13-44 buttons
    // 0-31. Named as an Xbox controller's, as its presets are (xinput_keycodes.h).
    inline constexpr uint8_t c_GamepadControlCount = 45;
    inline constexpr uint8_t c_GamepadFirstButton  = 13;
    inline constexpr uint8_t c_GamepadLeftTrigger  = 4;
    inline constexpr uint8_t c_GamepadRightTrigger = 5;

    [[nodiscard]] bool IsGamepadControl(uint8_t control) noexcept;
    [[nodiscard]] bool IsGamepadButton(uint8_t control) noexcept;   // else a half-axis

    // "L ↑", "LT", "A", "D ←", "Button 17"; and in words, "Left stick, up", "Left trigger", "A button".
    [[nodiscard]] std::string GamepadControlName(uint8_t control);
    [[nodiscard]] std::string GamepadControlWords(uint8_t control);

    // The game controller mode's bits. Read, changed and written back: the byte may hold bits Nazg
    // does not know. RecognitionOff is the K10 HE V3's, in no published source.
    namespace GamepadMode
    {
        inline constexpr uint8_t XInput         = 0x01;   // an Xbox controller; else a plain joystick
        inline constexpr uint8_t Typing         = 0x02;   // a gamepad key types its key too
        inline constexpr uint8_t RecognitionOff = 0x04;   // not seen as a controller at all
    }

    // Whether the firmware has RecognitionOff: the K10 HE V3, whose Keychron protocol is 4. Where it
    // does not, the bit would be stored and do nothing.
    [[nodiscard]] bool HasRecognitionSwitch(uint8_t keychronProtocol) noexcept;

    // How far each control goes, 0 to 1, by the firmware's numbers, from what the computer receives.
    using GamepadLevels = std::array<float, c_GamepadControlCount>;

    [[nodiscard]] GamepadLevels GamepadLevelsOf(const GameControllerReading& reading);

    // ---- One Key Multiple Commands ----
    //
    // The four points in the order a press meets them, the Launcher's columns: going down past the
    // first, down past the second, back up above the third, back up above the fourth.
    enum class DksPoint : uint8_t
    {
        StartingTrigger = 0,   // the slot's shallow press
        BottomIn        = 1,   // deep press
        BottomOut       = 2,   // deep release
        BounceBack      = 3,   // shallow release
    };

    inline constexpr size_t c_DksPoints   = 4;
    inline constexpr size_t c_DksCommands = 4;

    // An action: three bits, each a report 1 ms apart, played in this order.
    namespace DksAction
    {
        inline constexpr uint8_t Release     = 0x1;
        inline constexpr uint8_t Press       = 0x2;
        inline constexpr uint8_t ReleaseLast = 0x4;
        inline constexpr uint8_t Tap         = Press | ReleaseLast;   // what the Launcher writes for a dot
    }

    using DksActions = std::array<uint8_t, c_DksPoints>;   // one command's, by DksPoint

    [[nodiscard]] uint8_t    DksDepth(const DksSlot& slot, DksPoint point) noexcept;   // 0.1 mm
    void                     SetDksDepth(DksSlot& slot, DksPoint point, uint8_t depth) noexcept;
    [[nodiscard]] DksActions DksActionsOf(const DksSlot& slot, size_t command) noexcept;
    void                     SetDksActions(DksSlot& slot, size_t command, const DksActions& actions) noexcept;

    // A command as bars over the points: tapped at one, or held from one to another. Read from what
    // is PLAYED on a full press, not from the bits -- pressing a key already held changes nothing,
    // releasing one ends its bar, and a key never released is let go at the last point, as the
    // firmware does. Every one of the 4096 combinations of a command's actions plays as its bars do.
    struct DksBar
    {
        uint8_t from = 0;   // DksPoint
        uint8_t to   = 0;   // the same: a tap

        bool operator==(const DksBar&) const = default;
    };

    [[nodiscard]] std::vector<DksBar> DksBarsOf(const DksActions& actions);
    [[nodiscard]] DksActions          DksActionsFrom(const std::vector<DksBar>& bars);

    // What a key newly set to One Key Multiple Commands starts with (Rico, 2026-11-23): the key
    // itself, held from the first point to the last, at the Launcher's depths -- 1.1, 2.2, 2.2 and
    // 1.1 mm, within `fullTravel`. A keycode that is neither a basic key nor a modifier starts empty:
    // the firmware would send nothing.
    [[nodiscard]] DksSlot NewDksSlot(uint16_t keycode, uint8_t fullTravel);

    // QMK's basic keycodes and its modifiers, 0x0004-0x00A4 and 0x00E0-0x00E7: all a command sends.
    [[nodiscard]] bool IsDksKeycode(uint16_t keycode) noexcept;

    // A slot played as the firmware plays it (okmc_action()), from the key's travel: what is held now
    // and what was sent since the press began. Nazg reads the travel every 20 ms or so, so a fast press
    // may step over points the firmware met; they are played in turn all the same.
    class DksPlayer
    {
    public:
        struct Sent
        {
            size_t command = 0;
            bool   down    = false;

            bool operator==(const Sent&) const = default;
        };

        void Reset();
        void Step(const DksSlot& slot, int travelSixtieths);

        [[nodiscard]] float                    Depth() const noexcept { return m_Depth; }   // 0.1 mm
        [[nodiscard]] bool                     IsComingUp() const noexcept { return m_ComingUp; }
        [[nodiscard]] bool                     IsHeld(size_t command) const noexcept;
        [[nodiscard]] const std::vector<Sent>& SentSince() const noexcept { return m_Sent; }

    private:
        enum class Stage
        {
            Released,
            Shallow,
            Deep,
            DeepArmed,     // past Bottom Out: coming back above it plays
            DeepReleased,
        };

        bool Advance(const DksSlot& slot, int travelSixtieths);   // whether the stage changed
        void Play(const DksSlot& slot, DksPoint point);
        void ReleaseAll();

        Stage                           m_Stage = Stage::Released;
        std::array<bool, c_DksCommands> m_Held{};
        std::vector<Sent>               m_Sent;
        float                           m_Depth    = 0.0f;
        bool                            m_ComingUp = false;
        int                             m_Deepest  = 0;
    };

    // The shallowest Starting Trigger: at 0.1 mm the firmware never lets the key go (okmc_action()
    // asks it above the point less 0.1 mm).
    inline constexpr uint8_t c_DksShallowestStart = 2;
}
