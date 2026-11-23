// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins a Hall-effect Keychron's SOCD pairs: read, written, and played.
//
// THE BYTES AND THE PRESSES ARE A REAL BOARD'S where they say so: Rico's borrowed K10 HE V3 on
// 2026-11-23, its pairs set in Keychron's Launcher, the presses watched through 0xA9 0x30 beside what
// the computer received (keychron-commands.md, "SOCD").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronSocd.h"

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

    constexpr keychron::KeyPosition c_A{ 3, 1 };
    constexpr keychron::KeyPosition c_D{ 3, 3 };

    // The Gaming profile's SOCD slots 0 to 6 as the board answered them, one pair of each choice: A and D
    // Rappy Snappy, Q and E Last Key Priority, Z and C Absolute Priority, J and L Neutral, U and O again.
    const std::vector<uint8_t> c_Slots{ 0x00, 0x00, 0x00, 0x0B, 0x1B, 0x02, 0x0A, 0x1A, 0x03, 0x14, 0x24,
                                        0x04, 0x3B, 0x4B, 0x06, 0x3A, 0x4A, 0x04, 0x00, 0x00, 0x00 };

    keychron::Profile ProfileWith(const std::vector<uint8_t>& slots)
    {
        std::vector<uint8_t> raw(keychron::ProfileSizeFor(c_Rows, c_Cols, 20, 20), 0);
        std::copy(slots.begin(), slots.end(), raw.begin() + 888);   // 4 + 4 x 126 + 19 x 20
        return keychron::DecodeProfile(raw, c_Rows, c_Cols, 20, 20);
    }

    void TestRead()
    {
        std::printf("The pairs read\n");

        const keychron::Profile profile = ProfileWith(c_Slots);
        Check(keychron::SocdSlotsInUse(profile) == std::vector<uint8_t>{ 1, 2, 3, 4, 5 }, "slots 1 to 5, as the Launcher fills them");
        Check(keychron::FreeSocdSlot(profile) == uint8_t{ 0 }, "slot 0 is free");
        Check(profile.socdSlots[1] == keychron::MakeSocdSlot(c_A, c_D, keychron::SocdType::DeeperThenFirst), "A and D");
        Check(profile.socdSlots[4] == keychron::MakeSocdSlot({ 3, 7 }, { 3, 9 }, keychron::SocdType::Neither), "J and L");
        Check(keychron::SocdSlotPairing(profile, c_A, c_D) == uint8_t{ 1 } &&
                  keychron::SocdSlotPairing(profile, c_D, c_A) == uint8_t{ 1 } &&
                  !keychron::SocdSlotPairing(profile, c_A, { 2, 1 }),
              "a pair is found either way round, and only where it is");

        const keychron::SocdChoice choices[] = { keychron::SocdChoice::RappySnappy, keychron::SocdChoice::LastKeyPriority,
                                                 keychron::SocdChoice::AbsolutePriority, keychron::SocdChoice::Neutral };
        for (size_t slot = 1; slot <= 4; ++slot)
            Check(keychron::SocdChoiceOf(profile.socdSlots[slot].type) == choices[slot - 1], "the Launcher's choice");
        Check(!keychron::SocdChoiceOf(keychron::SocdType::DeeperWins) &&
                  !keychron::SocdChoiceOf(static_cast<keychron::SocdType>(9)),
              "type 1, and a type of no source, are none of the four");

        // The winner is the type's, not the keys' order: swapping it in the Launcher turned 4 into 5.
        using keychron::SocdChoice;
        using keychron::SocdType;
        Check(keychron::SocdTypeFor(SocdChoice::AbsolutePriority, SocdType::LastWins) == SocdType::FirstKeyWins &&
                  keychron::SocdTypeFor(SocdChoice::AbsolutePriority, SocdType::SecondKeyWins) == SocdType::SecondKeyWins,
              "Absolute Priority keeps its winner");
        Check(keychron::SocdTypeFor(SocdChoice::RappySnappy, SocdType::DeeperWins) == SocdType::DeeperThenFirst,
              "Rappy Snappy writes 2, never 1");
    }

    void TestWrites()
    {
        std::printf("AMC_SET_SOCD\n");

        {
            // N and M, Last Key Priority, into slot 0 of Gaming: sent to the board, answered with status 0.
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x16, 0x00 });

            keychron::SocdWrite write;
            write.slot = 0;
            write.pair = keychron::MakeSocdSlot({ 4, 7 }, { 4, 8 }, keychron::SocdType::LastWins);
            Run(protocol.SetSocd(1, write));
            const std::vector<uint8_t> sent{ 0xA9, 0x16, 1, 4, 7, 4, 8, 0, 3 };
            std::vector<uint8_t>       request = channel.RequestAt(0);
            request.resize(sent.size());
            Check(request == sent, "the request, as sent to the board on 2026-11-23");

            // What the board then held, 3C 44 03 in slot 0, is what the write makes of the profile.
            keychron::Profile profile = ProfileWith(c_Slots);
            Check(!keychron::Holds(profile, write), "not held before");
            keychron::ApplySocdWrite(profile, write);
            std::vector<uint8_t> after = c_Slots;
            after[0] = 0x3C;
            after[1] = 0x44;
            after[2] = 0x03;
            Check(keychron::Holds(profile, write) && profile.socdSlots == ProfileWith(after).socdSlots,
                  "the slot the board read back");
            Check(keychron::FreeSocdSlot(profile) == uint8_t{ 6 }, "the next free one");
        }
        {
            // Removing a pair: type 0, and the slot all zero, as the Launcher left slot 5.
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x16, 0x00 });

            keychron::SocdWrite write;
            write.slot = 5;
            Run(protocol.SetSocd(1, write));
            Check(channel.RequestAt(0)[7] == 5 && channel.RequestAt(0)[8] == 0, "the slot, with type 0");

            keychron::Profile profile = ProfileWith(c_Slots);
            keychron::ApplySocdWrite(profile, write);
            Check(keychron::Holds(profile, write) && profile.socdSlots[5] == keychron::SocdSlot{}, "cleared");
            Check(keychron::SocdSlotsInUse(profile) == std::vector<uint8_t>{ 1, 2, 3, 4 }, "the others stay where they are");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x16, 0x01 });
            keychron::SocdWrite write;
            write.pair = keychron::MakeSocdSlot(c_A, c_A, keychron::SocdType::LastWins);
            Check(Throws<ProtocolError>([&] { Run(protocol.SetSocd(0, write)); }), "a key with itself is refused by the board");
            write.pair = keychron::MakeSocdSlot(c_A, { 8, 0 }, keychron::SocdType::LastWins);
            Check(Throws<std::invalid_argument>([&] { Run(protocol.SetSocd(0, write)); }) && !keychron::FitsSocdSlot({ 8, 0 }),
                  "a key a slot cannot hold is never written");
        }

        keychron::Profile full = ProfileWith(c_Slots);
        for (keychron::SocdSlot& slot : full.socdSlots)
            slot = keychron::MakeSocdSlot(c_A, c_D, keychron::SocdType::LastWins);
        Check(!keychron::FreeSocdSlot(full) && keychron::SocdSlotsInUse(full).size() == 20, "twenty pairs take every slot");
        keychron::SocdWrite past;
        past.slot = 20;
        keychron::ApplySocdWrite(full, past);
        Check(!keychron::Holds(full, past), "a slot the board does not have holds nothing");
    }

    // A reading of both keys, 1/60 mm and pressed, and which the computer then had.
    struct Moment
    {
        int  first;
        bool firstPressed;
        int  second;
        bool secondPressed;
        bool firstSent;
        bool secondSent;
    };

    bool Plays(keychron::SocdType type, const std::vector<Moment>& moments)
    {
        keychron::SocdPlayer player;
        for (const Moment& moment : moments)
        {
            const keychron::SocdPlayer::Sent sent = player.Step(type, { moment.first, moment.firstPressed },
                                                                { moment.second, moment.secondPressed });
            if (sent.first != moment.firstSent || sent.second != moment.secondSent)
                return false;
        }
        return true;
    }

    void TestPlayer()
    {
        std::printf("Played\n");
        using keychron::SocdType;

        // The K10's presses, Rico's: travels in 1/60 mm, 215 the bottom.
        // A and D, Rappy Snappy. D alone, lightly; A past it: A is sent; D let go and pressed again to the
        // bottom, A at the bottom all along: A stays; A let go: D.
        Check(Plays(SocdType::DeeperThenFirst,
                    {
                        { 0, false, 120, true, false, true },
                        { 60, false, 84, true, false, true },
                        { 191, true, 120, true, true, false },
                        { 215, true, 114, true, true, false },
                        { 215, true, 103, false, true, false },
                        { 215, true, 126, true, true, false },
                        { 215, true, 215, true, true, false },
                        { 78, false, 215, true, false, true },
                        { 0, false, 0, false, false, false },
                    }),
              "Rappy Snappy: the deeper, and at the bottom the one held first");
        // D at the bottom, then A: D stays, A is never sent.
        Check(Plays(SocdType::DeeperThenFirst,
                    {
                        { 0, false, 215, true, false, true },
                        { 130, true, 215, true, false, true },
                        { 215, true, 215, true, false, true },
                        { 9, false, 215, true, false, true },
                    }),
              "Rappy Snappy: pressed second to the bottom, never sent");
        // The same as type 1, tried on the board: not both, 215 being short of 230.
        Check(Plays(SocdType::DeeperWins,
                    {
                        { 0, false, 215, true, false, true },
                        { 215, true, 215, true, false, true },
                        { 215, true, 0, false, true, false },
                    }),
              "type 1 on a K10 HE: as Rappy Snappy");
        // On a board of 4.0 mm it would send both (the source; on no board yet).
        Check(Plays(SocdType::DeeperWins, { { 0, false, 239, true, false, true }, { 239, true, 239, true, true, true } }) &&
                  Plays(SocdType::DeeperThenFirst,
                        { { 0, false, 239, true, false, true }, { 239, true, 239, true, false, true } }),
              "past 3.83 mm on both, type 1 sends both, type 2 the first");

        // Q and E, Last Key Priority: Q held, E pressed, E let go.
        Check(Plays(SocdType::LastWins,
                    {
                        { 215, true, 0, false, true, false },
                        { 215, true, 207, true, false, true },
                        { 215, true, 215, true, false, true },
                        { 215, true, 104, false, true, false },
                        { 10, false, 0, false, false, false },
                    }),
              "Last Key Priority: the last, then the other again");
        // Z and C, the first key winning: C held, Z pressed, Z let go. The second winning: Z held, C pressed.
        Check(Plays(SocdType::FirstKeyWins,
                    {
                        { 0, false, 215, true, false, true },
                        { 155, true, 215, true, true, false },
                        { 15, false, 215, true, false, true },
                    }),
              "Absolute Priority, the first key");
        Check(Plays(SocdType::SecondKeyWins,
                    {
                        { 215, true, 0, false, true, false },
                        { 215, true, 215, true, false, true },
                        { 215, true, 83, false, true, false },
                    }),
              "Absolute Priority, the second key");
        // J and L, Neutral: J held, L pressed, L let go.
        Check(Plays(SocdType::Neither,
                    {
                        { 215, true, 0, false, true, false },
                        { 215, true, 215, true, false, false },
                        { 215, true, 0, false, true, false },
                        { 0, false, 0, false, false, false },
                    }),
              "Neutral: neither while both are down");
        Check(Plays(SocdType::Off, { { 215, true, 215, true, true, true } }), "no pair holds nothing back");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRead();
    TestWrites();
    TestPlayer();

    return TestResult();
}
