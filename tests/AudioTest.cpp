// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Audio section's wire (adapters/via/NazgViaAudio.h): VIA V3's channel 4 read, set and
// stored, a firmware without audio refusing, and device indication played and put back -- the
// exact bytes against a scripted channel.
//
// Byte layouts from QMK's quantum/via.c (via-vial-commands.md, "Audio").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaAudio.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

using nazg::AudioSetting;
using nazg::AudioValues;
using nazg::Task;
using nazg::ViaProtocol;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the call did not complete synchronously");
        return task.TakeResult();
    }

    void Run(Task<void> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the call did not complete synchronously");
        task.TakeResult();
    }

    bool Starts(const std::vector<uint8_t>& frame, std::vector<uint8_t> bytes)
    {
        if (frame.size() != nazg::c_ViaReportSize)
            return false;
        for (size_t at = 0; at < bytes.size(); ++at)
            if (frame[at] != bytes[at])
                return false;
        for (size_t at = bytes.size(); at < frame.size(); ++at)
            if (frame[at] != 0)
                return false;
        return true;
    }

    void TestRead()
    {
        std::printf("read\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x08, 4, 1, 1 });
        channel.Reply({ 0x08, 4, 2, 0 });
        const AudioValues read = Run(nazg::ReadAudio(via));
        Check(Starts(channel.RequestAt(0), { 0x08, 4, 1 }) && Starts(channel.RequestAt(1), { 0x08, 4, 2 }),
              "get: [0x08][channel 4][1 audio, 2 clicky]");
        Check(read.on && !read.clicky, "the value from byte 3");

        channel.ReplyUnhandled();
        bool refused = false;
        try
        {
            Run(nazg::ReadAudio(via));
        }
        catch (const nazg::ProtocolError&)
        {
            refused = true;
        }
        Check(refused, "a firmware built without audio answers 0xFF: ProtocolError");
    }

    void TestWrite()
    {
        std::printf("write\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x07 });
        channel.Reply({ 0x09 });
        Run(nazg::WriteAudio(via, AudioSetting::Clicky, true));
        Check(channel.Requests().size() == 2 && Starts(channel.RequestAt(0), { 0x07, 4, 2, 1 }),
              "set: [0x07][4][2 clicky][1]");
        Check(channel.Requests().size() == 2 && Starts(channel.RequestAt(1), { 0x09, 4 }), "then stored: [0x09][4]");

        channel.Reply({ 0x07 });
        channel.Reply({ 0x09 });
        Run(nazg::WriteAudio(via, AudioSetting::On, false));
        Check(channel.Requests().size() == 4 && Starts(channel.RequestAt(2), { 0x07, 4, 1, 0 }), "audio off: [0x07][4][1][0]");
    }

    void TestPlay()
    {
        std::printf("play\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x03 });
        channel.Reply({ 0x03 });
        Run(nazg::PlayIndication(via));
        Check(channel.Requests().size() == 2 && Starts(channel.RequestAt(0), { 0x03, 0x05 }) &&
                  Starts(channel.RequestAt(1), { 0x03, 0x05, 1 }),
              "device indication 0, which plays, then 1, which puts the lighting back -- the value in byte 2");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRead();
    TestWrite();
    TestPlay();

    return TestResult();
}
