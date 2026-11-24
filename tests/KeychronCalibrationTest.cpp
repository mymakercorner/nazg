// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins a Hall-effect Keychron's calibration: its state read, and its sequence kept away from the firmware's traps.
//
// THE STATES AFTER ARE A REAL BOARD'S: Rico's borrowed K10 HE V3 on 2026-11-24, after a whole calibration and after
// one left early, both run in Keychron's Launcher (keychron-commands.md, "Calibration"). Those of a calibration
// running follow the published source; Nazg's own ran on that board the same day.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronCalibration.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

namespace keychron = nazg::keychron;

using nazg::ProtocolError;
using nazg::Task;
using keychron::CalibrationPhase;

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

    // As read on the board: state 0, both calibrated, the bits of the calibration before.
    const std::vector<uint8_t> c_AfterAll{ 0xA9, 0x41, 0x03, 0x00, 0xFD, 0xFF, 0x01, 0xFF, 0xFF, 0x1F, 0xFF,
                                           0xFF, 0x1F, 0xFF, 0x1F, 0x0E, 0xFD, 0xAF, 0x1E, 0x47, 0xDE, 0x07 };
    const std::vector<uint8_t> c_AfterSome{ 0xA9, 0x41, 0x03, 0x00, 0xFD, 0x3F };

    // One running, with row 0's done keys in its low byte.
    std::vector<uint8_t> Running(uint8_t phase, uint8_t row0) { return { 0xA9, 0x41, 0x01, phase, row0 }; }

    const std::vector<keychron::KeyPosition> c_Three{ { 0, 0 }, { 0, 1 }, { 0, 2 } };
    const std::vector<uint8_t>               c_Accepted{ 0xA9, 0x40, 0x00 };

    void TestState()
    {
        std::printf("the state\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);
        channel.Reply(c_AfterAll);
        channel.Reply(c_AfterSome);
        channel.Reply(Running(3, 0x05));
        channel.Reply(Running(9, 0x00));

        const keychron::CalibrationState all = Run(protocol.GetCalibrationState());
        Check(all.phase == CalibrationPhase::Off, "over, though the bits are still there");
        size_t done = 0;
        for (uint8_t row = 0; row < 6; ++row)
            for (uint8_t column = 0; column < 21; ++column)
                done += all.IsDone({ row, column }) ? 1 : 0;
        Check(done == 104, "the K10's 104 keys");
        Check(!all.IsDone({ 0, 1 }) && all.IsDone({ 0, 16 }) && all.IsDone({ 5, 18 }) && !all.IsDone({ 5, 19 }),
              "by row and column");
        Check(!all.IsDone({ 12, 0 }) && !all.IsDone({ 0, 24 }), "nothing past the reply");

        const keychron::CalibrationState some = Run(protocol.GetCalibrationState());
        Check(some.IsDone({ 0, 0 }) && some.IsDone({ 0, 13 }) && !some.IsDone({ 0, 14 }) && !some.IsDone({ 3, 1 }),
              "Esc and F1 to F12, left early");

        const keychron::CalibrationState running = Run(protocol.GetCalibrationState());
        Check(running.phase == CalibrationPhase::Bottom && keychron::CalibratedCount(running, c_Three) == 2, "running, 2 of 3");
        Check(Run(protocol.GetCalibrationState()).phase != CalibrationPhase::Off, "a phase of no source is not off");
    }

    void TestKeys()
    {
        std::printf("the keys the board has\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);
        channel.Reply({ 0xA9, 0x42, 0x00, 0x00, 0x00, 0x70, 0x0D, 0x36, 0x07 });   // Esc, read on the board
        channel.Reply({ 0xA9, 0x42, 0x00, 0x01 });                                // no key at (0, 1)
        channel.Reply({ 0xA9, 0x42, 0x01, 0x00, 0x00, 0x70, 0x0D, 0x36, 0x07 });
        channel.Reply({ 0xA9, 0x42, 0x01, 0x01, 0x00, 0x70, 0x0D, 0x36, 0x07 });
        const std::vector<keychron::KeyPosition> keys = Run(protocol.GetAnalogKeys(2, 2));
        Check(keys == std::vector<keychron::KeyPosition>{ { 0, 0 }, { 1, 0 }, { 1, 1 } }, "those calibrated");
        Check(channel.RequestAt(3)[2] == 1 && channel.RequestAt(3)[3] == 1, "asked by row and column");
    }

    void TestBegin()
    {
        std::printf("started\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(c_AfterAll);
            channel.Reply(c_Accepted);
            channel.Reply(Running(2, 0));
            channel.Reply(Running(2, 0));
            channel.Reply(Running(3, 0));
            const keychron::CalibrationState state = Run(keychron::BeginCalibration(protocol));
            Check(state.phase == CalibrationPhase::Bottom, "once the board waits for the keys");
            Check(channel.RequestAt(1)[1] == 0x40 && channel.RequestAt(1)[2] == 2, "0x40 2");
            Check(channel.Farewell().size() > 2 && channel.Farewell()[1] == 0x40 && channel.Farewell()[2] == 4,
                  "closing the board would end it");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(Running(3, 0x01));
            (void)Run(keychron::BeginCalibration(protocol));
            Check(channel.RequestCount() == 1 && !channel.Farewell().empty(), "one already running is taken up, not started again");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(c_AfterAll);
            channel.Reply({ 0xA9, 0x40, 0x01 });
            Check(Throws<ProtocolError>([&] { (void)Run(keychron::BeginCalibration(protocol)); }) && channel.Farewell().empty(),
                  "refused");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(c_AfterAll);
            channel.Reply(c_Accepted);
            channel.Reply(Running(2, 0));
            channel.Reply(c_AfterAll);
            Check(Throws<ProtocolError>([&] { (void)Run(keychron::BeginCalibration(protocol)); }) && channel.Farewell().empty(),
                  "left at once by the board: said, and nothing to end");
        }
    }

    void TestWatch()
    {
        std::printf("watched\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);
        protocol.SetCalibrationFarewell(true);
        channel.Reply(Running(3, 0x03));
        channel.Reply(Running(3, 0x07));
        (void)Run(keychron::WatchCalibration(protocol, c_Three));
        Check(!channel.Farewell().empty(), "a key left: closing would end it");
        (void)Run(keychron::WatchCalibration(protocol, c_Three));
        Check(channel.Farewell().empty(), "every key done: the board ends it, closing must not");
    }

    void TestEnd()
    {
        std::printf("ended early\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            protocol.SetCalibrationFarewell(true);
            channel.Reply(Running(3, 0x03));
            channel.Reply(c_Accepted);
            channel.Reply(Running(4, 0x03));
            channel.Reply(Running(0, 0x03));
            const keychron::CalibrationState state = Run(keychron::EndCalibration(protocol, c_Three));
            Check(channel.RequestAt(1)[1] == 0x40 && channel.RequestAt(1)[2] == 4, "0x40 4");
            Check(state.phase == CalibrationPhase::Off && keychron::CalibratedCount(state, c_Three) == 2, "2 keys kept");
            Check(channel.Farewell().empty(), "nothing left to end");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(Running(3, 0x07));
            const keychron::CalibrationState state = Run(keychron::EndCalibration(protocol, c_Three));
            Check(channel.RequestCount() == 1 && state.phase == CalibrationPhase::Bottom,
                  "not once every key is done: the board's timer would spoil the next");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(Running(2, 0x00));
            (void)Run(keychron::EndCalibration(protocol, c_Three));
            Check(channel.RequestCount() == 1, "not while the rest is measured: the board would be dead until replugged");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply(c_AfterAll);
            const keychron::CalibrationState state = Run(keychron::EndCalibration(protocol, c_Three));
            Check(channel.RequestCount() == 1 && state.phase == CalibrationPhase::Off, "over already");
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestState();
    TestKeys();
    TestBegin();
    TestWatch();
    TestEnd();

    return TestResult();
}
