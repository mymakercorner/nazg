// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins a Hall-effect Keychron's gamepad curve: read, checked, written, and played.
//
// THE BYTES AND THE OUTPUTS ARE A REAL BOARD'S where they say so: Rico's borrowed K10 HE V3 on 2026-11-24,
// its curve set in Keychron's Launcher, W pressed slowly and read through 0xA9 0x30 beside Windows'
// joystick (keychron-commands.md, "Game controller").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronCurve.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
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

    using Curve = std::vector<keychron::CurvePoint>;

    // Read on the board, as the Launcher stored them.
    const std::vector<uint8_t> c_LinearReply{ 0xA9, 0x20, 0x00, 0x00, 0x0B, 0x2A, 0x17, 0x54, 0x23, 0x7F };
    const std::vector<uint8_t> c_SmoothReply{ 0xA9, 0x20, 0x00, 0x00, 0x0C, 0x13, 0x17, 0x30, 0x23, 0x7F };

    void TestPresets()
    {
        std::printf("the Launcher's two curves\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);
        channel.Reply(c_LinearReply);
        channel.Reply(c_SmoothReply);
        Check(Run(protocol.GetCurve()) == keychron::LinearCurve(35), "Linear, as the Launcher stored it");
        Check(Run(protocol.GetCurve()) == keychron::SmoothCurve(35), "Smooth, as the Launcher stored it");
        for (int full = 10; full <= 40; ++full)
            Check(keychron::IsWritableCurve(keychron::LinearCurve(static_cast<uint8_t>(full))) &&
                      keychron::IsWritableCurve(keychron::SmoothCurve(static_cast<uint8_t>(full))),
                  "both writable at any travel");
    }

    void TestWritable()
    {
        std::printf("what may be written\n");

        Check(keychron::IsWritableCurve({ { 0, 0 }, { 8, 31 }, { 26, 95 }, { 35, 127 } }), "the board's own default");
        Check(keychron::IsWritableCurve({ { 5, 20 }, { 6, 20 }, { 7, 127 }, { 35, 127 } }), "a dead zone, a plateau");
        Check(!keychron::IsWritableCurve({ { 0, 0 }, { 8, 31 }, { 8, 95 }, { 35, 127 } }), "two points at one travel");
        Check(!keychron::IsWritableCurve({ { 0, 0 }, { 26, 31 }, { 8, 95 }, { 35, 127 } }), "a point before its neighbour");
        Check(!keychron::IsWritableCurve({ { 0, 0 }, { 8, 31 }, { 26, 128 }, { 35, 127 } }), "an output over 127");
        Check(!keychron::IsWritableCurve({ { 0, 0 }, { 8, 31 }, { 35, 127 } }) && !keychron::IsWritableCurve({}),
              "not four points");
    }

    void TestMoved()
    {
        std::printf("a point moved\n");

        const Curve linear = keychron::LinearCurve(35);
        Check(keychron::WithCurvePoint(linear, 1, 12, 19) == Curve{ { 0, 0 }, { 12, 19 }, { 23, 84 }, { 35, 127 } },
              "where it is put");
        Check(keychron::WithCurvePoint(linear, 1, 30, 127)[1] == keychron::CurvePoint{ 22, 84 },
              "0.1 mm short of the next, no higher");
        Check(keychron::WithCurvePoint(linear, 2, -5, -5)[2] == keychron::CurvePoint{ 12, 42 },
              "0.1 mm past the one before, no lower");
        Check(keychron::WithCurvePoint(linear, 0, -3, -3)[0] == keychron::CurvePoint{ 0, 0 }, "the first, not under rest");
        Check(keychron::WithCurvePoint(linear, 0, 6, 10)[0] == keychron::CurvePoint{ 6, 10 }, "the first, a dead zone");
        Check(keychron::WithCurvePoint(linear, 3, 20, 100)[3] == keychron::CurvePoint{ 35, 100 },
              "the last keeps its travel");
        Check(keychron::WithCurvePoint(linear, 4, 1, 1) == linear, "no fifth point");
        for (size_t point = 0; point < 4; ++point)
            for (int x = -2; x < 40; ++x)
                Check(keychron::IsWritableCurve(keychron::WithCurvePoint(linear, point, x, 300)), "always writable");

        const Curve crossed{ { 0, 0 }, { 20, 90 }, { 10, 40 }, { 35, 127 } };
        Check(keychron::WithCurvePoint(crossed, 1, 5, 50) == Curve{ { 0, 0 }, { 5, 40 }, { 10, 40 }, { 35, 127 } },
              "a curve that did not ascend, put right");
        const Curve squeezed{ { 10, 90 }, { 12, 90 }, { 10, 40 }, { 35, 127 } };
        Check(keychron::WithCurvePoint(squeezed, 1, 11, 50) == squeezed, "no room between its neighbours: it stays");
    }

    void TestPlayed()
    {
        std::printf("played\n");

        // W on the K10 HE V3, Linear, replugged: what Windows received at each 0.1 mm of travel.
        constexpr int c_Received[] = { 0,  3,  7,  11, 15, 19, 22, 26, 30, 34, 38,  42,  45,  49,  52,  56,  59,  63,
                                       66, 70, 73, 77, 80, 84, 87, 91, 94, 98, 101, 105, 109, 112, 116, 119, 123, 127 };
        bool same = true;
        for (int travel = 0; travel <= 35; ++travel)
            same = same && static_cast<int>(keychron::CurveOutput(keychron::LinearCurve(35), static_cast<float>(travel))) ==
                               c_Received[travel];
        Check(same, "as the board plays it, at every travel");

        const Curve dead{ { 6, 10 }, { 12, 42 }, { 30, 127 }, { 35, 127 } };
        Check(keychron::CurveOutput(dead, 5) == 0 && keychron::CurveOutput(dead, 6) == 10, "nothing under the first point");
        Check(keychron::CurveOutput(dead, 30) == 127 && keychron::CurveOutput(dead, 33) == 127, "the most before the bottom");
        Check(keychron::CurveOutput(keychron::LinearCurve(35), 36) == 127, "past the last point");
        const Curve lifted{ { 0, 20 }, { 12, 42 }, { 30, 127 }, { 35, 127 } };
        Check(keychron::CurveOutput(lifted, 0) == 0, "nothing at rest");
        Check(keychron::CurveOutput({}, 10) == 0, "no curve");
        Check(keychron::CurveOutput(keychron::SmoothCurve(35), 17.5f) == 33.5f, "between two tenths, the line");
        Check(keychron::CurveOutput(dead, 5.9f) == 0.0f, "nothing just under the first point");
    }

    void TestWritten()
    {
        std::printf("written\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x21, 0x00 });
            channel.Reply(c_SmoothReply);
            Check(Run(protocol.SetCurve(keychron::SmoothCurve(35))) == keychron::SmoothCurve(35), "what the board kept");

            const std::vector<uint8_t> sent = channel.RequestAt(0);
            const std::vector<uint8_t> expected{ 0xA9, 0x21, 0x00, 0x00, 0x0C, 0x13, 0x17, 0x30, 0x23, 0x7F };
            Check(std::vector<uint8_t>(sent.begin(), sent.begin() + 10) == expected, "four points from byte 2, x then y");
            Check(channel.RequestAt(1)[1] == 0x20, "then read");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            channel.Reply({ 0xA9, 0x21, 0x01 });
            Check(Throws<ProtocolError>([&] { (void)Run(protocol.SetCurve(keychron::SmoothCurve(35))); }), "refused, it says so");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);
            Check(Throws<std::invalid_argument>(
                      [&] { (void)Run(protocol.SetCurve({ { 0, 0 }, { 9, 1 }, { 9, 2 }, { 35, 127 } })); }),
                  "a curve the board could never replace is not sent");
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestPresets();
    TestWritable();
    TestMoved();
    TestPlayed();
    TestWritten();

    return TestResult();
}
