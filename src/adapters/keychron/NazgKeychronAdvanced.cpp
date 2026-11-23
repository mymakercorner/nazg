// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeychronAdvanced.h"

#include <algorithm>
#include <utility>

namespace nazg::keychron
{
    namespace
    {
        // The Keychron protocol (0xA0) of the one board read with RecognitionOff, a K10 HE V3. Every
        // published branch says 2.
        constexpr uint8_t c_ProtocolWithRecognition = 4;

        // The Launcher's depths for a key newly set to One Key Multiple Commands, in 0.1 mm.
        constexpr uint8_t c_NewShallow = 11;
        constexpr uint8_t c_NewDeep    = 22;

        KeyConfig* KeyOf(Profile& profile, uint8_t columns, KeyPosition key)
        {
            const size_t index = static_cast<size_t>(key.row) * columns + key.column;
            return key.column < columns && index < profile.keys.size() ? &profile.keys[index] : nullptr;
        }

        const KeyConfig* KeyOf(const Profile& profile, uint8_t columns, KeyPosition key)
        {
            const size_t index = static_cast<size_t>(key.row) * columns + key.column;
            return key.column < columns && index < profile.keys.size() ? &profile.keys[index] : nullptr;
        }

        struct ControlName
        {
            const char* shortName;
            const char* words;
        };

        // The half-axes, then -- from 13 -- the buttons, as xinput_keycodes.h orders them.
        constexpr ControlName c_Axes[] = {
            { "L \xE2\x86\x90", "Left stick, left" },   { "L \xE2\x86\x92", "Left stick, right" },
            { "L \xE2\x86\x93", "Left stick, down" },   { "L \xE2\x86\x91", "Left stick, up" },
            { "LT", "Left trigger" },                   { "RT", "Right trigger" },
            { "R \xE2\x86\x90", "Right stick, left" },  { "R \xE2\x86\x92", "Right stick, right" },
            { "R \xE2\x86\x93", "Right stick, down" },  { "R \xE2\x86\x91", "Right stick, up" },
            { "RZ -", "RZ axis, minus" },               { "RZ +", "RZ axis, plus" },
        };
        constexpr ControlName c_Buttons[] = {
            { "A", "A button" },          { "B", "B button" },
            { "X", "X button" },          { "Y", "Y button" },
            { "LB", "Left bumper" },      { "RB", "Right bumper" },
            { "View", "View button" },    { "Menu", "Menu button" },
            { "L3", "Left stick pressed" },           { "R3", "Right stick pressed" },
            { "D \xE2\x86\x91", "D-pad up" },         { "D \xE2\x86\x93", "D-pad down" },
            { "D \xE2\x86\x90", "D-pad left" },       { "D \xE2\x86\x92", "D-pad right" },
            { "Xbox", "Xbox button" },
        };
        constexpr uint8_t c_AxisCount        = static_cast<uint8_t>(sizeof(c_Axes) / sizeof(c_Axes[0]));
        constexpr uint8_t c_NamedButtonCount = static_cast<uint8_t>(sizeof(c_Buttons) / sizeof(c_Buttons[0]));
    }

    bool IsKnown(AdvancedMode mode) noexcept
    {
        return mode == AdvancedMode::None || mode == AdvancedMode::Dks || mode == AdvancedMode::Gamepad ||
               mode == AdvancedMode::Toggle;
    }

    void ApplyAdvancedWrite(Profile& profile, uint8_t columns, const AdvancedWrite& write)
    {
        KeyConfig* key = KeyOf(profile, columns, write.key);
        if (!key)
            return;
        key->advancedMode = write.mode;
        key->advancedData =
            write.mode == AdvancedMode::Dks || write.mode == AdvancedMode::Gamepad ? write.data : uint8_t{ 0 };
        if (write.mode == AdvancedMode::Dks && write.data < profile.dksSlots.size())
            profile.dksSlots[write.data] = write.dks;
    }

    bool Holds(const Profile& profile, uint8_t columns, const AdvancedWrite& write)
    {
        const KeyConfig* key = KeyOf(profile, columns, write.key);
        if (!key || key->advancedMode != write.mode)
            return false;
        if (write.mode == AdvancedMode::Gamepad)
            return key->advancedData == write.data;
        if (write.mode == AdvancedMode::Dks)
            return key->advancedData == write.data && write.data < profile.dksSlots.size() &&
                   profile.dksSlots[write.data] == write.dks;
        return true;
    }

    std::optional<uint8_t> FreeDksSlot(const Profile& profile)
    {
        for (size_t slot = 0; slot < profile.dksSlots.size() && slot <= 0xFF; ++slot)
        {
            const bool taken = std::any_of(profile.keys.begin(), profile.keys.end(), [&](const KeyConfig& key)
                                           { return key.advancedMode == AdvancedMode::Dks && key.advancedData == slot; });
            if (!taken)
                return static_cast<uint8_t>(slot);
        }
        return std::nullopt;
    }

    size_t DksSlotsInUse(const Profile& profile)
    {
        size_t used = 0;
        for (size_t slot = 0; slot < profile.dksSlots.size(); ++slot)
            used += std::any_of(profile.keys.begin(), profile.keys.end(), [&](const KeyConfig& key)
                                { return key.advancedMode == AdvancedMode::Dks && key.advancedData == slot; })
                        ? 1u
                        : 0u;
        return used;
    }

    bool IsGamepadControl(uint8_t control) noexcept
    {
        return control < c_GamepadControlCount && control != c_AxisCount;
    }

    bool IsGamepadButton(uint8_t control) noexcept
    {
        return control >= c_GamepadFirstButton && control < c_GamepadControlCount;
    }

    std::string GamepadControlName(uint8_t control)
    {
        if (control < c_AxisCount)
            return c_Axes[control].shortName;
        if (IsGamepadButton(control) && control - c_GamepadFirstButton < c_NamedButtonCount)
            return c_Buttons[control - c_GamepadFirstButton].shortName;
        // Counted from 1, as Windows' controller page does.
        return IsGamepadButton(control) ? "Button " + std::to_string(control - c_GamepadFirstButton + 1) : "?";
    }

    std::string GamepadControlWords(uint8_t control)
    {
        if (control < c_AxisCount)
            return c_Axes[control].words;
        if (IsGamepadButton(control) && control - c_GamepadFirstButton < c_NamedButtonCount)
            return c_Buttons[control - c_GamepadFirstButton].words;
        return GamepadControlName(control);
    }

    GamepadLevels GamepadLevelsOf(const GameControllerReading& reading)
    {
        GamepadLevels levels{};
        const auto    axis   = [&](size_t index) { return index < reading.axes.size() ? reading.axes[index] : 0.0f; };
        const auto    button = [&](size_t index) { return index < reading.buttons.size() && reading.buttons[index]; };
        const auto    halves = [&](float value, uint8_t negative, uint8_t positive)
        {
            levels[negative] = std::clamp(-value, 0.0f, 1.0f);
            levels[positive] = std::clamp(value, 0.0f, 1.0f);
        };

        // X to the right; Y down, and the firmware numbers down before up.
        halves(axis(0), 0, 1);
        halves(-axis(1), 2, 3);
        if (reading.isXbox)
        {
            halves(axis(2), 6, 7);
            halves(-axis(3), 8, 9);
            levels[c_GamepadLeftTrigger]  = std::clamp(axis(4), 0.0f, 1.0f);
            levels[c_GamepadRightTrigger] = std::clamp(axis(5), 0.0f, 1.0f);
        }
        else
        {
            halves(axis(2), c_GamepadLeftTrigger, c_GamepadRightTrigger);   // one axis for both
            halves(axis(3), 6, 7);
            halves(-axis(4), 8, 9);
            halves(axis(5), 10, 11);
        }
        // The buttons are in the firmware's order in both.
        for (size_t index = 0; c_GamepadFirstButton + index < c_GamepadControlCount; ++index)
            levels[c_GamepadFirstButton + index] = button(index) ? 1.0f : 0.0f;
        return levels;
    }

    bool HasRecognitionSwitch(uint8_t keychronProtocol) noexcept
    {
        return keychronProtocol >= c_ProtocolWithRecognition;
    }

    uint8_t DksDepth(const DksSlot& slot, DksPoint point) noexcept
    {
        switch (point)
        {
        case DksPoint::StartingTrigger: return slot.shallowPress;
        case DksPoint::BottomIn: return slot.deepPress;
        case DksPoint::BottomOut: return slot.deepRelease;
        case DksPoint::BounceBack: return slot.shallowRelease;
        }
        return 0;
    }

    void SetDksDepth(DksSlot& slot, DksPoint point, uint8_t depth) noexcept
    {
        switch (point)
        {
        case DksPoint::StartingTrigger: slot.shallowPress = depth; break;
        case DksPoint::BottomIn: slot.deepPress = depth; break;
        case DksPoint::BottomOut: slot.deepRelease = depth; break;
        case DksPoint::BounceBack: slot.shallowRelease = depth; break;
        }
    }

    DksActions DksActionsOf(const DksSlot& slot, size_t command) noexcept
    {
        if (command >= c_DksCommands)
            return {};
        return { slot.onShallowPress[command], slot.onDeepPress[command], slot.onDeepRelease[command],
                 slot.onShallowRelease[command] };
    }

    void SetDksActions(DksSlot& slot, size_t command, const DksActions& actions) noexcept
    {
        if (command >= c_DksCommands)
            return;
        slot.onShallowPress[command]   = actions[static_cast<size_t>(DksPoint::StartingTrigger)];
        slot.onDeepPress[command]      = actions[static_cast<size_t>(DksPoint::BottomIn)];
        slot.onDeepRelease[command]    = actions[static_cast<size_t>(DksPoint::BottomOut)];
        slot.onShallowRelease[command] = actions[static_cast<size_t>(DksPoint::BounceBack)];
    }

    std::vector<DksBar> DksBarsOf(const DksActions& actions)
    {
        std::vector<DksBar>    bars;
        std::optional<uint8_t> from;
        for (uint8_t point = 0; point < c_DksPoints; ++point)
        {
            // The three reports, in their order: release, press, release.
            const bool reports[] = { (actions[point] & DksAction::Release) != 0, (actions[point] & DksAction::Press) != 0,
                                     (actions[point] & DksAction::ReleaseLast) != 0 };
            for (size_t report = 0; report < 3; ++report)
            {
                if (!reports[report])
                    continue;
                if (report == 1)
                {
                    if (!from)
                        from = point;
                }
                else if (from)
                {
                    bars.push_back({ *from, point });
                    from.reset();
                }
            }
        }
        if (from)
            bars.push_back({ *from, static_cast<uint8_t>(c_DksPoints - 1) });
        return bars;
    }

    DksActions DksActionsFrom(const std::vector<DksBar>& bars)
    {
        DksActions actions{};
        for (const DksBar bar : bars)
        {
            if (bar.from >= c_DksPoints || bar.to >= c_DksPoints)
                continue;
            if (bar.from == bar.to)
            {
                actions[bar.from] |= DksAction::Tap;
                continue;
            }
            actions[bar.from] |= DksAction::Press;
            actions[bar.to] |= DksAction::Release;
        }
        return actions;
    }

    void DksPlayer::Reset()
    {
        *this = DksPlayer();
    }

    bool DksPlayer::IsHeld(size_t command) const noexcept
    {
        return command < m_Held.size() && m_Held[command];
    }

    void DksPlayer::Step(const DksSlot& slot, int travelSixtieths)
    {
        if (m_Stage == Stage::Released && travelSixtieths >= slot.shallowPress * 6 && slot.shallowPress > 0)
        {
            m_Sent.clear();
            m_Deepest = 0;
        }
        // Coming up once it is clearly above the deepest it went: half a tenth's noise is not a turn.
        m_Deepest  = travelSixtieths <= 0 ? 0 : std::max(m_Deepest, travelSixtieths);
        m_ComingUp = travelSixtieths < m_Deepest - 6;
        m_Depth    = static_cast<float>(travelSixtieths) / 6.0f;

        // A reading may step over several points: each stage in turn, until none changes.
        for (int steps = 0; steps < 6 && Advance(slot, travelSixtieths); ++steps)
        {
        }
    }

    bool DksPlayer::Advance(const DksSlot& slot, int travelSixtieths)
    {
        const int  travel = travelSixtieths;
        const bool up     = travel < slot.shallowRelease * 6 && travel < (slot.shallowPress - 1) * 6;
        switch (m_Stage)
        {
        case Stage::Released:
            if (slot.shallowPress == 0 || travel < slot.shallowPress * 6)
                return false;
            m_Stage = Stage::Shallow;
            Play(slot, DksPoint::StartingTrigger);
            return true;
        case Stage::Shallow:
            if (up)
            {
                m_Stage = Stage::Released;
                ReleaseAll();
                return false;
            }
            if (travel < slot.deepPress * 6)
                return false;
            m_Stage = Stage::Deep;
            Play(slot, DksPoint::BottomIn);
            return true;
        case Stage::Deep:
            if (travel > slot.deepRelease * 6)
            {
                m_Stage = Stage::DeepArmed;
                return true;
            }
            if (up)
            {
                m_Stage = Stage::Released;
                ReleaseAll();
            }
            return false;
        case Stage::DeepArmed:
            if (travel > slot.deepRelease * 6)
                return false;
            m_Stage = Stage::DeepReleased;
            Play(slot, DksPoint::BottomOut);
            return true;
        case Stage::DeepReleased:
            if (travel <= slot.shallowRelease * 6 && travel < (slot.shallowPress - 1) * 6)
            {
                m_Stage = Stage::Released;
                Play(slot, DksPoint::BounceBack);
                ReleaseAll();
            }
            return false;
        }
        return false;
    }

    void DksPlayer::Play(const DksSlot& slot, DksPoint point)
    {
        // Every command's release, then every press, then every last release: a report each.
        constexpr uint8_t c_Reports[] = { DksAction::Release, DksAction::Press, DksAction::ReleaseLast };
        for (const uint8_t report : c_Reports)
            for (size_t command = 0; command < c_DksCommands; ++command)
            {
                if (!slot.keycodes[command] ||
                    !(DksActionsOf(slot, command)[static_cast<size_t>(point)] & report))
                    continue;
                const bool down = report == DksAction::Press;
                if (m_Held[command] == down)
                    continue;
                m_Held[command] = down;
                m_Sent.push_back({ command, down });
            }
    }

    void DksPlayer::ReleaseAll()
    {
        for (size_t command = 0; command < c_DksCommands; ++command)
            if (std::exchange(m_Held[command], false))
                m_Sent.push_back({ command, false });
    }

    bool IsDksKeycode(uint16_t keycode) noexcept
    {
        return (keycode >= 0x0004 && keycode <= 0x00A4) || (keycode >= 0x00E0 && keycode <= 0x00E7);
    }

    DksSlot NewDksSlot(uint16_t keycode, uint8_t fullTravel)
    {
        const uint8_t full    = fullTravel ? fullTravel : uint8_t{ 40 };
        const uint8_t shallow = std::min(c_NewShallow, full);
        const uint8_t deep    = std::min(c_NewDeep, full);

        DksSlot slot;
        slot.shallowPress   = shallow;
        slot.shallowRelease = shallow;
        slot.deepPress      = deep;
        slot.deepRelease    = deep;
        if (IsDksKeycode(keycode))
        {
            slot.keycodes[0] = keycode;
            SetDksActions(slot, 0, DksActionsFrom({ { 0, static_cast<uint8_t>(c_DksPoints - 1) } }));
        }
        return slot;
    }
}
