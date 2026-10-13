// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The knobs' encoder map over scripted bytes (adapters/vial/NazgEncoderMap.h): read on either
// protocol, told apart from a firmware that keeps none, written with a read-back.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/vial/NazgEncoderMap.h"
#include "adapters/vial/NazgVialProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

using nazg::DecodeQmkKeycode;
using nazg::KnobTurns;
using nazg::Task;
using nazg::VialProtocol;

namespace
{
    constexpr nazg::QmkKeycodeVersion c_Version = nazg::c_LatestQmkKeycodeVersion;

    constexpr uint16_t c_VolumeUp   = 0x00A9;
    constexpr uint16_t c_VolumeDown = 0x00AA;
    constexpr uint16_t c_Mute       = 0x00A8;

    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the call did not complete synchronously");
        return task.TakeResult();
    }

    // VIA from protocol 10: a request per direction, counter-clockwise first, [layer, knob, cw].
    void TestViaRead()
    {
        std::printf("VIA: a call per direction\n");

        FakeDeviceChannel channel;
        VialProtocol      protocol(channel);

        // 2 layers x 1 knob: ccw, cw on layer 0, then on layer 1.
        channel.Reply({ 0x14, 0, 0, 0, 0x00, c_VolumeDown });
        channel.Reply({ 0x14, 0, 0, 1, 0x00, c_VolumeUp });
        channel.Reply({ 0x14, 1, 0, 0, 0x00, c_Mute });
        channel.Reply({ 0x14, 1, 0, 1, 0x00, 0x00 });

        const auto map = Run(nazg::ReadEncoderMap(protocol, false, 12, 2, 1, c_Version));

        Check(map.size() == 2 && map[0].size() == 1, "[layer][knob]");
        Check(map[0][0].counterClockwise == DecodeQmkKeycode(c_VolumeDown, c_Version) &&
                  map[0][0].clockwise == DecodeQmkKeycode(c_VolumeUp, c_Version),
              "layer 0's turns, decoded");
        Check(map[1][0].counterClockwise == DecodeQmkKeycode(c_Mute, c_Version), "layer 1's");
        Check(channel.RequestCount() == 4, "two requests per knob and layer");
        Check(channel.RequestAt(1)[0] == 0x14 && channel.RequestAt(1)[1] == 0 && channel.RequestAt(1)[2] == 0 &&
                  channel.RequestAt(1)[3] == 1,
              "0x14, layer, knob, clockwise");
    }

    // A firmware keeping no map: VIA before 10 is not asked; 0xFF on the first request; Vial's echo.
    void TestNoMap()
    {
        std::printf("no encoder map\n");

        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            Check(Run(nazg::ReadEncoderMap(protocol, false, 9, 4, 1, c_Version)).empty() &&
                      channel.RequestCount() == 0,
                  "VIA 9 has no encoder commands: nothing is sent");
        }
        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            channel.ReplyUnhandled();
            Check(Run(nazg::ReadEncoderMap(protocol, false, 12, 4, 1, c_Version)).empty() &&
                      channel.RequestCount() == 1,
                  "VIA answering 0xFF has none, and is asked once");
        }
        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            channel.Reply({ 0xFE, 0x03, 0, 0 });   // the request, echoed
            Check(Run(nazg::ReadEncoderMap(protocol, true, 9, 4, 1, c_Version)).empty(),
                  "Vial echoing the request has none");
        }
        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            Check(Run(nazg::ReadEncoderMap(protocol, true, 9, 4, 0, c_Version)).empty() &&
                      channel.RequestCount() == 0,
                  "no knob, nothing asked");
        }
    }

    // Vial: both directions in one reply, [layer, knob].
    void TestVialRead()
    {
        std::printf("Vial: both directions at once\n");

        FakeDeviceChannel channel;
        VialProtocol      protocol(channel);

        // 1 layer x 2 knobs.
        channel.Reply({ 0x00, c_VolumeDown, 0x00, c_VolumeUp });
        channel.Reply({ 0x00, c_Mute, 0x00, 0x00 });

        const auto map = Run(nazg::ReadEncoderMap(protocol, true, 9, 1, 2, c_Version));

        Check(map.size() == 1 && map[0].size() == 2, "one layer, two knobs");
        Check(map[0][0].clockwise == DecodeQmkKeycode(c_VolumeUp, c_Version) &&
                  map[0][1].counterClockwise == DecodeQmkKeycode(c_Mute, c_Version),
              "each knob's pair");
        Check(channel.RequestCount() == 2 && channel.RequestAt(1)[0] == 0xFE && channel.RequestAt(1)[1] == 0x03 &&
                  channel.RequestAt(1)[2] == 0 && channel.RequestAt(1)[3] == 1,
              "a request per knob: 0xFE 0x03, layer, knob");
    }

    // A turn written, then read back: what the board holds is what is returned.
    void TestWrite()
    {
        std::printf("write a turn\n");

        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            channel.Reply({ 0x15 });
            channel.Reply({ 0x14, 2, 1, 1, 0x00, c_VolumeUp });
            const nazg::Keycode stored = Run(nazg::WriteKnobTurn(protocol, false, 2, 1, true,
                                                                 DecodeQmkKeycode(c_VolumeUp, c_Version), c_Version));
            Check(stored == DecodeQmkKeycode(c_VolumeUp, c_Version), "VIA: what the board holds");
            const auto& set = channel.RequestAt(0);
            Check(set[0] == 0x15 && set[1] == 2 && set[2] == 1 && set[3] == 1 && set[4] == 0x00 && set[5] == c_VolumeUp,
                  "0x15, layer, knob, clockwise, keycode big-endian");
        }
        {
            FakeDeviceChannel channel;
            VialProtocol      protocol(channel);
            channel.Reply({ 0xFE, 0x04 });   // a set answers nothing: its request back
            channel.Reply({ 0x00, c_Mute, 0x00, c_VolumeUp });
            const nazg::Keycode stored = Run(nazg::WriteKnobTurn(protocol, true, 0, 0, false,
                                                                 DecodeQmkKeycode(c_Mute, c_Version), c_Version));
            Check(stored == DecodeQmkKeycode(c_Mute, c_Version), "Vial: the counter-clockwise half of the read-back");
            const auto& set = channel.RequestAt(0);
            Check(set[0] == 0xFE && set[1] == 0x04 && set[2] == 0 && set[3] == 0 && set[4] == 0 && set[6] == c_Mute,
                  "0xFE 0x04, layer, knob, clockwise, keycode big-endian");
        }
    }

    void TestKnobCount()
    {
        std::printf("knob count\n");

        nazg::KeyboardDefinition definition;
        Check(nazg::KnobCount(definition) == 0, "none drawn, none counted");

        nazg::DefinitionKey key;
        definition.keys.push_back(key);
        key.encoderIndex = 2;
        definition.keys.push_back(key);
        key.encoderIndex = 0;
        definition.keys.push_back(key);
        Check(nazg::KnobCount(definition) == 3, "one past the highest index");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestViaRead();
    TestNoMap();
    TestVialRead();
    TestWrite();
    TestKnobCount();

    return TestResult();
}
