// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Vial branch: detection (including correctly reporting "this is a plain VIA
// board"), the paged definition download, and the little-endian scalars that are the
// easiest thing to get backwards, since VIA's are big-endian on the same device.
//
// Byte layouts come from docs/research_material/via-vial-commands.md, read out of
// vial-qmk's vial.c.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/vial/NazgVialProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

using nazg::ProtocolError;
using nazg::Task;
using nazg::VialProtocol;

namespace
{
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

    void TestDetectOnVialBoard()
    {
        std::printf("detect on a Vial board\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // [0..3] protocol version little-endian, [4..11] keyboard UID, [12] VialRGB.
        channel.Reply({ 0x06, 0x00, 0x00, 0x00,
                        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x01 });

        const auto identity = Run(vial.Detect());

        Check(identity.has_value(), "a Vial board is detected");
        Check(identity->protocolVersion == 6, "the version is parsed little-endian");
        Check(identity->keyboardUid == 0x0807060504030201ull, "the UID is parsed little-endian");
        Check(identity->supportsVialRgb, "the VialRGB flag is byte 12");

        const std::vector<uint8_t>& request = channel.RequestAt(0);
        Check(request[0] == 0xFE, "the Vial prefix leads the report");
        Check(request[1] == 0x00, "the sub-command follows it");
    }

    // The case that matters for a mixed fleet: the same call on plain VIA firmware has
    // to come back "not Vial" rather than throwing or returning nonsense.
    void TestDetectOnViaBoard()
    {
        std::printf("detect on a plain VIA board\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.ReplyUnhandled();

        const auto identity = Run(vial.Detect());

        Check(!identity.has_value(), "a VIA board reports no Vial support");
    }

    void TestDefinitionDownload()
    {
        std::printf("definition download\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // 70 bytes is three pages: 32 + 32 + 6, with the last one truncated.
        channel.Reply({ 70, 0x00, 0x00, 0x00 });

        for (uint8_t marker = 1; marker <= 3; ++marker)
            channel.Reply(std::vector<uint8_t>(nazg::c_ViaReportSize, marker));

        const std::vector<uint8_t> definition = Run(vial.DownloadDefinition());

        Check(channel.RequestCount() == 4, "one size query plus three pages");
        Check(definition.size() == 70, "the download stops at the reported size");
        Check(definition[0] == 1 && definition[31] == 1, "page 0 lands first");
        Check(definition[32] == 2 && definition[63] == 2, "page 1 follows");
        Check(definition[64] == 3 && definition[69] == 3, "the last page is truncated to what remains");

        Check(channel.RequestAt(1)[2] == 0 && channel.RequestAt(1)[3] == 0, "page 0 requested");
        Check(channel.RequestAt(2)[2] == 1 && channel.RequestAt(2)[3] == 0,
              "the page index is little-endian, unlike VIA's big-endian offsets");
        Check(channel.RequestAt(3)[2] == 2 && channel.RequestAt(3)[3] == 0, "page 2 requested");
    }

    void TestImplausibleDefinitionSizeIsRefused()
    {
        std::printf("implausible definition size\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0xFF, 0xFF, 0xFF, 0x7F });   // ~2 GB

        Check(Throws([&] { Run(vial.DownloadDefinition()); }),
              "a nonsense size is refused instead of allocated");
        Check(channel.RequestCount() == 1, "no pages were requested");
    }

    // The capability query that replaces guessing from the version number -- alt repeat
    // key was added in 2025 without bumping the protocol past 6.
    void TestEntryCounts()
    {
        std::printf("dynamic entry counts\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0x00);
        reply[0] = 8;    // tap dance
        reply[1] = 16;   // combo
        reply[2] = 4;    // key override
        reply[3] = 0;    // alt repeat key: compiled out
        reply[31] = 0x03;
        channel.ReplyRaw(reply);

        const auto counts = Run(vial.GetEntryCounts());

        Check(counts.tapDance == 8 && counts.combo == 16, "the counts are read in order");
        Check(counts.keyOverride == 4, "key overrides counted");
        Check(counts.altRepeatKey == 0, "a zero count means the feature is compiled out");
        Check(counts.capsWord && counts.layerLock, "the feature bits are in the LAST byte");

        Check(channel.RequestAt(0)[1] == 0x0D && channel.RequestAt(0)[2] == 0x00,
              "the entry op is addressed by sub-command then operation");
    }

    void TestUnlockStatus()
    {
        std::printf("unlock status\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // The firmware fills the report with 0xFF, then writes status and combo over it.
        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0xFF);
        reply[0] = 0;    // locked
        reply[1] = 1;    // unlock in progress
        reply[2] = 3; reply[3] = 4;
        reply[4] = 5; reply[5] = 6;
        channel.ReplyRaw(reply);

        const auto status = Run(vial.GetUnlockStatus());

        Check(!status.unlocked, "the board reports itself locked");
        Check(status.inProgress, "an unlock is in progress");
        Check(status.combo.size() == 2, "the 0xFF fill terminates the combo list");
        Check(status.combo[0] == std::make_pair<uint8_t, uint8_t>(3, 4), "first key is (row, column)");
        Check(status.combo[1] == std::make_pair<uint8_t, uint8_t>(5, 6), "second key follows");
    }

    void TestEncoderReturnsBothDirections()
    {
        std::printf("encoder\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0x00, 0x2A, 0x00, 0x2B });

        const auto encoder = Run(vial.GetEncoder(1, 0));

        Check(encoder.counterClockwise == 0x002A, "counter-clockwise comes first");
        Check(encoder.clockwise == 0x002B, "clockwise follows, both big-endian");
        Check(channel.RequestCount() == 1, "Vial returns both directions in ONE round trip, unlike VIA");
    }

    // The point of deriving from ViaProtocol: the shared commands are not reimplemented.
    void TestInheritedViaCommands()
    {
        std::printf("inherited VIA commands\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0x01, 0x00, 0x09 });
        const uint16_t version = Run(vial.GetProtocolVersion());

        Check(version == 9, "Vial firmware reports VIA protocol 9 through the inherited command");

        channel.Reply({ 0x11, 4 });
        Check(Run(vial.GetLayerCount()) == 4, "layer count works unchanged on the Vial branch");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestDetectOnVialBoard();
    TestDetectOnViaBoard();
    TestDefinitionDownload();
    TestImplausibleDefinitionSizeIsRefused();
    TestEntryCounts();
    TestUnlockStatus();
    TestEncoderReturnsBothDirections();
    TestInheritedViaCommands();

    return TestResult();
}
