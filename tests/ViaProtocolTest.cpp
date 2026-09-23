// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the VIA half of the adapter against a scripted channel: the exact bytes that go
// out, the parsing of what comes back, and the 0xFF failure path that is the protocol's
// entire error model.
//
// Byte layouts come from docs/research_material/via-vial-commands.md, which was written
// by reading QMK's via.c -- so a change to that table should break something here.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

using nazg::ProtocolError;
using nazg::Task;
using nazg::ViaCommand;
using nazg::ViaKeyboardValue;
using nazg::ViaProtocol;

namespace
{
    // Every call here completes without suspending, because the fake channel answers
    // immediately and Task<T> starts eagerly. If that ever stops being true the check
    // below turns it into a test failure rather than a mystery.
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");

        return task.TakeResult();
    }

    template <typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const ProtocolError&)
        {
            return true;
        }
        return false;
    }

    void TestProtocolVersion()
    {
        std::printf("protocol version\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x01, 0x00, 0x0C });

        const uint16_t version = Run(via.GetProtocolVersion());

        Check(version == 0x000C, "the version is parsed big-endian");
        Check(channel.RequestCount() == 1, "one round trip");
        Check(channel.RequestAt(0).size() == nazg::c_ViaReportSize, "the request is a full 32-byte report");
        Check(channel.RequestAt(0)[0] == 0x01, "the command id leads the report");
        Check(channel.RequestAt(0)[31] == 0x00, "the unused tail is zero-padded");
    }

    // 0xFF in byte 0 is the only failure the wire can express. A command compiled out
    // of the firmware answers exactly like one that does not exist.
    void TestUnhandledCommandThrows()
    {
        std::printf("unhandled command\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.ReplyUnhandled();

        Check(Throws([&] { Run(via.GetProtocolVersion()); }), "0xFF becomes a ProtocolError");
    }

    void TestMismatchedReplyThrows()
    {
        std::printf("mismatched reply\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        // Answering a different command than the one asked means the request and reply
        // streams have drifted apart -- worse than a refusal, and worth its own message.
        channel.Reply({ 0x04, 0x00, 0x0C });

        Check(Throws([&] { Run(via.GetProtocolVersion()); }), "a reply for another command is rejected");
    }

    void TestShortReplyThrows()
    {
        std::printf("short reply\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.ReplyRaw({ 0x01, 0x00 });

        Check(Throws([&] { Run(via.GetProtocolVersion()); }), "a truncated report is rejected before parsing");
    }

    void TestKeyboardValue()
    {
        std::printf("keyboard value\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x02, 0x04, 0x00, 0x01, 0x02, 0x03 });

        const uint32_t value = Run(via.GetKeyboardValue(ViaKeyboardValue::FirmwareVersion));

        Check(value == 0x00010203u, "the 32-bit value is parsed big-endian from byte 2");
        Check(channel.RequestAt(0)[1] == 0x04, "the value id is the first argument");
    }

    void TestKeycodeRoundTrip()
    {
        std::printf("keycode get and set\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x04, 1, 2, 3, 0x7C, 0x0A });

        const uint16_t keycode = Run(via.GetKeycode(1, 2, 3));

        Check(keycode == 0x7C0A, "the keycode is parsed big-endian from byte 4");

        const std::vector<uint8_t>& request = channel.RequestAt(0);
        Check(request[1] == 1 && request[2] == 2 && request[3] == 3, "layer, row and column are sent in order");

        channel.Reply({ 0x05 });
        Run(via.SetKeycode(4, 5, 6, 0x1234));

        const std::vector<uint8_t>& write = channel.RequestAt(1);
        Check(write[0] == 0x05, "set uses command 0x05");
        Check(write[1] == 4 && write[2] == 5 && write[3] == 6, "the position is sent in order");
        Check(write[4] == 0x12 && write[5] == 0x34, "the keycode is written big-endian");
    }

    void TestLayerCount()
    {
        std::printf("layer count\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x11, 4 });

        Check(Run(via.GetLayerCount()) == 4, "the count is byte 1");
    }

    // The batched read that exists because asking key by key was too chatty. Three
    // bytes of header after the command id leave 28 per round trip.
    void TestBufferChunking()
    {
        std::printf("buffer chunking\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        // Three chunks for 60 bytes: 28 + 28 + 4. Each reply is filled with its own
        // marker byte so the reassembly order is observable.
        for (uint8_t marker = 1; marker <= 3; ++marker)
        {
            std::vector<uint8_t> reply(4, 0x00);
            reply[0] = 0x12;
            reply.insert(reply.end(), 28, marker);
            channel.Reply(reply);
        }

        const std::vector<uint8_t> buffer = Run(via.GetKeymapBuffer(0, 60));

        Check(channel.RequestCount() == 3, "60 bytes took three round trips");
        Check(buffer.size() == 60, "exactly the requested length came back");

        Check(channel.RequestAt(0)[1] == 0x00 && channel.RequestAt(0)[2] == 0x00 &&
              channel.RequestAt(0)[3] == 28, "first chunk asks for 28 bytes at offset 0");
        Check(channel.RequestAt(1)[1] == 0x00 && channel.RequestAt(1)[2] == 28 &&
              channel.RequestAt(1)[3] == 28, "second chunk asks for 28 bytes at offset 28");
        Check(channel.RequestAt(2)[1] == 0x00 && channel.RequestAt(2)[2] == 56 &&
              channel.RequestAt(2)[3] == 4, "the last chunk asks only for what remains");

        Check(buffer[0] == 1 && buffer[27] == 1, "the first chunk lands first");
        Check(buffer[28] == 2 && buffer[55] == 2, "the second chunk follows it");
        Check(buffer[56] == 3 && buffer[59] == 3, "the tail chunk is truncated, not padded");
    }

    // Offsets are 16-bit and big-endian, so anything past 255 exercises the high byte.
    void TestBufferOffsetIsBigEndian()
    {
        std::printf("buffer offset encoding\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        std::vector<uint8_t> reply(4, 0x00);
        reply[0] = 0x12;
        reply.insert(reply.end(), 28, 0xAB);
        channel.Reply(reply);

        Run(via.GetKeymapBuffer(0x0140, 4));

        Check(channel.RequestAt(0)[1] == 0x01 && channel.RequestAt(0)[2] == 0x40,
              "the offset is split high byte first");
    }

    void TestMacroQueries()
    {
        std::printf("macro queries\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x0C, 16 });
        Check(Run(via.GetMacroCount()) == 16, "the macro count is byte 1");

        channel.Reply({ 0x0D, 0x03, 0xE8 });
        Check(Run(via.GetMacroBufferSize()) == 1000, "the buffer size is big-endian from byte 1");
    }

    // Probing is the only reliable capability test: protocol version numbers track
    // features in neither direction, so the adapter asks and watches for 0xFF.
    void TestSupportsProbe()
    {
        std::printf("capability probe\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.ReplyUnhandled();
        Check(!Run(via.Supports(ViaCommand::DynamicKeymapGetEncoder, { 0, 0, 0 })),
              "0xFF reports the command as absent");

        channel.Reply({ 0x14, 0, 0, 0, 0x00, 0x2A });
        Check(Run(via.Supports(ViaCommand::DynamicKeymapGetEncoder, { 0, 0, 0 })),
              "an echoed command id reports it as present");

        Check(!Throws([&] { channel.ReplyUnhandled(); Run(via.Supports(ViaCommand::BootloaderJump)); }),
              "probing never throws, so it can be used to explore a device");
    }

    // Writing one key: encode, set, read back, decode. What comes back is what the board
    // stored, which is not always what was sent.
    void TestWriteKeycode()
    {
        std::printf("write one keycode\n");

        constexpr auto c_Version = nazg::QmkKeycodeVersion::V0_0_7;

        {
            FakeDeviceChannel channel;
            ViaProtocol       via(channel);

            channel.Reply({ 0x05 });                              // set: echoed
            channel.Reply({ 0x04, 2, 3, 4, 0x7C, 0x42 });         // get: HF_TOGG

            const nazg::Keycode stored =
                Run(nazg::WriteKeycode(via, 2, 3, 4, nazg::NamedKey{ "HF_TOGG" }, c_Version));

            Check(stored == nazg::Keycode{ nazg::NamedKey{ "HF_TOGG" } }, "the board stored the key sent");
            Check(channel.RequestCount() == 2, "one set and one read-back");

            const std::vector<uint8_t>& set = channel.RequestAt(0);
            Check(set[0] == 0x05 && set[1] == 2 && set[2] == 3 && set[3] == 4 && set[4] == 0x7C && set[5] == 0x42,
                  "the set carries layer, row, column and the value big-endian");

            const std::vector<uint8_t>& get = channel.RequestAt(1);
            Check(get[0] == 0x04 && get[1] == 2 && get[2] == 3 && get[3] == 4, "the read-back asks for the same cell");
        }

        {
            // Vial's keycode firewall, as a locked board does it: success on the set,
            // then 0x0000 in the cell.
            FakeDeviceChannel channel;
            ViaProtocol       via(channel);

            channel.Reply({ 0x05 });
            channel.Reply({ 0x04, 0, 0, 0, 0x00, 0x00 });

            const nazg::Keycode stored =
                Run(nazg::WriteKeycode(via, 0, 0, 0, nazg::NamedKey{ "QK_BOOT" }, c_Version));

            Check(stored == nazg::Keycode{ nazg::NamedKey{ "KC_NO" } },
                  "a firewalled QK_BOOT reads back as KC_NO -- the caller sees what really happened");
        }

        {
            FakeDeviceChannel channel;
            ViaProtocol       via(channel);

            bool refused = false;
            try
            {
                (void)Run(nazg::WriteKeycode(via, 0, 0, 0, nazg::LayerTapKey{ 16, "KC_A" }, c_Version));
            }
            catch (const std::invalid_argument&)
            {
                refused = true;
            }

            Check(refused, "a keycode this version cannot store is refused");
            Check(channel.RequestCount() == 0, "before anything reaches the board");
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestProtocolVersion();
    TestUnhandledCommandThrows();
    TestMismatchedReplyThrows();
    TestShortReplyThrows();
    TestKeyboardValue();
    TestKeycodeRoundTrip();
    TestLayerCount();
    TestBufferChunking();
    TestBufferOffsetIsBigEndian();
    TestMacroQueries();
    TestSupportsProbe();
    TestWriteKeycode();

    return TestResult();
}
