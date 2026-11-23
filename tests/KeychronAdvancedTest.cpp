// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins a Hall-effect Keychron's advanced keys: One Key Multiple Commands (DKS), Long-Press Switch
// (toggle), Gamepad Analog, and the gamepad's mode.
//
// THE BYTES ARE A REAL BOARD'S where they say so: read from Rico's borrowed K10 HE V3 on 2026-11-23,
// set in Keychron's Launcher, and the one hold written to it and tried (keychron-commands.md, "A DKS
// slot read"). The rest pins the request layout the source reads (profile_set_adv_mode()).
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronAdvanced.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace keychron = nazg::keychron;

using nazg::ProtocolError;
using nazg::Task;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");
        return task.TakeResult();
    }

    template <typename TException, typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const TException&)
        {
            return true;
        }
        return false;
    }

    constexpr uint8_t c_Rows = 6;
    constexpr uint8_t c_Cols = 21;

    constexpr uint16_t c_KcW      = 0x001A;
    constexpr uint16_t c_KcLShift = 0x00E1;

    // Profile 0 as the board held it, with `slot0` at slot 0 and `wWord` at W (2, 2).
    keychron::Profile ProfileWith(const std::vector<uint8_t>& slot0, uint32_t wWord)
    {
        std::vector<uint8_t> raw(keychron::ProfileSizeFor(c_Rows, c_Cols, 20, 20), 0);
        raw[0]             = 0x45;   // global 0x00010445
        raw[1]             = 0x04;
        raw[2]             = 0x01;
        const size_t wAt   = 4u + 4u * (2u * c_Cols + 2u);
        for (size_t byte = 0; byte < 4; ++byte)
            raw[wAt + byte] = static_cast<uint8_t>(wWord >> (8 * byte));
        std::copy(slot0.begin(), slot0.end(), raw.begin() + 508);
        return keychron::DecodeProfile(raw, c_Rows, c_Cols, 20, 20);
    }

    // The slot as first found: W, V, U, T, the Launcher's dots, one a point.
    const std::vector<uint8_t> c_SlotWvut{ 0xCB, 0x62, 0x59, 0x1A, 0x00, 0x19, 0x00, 0x18, 0x00, 0x17,
                                           0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x60, 0x60, 0x00 };
    // W at every point and Left Shift at the two deep ones, dots again: it types wWWw.
    const std::vector<uint8_t> c_SlotTaps{ 0xCB, 0x62, 0x59, 0x1A, 0x00, 0xE1, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x66, 0x66, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00 };
    // The hold written: W from Starting Trigger to Bounce Back, Left Shift from Bottom In to Bottom Out.
    const std::vector<uint8_t> c_SlotHold{ 0xCB, 0x62, 0x59, 0x1A, 0x00, 0xE1, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x12, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00 };

    using Bars = std::vector<keychron::DksBar>;

    void TestSlotsRead()
    {
        std::printf("DKS slots, as the K10 HE V3 held them\n");

        const keychron::Profile wvut = ProfileWith(c_SlotWvut, 0);
        const keychron::DksSlot& slot = wvut.dksSlots[0];
        Check(keychron::DksDepth(slot, keychron::DksPoint::StartingTrigger) == 11 &&
                  keychron::DksDepth(slot, keychron::DksPoint::BottomIn) == 22 &&
                  keychron::DksDepth(slot, keychron::DksPoint::BottomOut) == 22 &&
                  keychron::DksDepth(slot, keychron::DksPoint::BounceBack) == 11,
              "1.1, 2.2, 2.2, 1.1 mm, as the Launcher showed");
        Check(slot.keycodes[0] == c_KcW && slot.keycodes[1] == 0x0019 && slot.keycodes[2] == 0x0018 &&
                  slot.keycodes[3] == 0x0017,
              "W, V, U, T");
        for (uint8_t command = 0; command < 4; ++command)
            Check(keychron::DksBarsOf(keychron::DksActionsOf(slot, command)) == Bars{ { command, command } },
                  "each tapped at one point, in the points' order");
        Check(keychron::FreeDksSlot(wvut) == uint8_t{ 0 } && keychron::DksSlotsInUse(wvut) == 0,
              "a filled slot no key refers to is free");

        const keychron::Profile taps = ProfileWith(c_SlotTaps, 0x00300000u);
        const keychron::KeyConfig w  = keychron::EffectiveConfig(taps, { 2, 2 }, c_Cols);
        Check(w.advancedMode == keychron::AdvancedMode::Dks && w.advancedData == 0, "W is in DKS, slot 0");
        Check(keychron::FreeDksSlot(taps) == uint8_t{ 1 } && keychron::DksSlotsInUse(taps) == 1, "so the next free is 1");
        Check(keychron::DksBarsOf(keychron::DksActionsOf(taps.dksSlots[0], 0)) ==
                  Bars{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 } },
              "dots beside one another are taps, not a hold: wWWw");

        const keychron::Profile hold = ProfileWith(c_SlotHold, 0x00300000u);
        Check(keychron::DksBarsOf(keychron::DksActionsOf(hold.dksSlots[0], 0)) == Bars{ { 0, 3 } } &&
                  keychron::DksBarsOf(keychron::DksActionsOf(hold.dksSlots[0], 1)) == Bars{ { 1, 2 } },
              "W held all along, Left Shift at the bottom: walk and run");

        // Long-Press Switch, as the Launcher set it: adv_mode 5 and nothing else.
        Check(keychron::DecodeKeyConfig(0x00500000u).advancedMode == keychron::AdvancedMode::Toggle,
              "0x00500000 is Long-Press Switch");
        Check(keychron::IsKnown(keychron::AdvancedMode::Toggle) &&
                  !keychron::IsKnown(keychron::DecodeKeyConfig(0x00700000u).advancedMode),
              "a mode in no published source is not known");
    }

    void TestWrites()
    {
        std::printf("AMC_SET_ADVANCE_MODE\n");

        {
            // Byte for byte what was sent to the board for the hold, and answered with status 0.
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x15, 0x00 });

            keychron::AdvancedWrite write;
            write.key  = { 2, 2 };
            write.mode = keychron::AdvancedMode::Dks;
            write.data = 0;
            write.dks  = keychron::NewDksSlot(c_KcW, 35);
            write.dks.keycodes[1] = c_KcLShift;
            keychron::SetDksActions(write.dks, 1, keychron::DksActionsFrom({ { 1, 2 } }));
            Run(protocol.SetKeyAdvanced(0, write));

            const std::vector<uint8_t> sent{ 0xA9, 0x15, 0, 1, 2, 2, 0, 11, 11, 22, 22, 0x1A, 0, 0xE1, 0, 0,
                                             0,    0,    0, 0x12, 0x00, 0x00, 0x12, 0, 0, 0, 0 };
            std::vector<uint8_t> request = channel.RequestAt(0);
            request.resize(sent.size());
            Check(request == sent, "the hold's request, as sent to the board on 2026-11-23");

            // What the board then held is what the write makes of the profile.
            keychron::Profile profile = ProfileWith(c_SlotTaps, 0x00300000u);
            Check(!keychron::Holds(profile, c_Cols, write), "not held before");
            keychron::ApplyAdvancedWrite(profile, c_Cols, write);
            Check(keychron::Holds(profile, c_Cols, write), "held once applied");
            Check(profile.dksSlots[0] == ProfileWith(c_SlotHold, 0).dksSlots[0], "the slot the board read back");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x15, 0x00 });
            channel.Reply({ 0xA9, 0x15, 0x00 });
            channel.Reply({ 0xA9, 0x15, 0x00 });

            keychron::AdvancedWrite write;
            write.key  = { 4, 4 };
            write.mode = keychron::AdvancedMode::Toggle;
            write.data = 9;   // a toggle has none
            Run(protocol.SetKeyAdvanced(1, write));
            Check(channel.RequestAt(0)[2] == 1 && channel.RequestAt(0)[3] == 3, "toggle is operation 3, not its stored 5");
            Check(channel.RequestAt(0)[4] == 4 && channel.RequestAt(0)[5] == 4, "row, column");

            write.mode = keychron::AdvancedMode::Gamepad;
            write.data = 13;
            Run(protocol.SetKeyAdvanced(1, write));
            Check(channel.RequestAt(1)[3] == 2 && channel.RequestAt(1)[6] == 13, "gamepad is operation 2, then the control");

            write.mode = keychron::AdvancedMode::None;
            Run(protocol.SetKeyAdvanced(1, write));
            Check(channel.RequestAt(2)[3] == 0 && channel.RequestAt(2)[6] == 0, "none is operation 0");

            write.mode = static_cast<keychron::AdvancedMode>(7);
            Check(Throws<std::invalid_argument>([&] { Run(protocol.SetKeyAdvanced(1, write)); }),
                  "a mode Nazg does not know is never written");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x15, 0x01 });
            Check(Throws<ProtocolError>([&] { Run(protocol.SetKeyAdvanced(0, {})); }), "a non-zero status is refused");
        }

        // Taking a key out of DKS leaves its slot filled, as seen on the board.
        keychron::Profile profile = ProfileWith(c_SlotTaps, 0x00300000u);
        keychron::AdvancedWrite clear;
        clear.key = { 2, 2 };
        keychron::ApplyAdvancedWrite(profile, c_Cols, clear);
        Check(keychron::EffectiveConfig(profile, { 2, 2 }, c_Cols).advancedMode == keychron::AdvancedMode::None,
              "the key is out of DKS");
        Check(!profile.dksSlots[0].IsEmpty() && keychron::FreeDksSlot(profile) == uint8_t{ 0 },
              "its slot stays filled, and is free again");

        // Every slot taken.
        for (size_t slot = 0; slot < 20; ++slot)
        {
            profile.keys[slot].advancedMode = keychron::AdvancedMode::Dks;
            profile.keys[slot].advancedData = static_cast<uint8_t>(slot);
        }
        Check(!keychron::FreeDksSlot(profile) && keychron::DksSlotsInUse(profile) == 20, "twenty keys take every slot");
    }

    void TestGamepad()
    {
        std::printf("the gamepad's controls and mode\n");

        Check(keychron::GamepadControlWords(3) == "Left stick, up" && keychron::GamepadControlName(4) == "LT" &&
                  keychron::GamepadControlName(13) == "A" && keychron::GamepadControlName(27) == "Xbox",
              "named as an Xbox controller's");
        Check(keychron::GamepadControlName(28) == "Button 16" && keychron::GamepadControlName(44) == "Button 32",
              "past them, buttons counted from 1");
        Check(!keychron::IsGamepadControl(12) && keychron::IsGamepadControl(11) && keychron::IsGamepadControl(44) &&
                  !keychron::IsGamepadControl(45),
              "12 is no control, nor anything past 44");
        Check(!keychron::IsGamepadButton(11) && keychron::IsGamepadButton(13), "buttons from 13");

        {
            // Read on the board: 00 Standard, 01 Xbox, 02 typing, 04 recognition off.
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x22, 0x00, 0x06 });
            Check(Run(protocol.GetGamepadMode()) == (keychron::GamepadMode::Typing | keychron::GamepadMode::RecognitionOff),
                  "06: keyboard keys on, recognition off");
        }
        {
            // The set's answer is the request back: not a refusal. What counts is what is read then.
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.ReplyEcho();
            channel.Reply({ 0xA9, 0x22, 0x00, 0x01 });
            Check(Run(protocol.SetGamepadMode(0x01)) == 0x01, "the mode the board holds, read back");
            Check(channel.RequestAt(0)[1] == 0x23 && channel.RequestAt(0)[2] == 0x01 && channel.RequestAt(1)[1] == 0x22,
                  "set, then read");
        }
        Check(keychron::HasRecognitionSwitch(4) && !keychron::HasRecognitionSwitch(2),
              "the recognition switch is the K10 HE V3's, protocol 4");
    }

    // What Windows received from the K10 HE V3 as a Standard Controller (2026-11-23): W took Y from the
    // centre to its least, A X to its least, D to its most; Left Ctrl took Z to its least, U Ry to its least;
    // / was button 0, ' 1 and Up 10.
    void TestLevels()
    {
        std::printf("what the computer receives\n");

        nazg::GameControllerReading plain;
        plain.isSeen  = true;
        plain.axes    = { 0.5f, -1.0f, -0.7f, 0.2f, -0.3f, 0.0f };
        plain.buttons = std::vector<bool>(16, false);
        plain.buttons[0] = plain.buttons[10] = true;
        keychron::GamepadLevels levels = keychron::GamepadLevelsOf(plain);
        Check(levels[3] == 1.0f && levels[2] == 0.0f, "Y at its least is the left stick up");
        Check(levels[1] == 0.5f && levels[0] == 0.0f, "X above the centre is right");
        Check(levels[4] > 0.69f && levels[5] == 0.0f, "Z below the centre is the left trigger, sharing the right one's axis");
        Check(levels[9] > 0.29f && levels[7] > 0.19f, "Ry at its least is the right stick up, Rx right");
        Check(levels[13] == 1.0f && levels[14] == 0.0f && levels[23] == 1.0f, "button 0 is A, 10 the D-pad's up");

        nazg::GameControllerReading xbox;
        xbox.isSeen  = true;
        xbox.isXbox  = true;
        xbox.axes    = { -0.4f, 0.6f, 0.0f, -0.9f, 0.1f, 0.8f };
        xbox.buttons = std::vector<bool>(15, false);
        xbox.buttons[14] = true;
        levels = keychron::GamepadLevelsOf(xbox);
        Check(levels[0] > 0.39f && levels[2] > 0.59f && levels[9] > 0.89f, "an Xbox controller's sticks");
        Check(levels[4] > 0.09f && levels[5] > 0.79f, "and its triggers, each its own");
        Check(levels[27] == 1.0f, "button 14 is Xbox");

        levels = keychron::GamepadLevelsOf({});
        Check(std::all_of(levels.begin(), levels.end(), [](float level) { return level == 0.0f; }), "nothing read, nothing pressed");
    }

    // One command played on a full press as okmc_action() plays it: every change of the key, by point.
    using Played = std::vector<std::pair<size_t, bool>>;
    Played Play(const keychron::DksActions& actions)
    {
        bool   held = false;
        Played played;
        for (size_t point = 0; point < actions.size(); ++point)
            for (int bit = 0; bit < 3; ++bit)
            {
                if (!(actions[point] >> bit & 1))
                    continue;
                const bool down = bit == 1;
                if (down != held)
                {
                    held = down;
                    played.emplace_back(point, down);
                }
            }
        if (held)
            played.emplace_back(actions.size() - 1, false);
        return played;
    }

    // The slots as the K10 HE V3 played them: wWWw for the Launcher's taps, walk and run for the hold.
    void TestPlayer()
    {
        std::printf("a slot played\n");

        using Sent = std::vector<keychron::DksPlayer::Sent>;
        const keychron::DksSlot taps = ProfileWith(c_SlotTaps, 0).dksSlots[0];
        const keychron::DksSlot hold = ProfileWith(c_SlotHold, 0).dksSlots[0];

        const auto press = [](keychron::DksPlayer& player, const keychron::DksSlot& slot, int deepest, int step)
        {
            for (int travel = 0; travel <= deepest; travel += step)
                player.Step(slot, travel);
            player.Step(slot, deepest);
            for (int travel = deepest; travel >= 0; travel -= step)
                player.Step(slot, travel);
            player.Step(slot, 0);
        };

        // Commands: 0 W, 1 Left Shift. A tap is down then up; at the deep points W and Shift go down in one
        // report and up in the next.
        const Sent wWWw{ { 0, true }, { 0, false },                                         // w
                         { 0, true }, { 1, true }, { 0, false }, { 1, false },              // W
                         { 0, true }, { 1, true }, { 0, false }, { 1, false },              // W
                         { 0, true }, { 0, false } };                                       // w
        keychron::DksPlayer player;
        press(player, taps, 210, 3);
        Check(player.SentSince() == wWWw, "the taps type wWWw, as on the board");
        press(player, taps, 210, 70);
        Check(player.SentSince() == wWWw, "and read too far apart to meet every point, the same");

        player.Reset();
        press(player, hold, 210, 3);
        Check(player.SentSince() == Sent{ { 0, true }, { 1, true }, { 1, false }, { 0, false } },
              "walk and run: W, Shift at the bottom, Shift off, W off");
        press(player, hold, 100, 3);
        Check(player.SentSince() == Sent{ { 0, true }, { 0, false } }, "let go before Bottom In: W alone, released");

        player.Reset();
        for (int travel = 0; travel <= 120; travel += 6)
            player.Step(hold, travel);
        Check(player.IsHeld(0) && !player.IsHeld(1) && !player.IsComingUp(), "part way down: W held, Shift not yet");
        player.Step(hold, 160);
        Check(player.IsHeld(1) && player.Depth() > 26.0f, "past Bottom In: Shift too");
        player.Step(hold, 130);
        Check(player.IsHeld(0) && !player.IsHeld(1) && player.IsComingUp(), "coming up past Bottom Out: Shift off, W held");

        // Bottom Out at full travel is never gone past, so neither it nor Bounce Back plays.
        keychron::DksSlot late = taps;
        keychron::SetDksDepth(late, keychron::DksPoint::BottomOut, 36);
        player.Reset();
        press(player, late, 210, 3);
        Check(player.SentSince().size() == 6, "a Bottom Out never passed: the last two points play nothing");
    }

    void TestBars()
    {
        std::printf("a command as bars\n");

        Check(keychron::DksActionsFrom({ { 0, 3 } }) == keychron::DksActions{ 2, 0, 0, 1 }, "held from end to end");
        Check(keychron::DksActionsFrom({ { 1, 1 } }) == keychron::DksActions{ 0, 6, 0, 0 }, "a tap is the Launcher's 0b110");
        Check(keychron::DksActionsFrom({ { 0, 1 }, { 1, 2 } }) == keychron::DksActions{ 2, 3, 1, 0 },
              "two holds meeting: released and pressed again");
        Check(keychron::DksBarsOf({ 2, 0, 0, 0 }) == Bars{ { 0, 3 } }, "never released: let go at the end");

        size_t different = 0;
        for (unsigned n = 0; n < 4096; ++n)
        {
            const keychron::DksActions actions{ static_cast<uint8_t>(n & 7), static_cast<uint8_t>(n >> 3 & 7),
                                                static_cast<uint8_t>(n >> 6 & 7), static_cast<uint8_t>(n >> 9 & 7) };
            different += Play(keychron::DksActionsFrom(keychron::DksBarsOf(actions))) != Play(actions) ? 1u : 0u;
        }
        Check(different == 0, "every combination of actions plays as its bars do");

        const keychron::DksSlot fresh = keychron::NewDksSlot(c_KcW, 35);
        Check(fresh.keycodes[0] == c_KcW && keychron::DksBarsOf(keychron::DksActionsOf(fresh, 0)) == Bars{ { 0, 3 } } &&
                  fresh.keycodes[1] == 0,
              "a new one holds the key itself, the other commands empty");
        Check(fresh.shallowPress == 11 && fresh.deepPress == 22 && fresh.deepRelease == 22 && fresh.shallowRelease == 11,
              "at the Launcher's depths");
        Check(keychron::NewDksSlot(0x5221, 35).keycodes[0] == 0, "a layer key starts empty: the firmware would send nothing");
        Check(keychron::NewDksSlot(c_KcW, 20).deepPress == 20, "never past the board's travel");
        Check(keychron::IsDksKeycode(c_KcLShift) && !keychron::IsDksKeycode(0x00A5) && !keychron::IsDksKeycode(0),
              "basic keys and modifiers only");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestSlotsRead();
    TestWrites();
    TestGamepad();
    TestBars();
    TestPlayer();
    TestLevels();

    return TestResult();
}
