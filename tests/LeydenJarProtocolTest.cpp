// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Leyden Jar's commands: the probe that finds one among VIA and Vial boards, the
// request's layout -- VIA's get or set, the value id, the magic -- every reply's offsets and
// little-endian scalars, what ReadControllerInfo() asks per protocol version, and the commands
// sent with no reply awaited.
//
// Byte layouts from keyboards/leyden_jar/util_protocol.c in Rico's vial-qmk fork, and the
// Leyden Jar Diagnostic Tool's LeydenJarProtocol.cpp, which speaks it.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/leydenjar/NazgLeydenJarProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

namespace leydenjar = nazg::leydenjar;

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

    void Run(Task<void> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");

        task.TakeResult();
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

    constexpr uint8_t c_Get = 0x02;
    constexpr uint8_t c_Set = 0x03;

    // The firmware's answer: the request's four leading bytes, then `payload` from byte 4.
    std::vector<uint8_t> Answer(uint8_t command, leydenjar::Value value, std::vector<uint8_t> payload = {})
    {
        std::vector<uint8_t> reply = { command, static_cast<uint8_t>(value), 0xC0, 0x21 };
        reply.insert(reply.end(), payload.begin(), payload.end());
        return reply;
    }

    bool IsRequest(const std::vector<uint8_t>& request, uint8_t command, leydenjar::Value value)
    {
        return request.size() == via::c_ReportSize && request[0] == command &&
               request[1] == static_cast<uint8_t>(value) && request[2] == 0xC0 && request[3] == 0x21;
    }

    void TestProbe()
    {
        std::printf("the probe\n");

        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            // 0.9.1: major, mid, then the minor little-endian.
            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0x00, 0x09, 0x01, 0x00 }));

            const auto version = Run(protocol.Probe());
            Check(version.has_value(), "a Leyden Jar answers");
            Check(version && *version == leydenjar::Version{ 0, 9, 1 }, "its version is 0.9.1");
            Check(IsRequest(channel.RequestAt(0), c_Get, leydenjar::Value::ProtocolVersion),
                  "asked with VIA's get keyboard value, 0x81 and the magic C0 21");
        }
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0x01, 0x00, 0x02, 0x01 }));
            const auto version = Run(protocol.Probe());
            Check(version && *version == leydenjar::Version{ 1, 0, 0x0102 }, "the minor is little-endian");
        }
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.ReplyUnhandled();
            Check(!Run(protocol.Probe()).has_value(), "a VIA or Vial board refuses: no Leyden Jar");
        }
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.ReplyEcho();
            Check(!Run(protocol.Probe()).has_value(), "a firmware echoing the request reads 0.0.0: no Leyden Jar");
        }
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            // A report nobody asked for -- Keychron's layer report -- then the answer.
            channel.Reply({ 0xA3, 0x00, 0x03 });
            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0x00, 0x09, 0x01, 0x00 }));
            Check(Run(protocol.Probe()).has_value() && channel.DroppedCount() == 1,
                  "a report that is not the reply is read past");
        }
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            // Another value's answer with the same command is not the reply: the magic or the
            // value id differ.
            channel.Reply({ c_Get, 0x81, 0x00, 0x00, 0x00, 0x09 });
            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0x00, 0x09, 0x01, 0x00 }));
            Check(Run(protocol.Probe()).has_value() && channel.DroppedCount() == 1,
                  "a reply without the magic is not the answer");
        }
    }

    void TestDetailsAndMapping()
    {
        std::printf("details and the matrix mapping\n");

        FakeDeviceChannel   channel;
        leydenjar::Protocol protocol(channel);

        // 8 x 18 logical, 8 x 18 controller, Beam Spring, 4 bins.
        channel.Reply(Answer(c_Get, leydenjar::Value::Details, { 8, 18, 8, 18, 1, 4 }));
        const leydenjar::Details details = Run(protocol.GetDetails());
        Check(details.logicalRows == 8 && details.logicalColumns == 18, "the logical matrix");
        Check(details.controllerRows == 8 && details.controllerColumns == 18, "the controller's");
        Check(details.technology == leydenjar::SwitchTechnology::BeamSpring, "a Beam Spring");
        Check(details.bins == 4, "four bins");

        // XWhatsit wiring, the 18 columns, then the 8 rows.
        std::vector<uint8_t> payload = { 1 };
        for (uint8_t column = 0; column < 18; ++column)
            payload.push_back(static_cast<uint8_t>(17 - column));
        for (uint8_t row = 0; row < 8; ++row)
            payload.push_back(static_cast<uint8_t>((row + 1) % 8));
        channel.Reply(Answer(c_Get, leydenjar::Value::MatrixMapping, payload));

        const leydenjar::MatrixMapping mapping = Run(protocol.GetMatrixMapping(details));
        Check(mapping.wiring == static_cast<uint8_t>(leydenjar::MatrixWiring::XWhatsit), "XWhatsit's wiring");
        Check(mapping.columns.size() == 18 && mapping.columns[0] == 17 && mapping.columns[17] == 0,
              "the columns come first");
        Check(mapping.rows.size() == 8 && mapping.rows[0] == 1 && mapping.rows[7] == 0, "then the rows");

        leydenjar::Details tooBig = details;
        tooBig.controllerColumns  = 24;
        Check(Throws<std::invalid_argument>([&] { Run(protocol.GetMatrixMapping(tooBig)); }),
              "a mapping that cannot fit in a report is refused before it is asked");
    }

    void TestReadings()
    {
        std::printf("levels, scans, bins, thresholds\n");

        FakeDeviceChannel   channel;
        leydenjar::Protocol protocol(channel);

        channel.ReplyEcho();
        Run(protocol.DetectLevels());
        Check(IsRequest(channel.RequestAt(0), c_Set, leydenjar::Value::DetectLevels), "levels measured by a set");

        // Column 5 echoed, then three levels little-endian.
        channel.Reply(Answer(c_Get, leydenjar::Value::ColumnLevels, { 5, 0, 0x34, 0x12, 0xFF, 0x00, 0x00, 0x02 }));
        const std::vector<uint16_t> levels = Run(protocol.GetColumnLevels(5, 3));
        Check(levels == std::vector<uint16_t>{ 0x1234, 0x00FF, 0x0200 }, "levels follow the column, little-endian");
        Check(channel.RequestAt(1)[4] == 5 && channel.RequestAt(1)[5] == 0, "the column asked for in two bytes");
        Check(Throws<std::invalid_argument>([&] { Run(protocol.GetColumnLevels(0, 14)); }),
              "14 levels do not fit after the column");

        channel.Reply(Answer(c_Get, leydenjar::Value::BinMap, { 2, 0, 0, 1, 255 }));
        Check(Run(protocol.GetColumnBins(2, 3)) == std::vector<uint8_t>{ 0, 1, 255 }, "bins follow the column");

        // The row's index, two bytes unused, then the row: columns 0, 9 and 17.
        channel.Reply(Answer(c_Get, leydenjar::Value::LogicalMatrixRow, { 3, 0, 0, 0, 0x01, 0x02, 0x02, 0x00 }));
        Check(Run(protocol.GetLogicalMatrixRow(3)) == 0x00020201u, "a logical row: 32 bits from byte 8");

        channel.Reply(Answer(c_Get, leydenjar::Value::ControllerMatrix, { 0x00, 0x81, 0x00 }));
        Check(Run(protocol.GetControllerMatrix(3)) == std::vector<uint8_t>{ 0x00, 0x81, 0x00 },
              "the controller's matrix: a byte per column");

        channel.Reply(Answer(c_Get, leydenjar::Value::DacThreshold, { 1, 0, 0x90, 0x01 }));
        Check(Run(protocol.GetDacThreshold(1)) == 400, "a bin's threshold after the bin");

        channel.Reply(Answer(c_Get, leydenjar::Value::IsLeftHalf, { 0 }));
        Check(!Run(protocol.IsLeftHalf()), "the right half");
        channel.Reply(Answer(c_Get, leydenjar::Value::IsLeftHalf, { 2 }));
        Check(Throws<ProtocolError>([&] { Run(protocol.IsLeftHalf()); }), "a half that is neither is refused");

        channel.ReplyUnhandled();
        Check(Throws<ProtocolError>([&] { Run(protocol.GetDetails()); }), "a refusal throws");
    }

    void TestKeyOutput()
    {
        std::printf("key output\n");

        FakeDeviceChannel   channel;
        leydenjar::Protocol protocol(channel);

        channel.ReplyEcho();
        Run(protocol.SetKeyOutputEnabled(false));
        channel.ReplyEcho();
        Run(protocol.SetKeyOutputEnabled(true));

        Check(IsRequest(channel.RequestAt(0), c_Set, leydenjar::Value::KeyOutput) && channel.RequestAt(0)[4] == 0,
              "turned off with a set, 0 at byte 4");
        Check(channel.RequestAt(1)[4] == 1, "turned on with 1");

        channel.Reply(Answer(c_Get, leydenjar::Value::KeyOutput, { 1 }));
        Check(Run(protocol.IsKeyOutputEnabled()), "read back on");

        // Off until closed: on again is the channel's farewell, set before it goes off.
        FakeDeviceChannel   section;
        leydenjar::Protocol diagnostics(section);
        section.ReplyEcho();
        Run(diagnostics.TurnKeyOutputOffUntilClosed());
        Check(IsRequest(section.Farewell(), c_Set, leydenjar::Value::KeyOutput) && section.Farewell()[4] == 1,
              "the farewell turns key output on");
        Check(section.RequestCount() == 1 && section.RequestAt(0)[4] == 0, "then it is turned off");
    }

    // The section's readings: each one series, the board's time rather than a frame per round trip.
    void TestReadingSeries()
    {
        std::printf("readings as one series\n");

        FakeDeviceChannel   channel;
        leydenjar::Protocol protocol(channel);

        channel.ReplyEcho();
        channel.Reply(Answer(c_Get, leydenjar::Value::ColumnLevels, { 0, 0, 0x76, 0x01, 0x80, 0x01 }));
        channel.Reply(Answer(c_Get, leydenjar::Value::ColumnLevels, { 1, 0, 0x00, 0x00, 0xF3, 0x01 }));
        const std::vector<std::vector<uint16_t>> levels = Run(protocol.ReadLevels(2, 2));
        Check(channel.SeriesCount() == 1 && channel.RequestCount() == 3, "the sweep and two columns in one series");
        Check(IsRequest(channel.RequestAt(0), c_Set, leydenjar::Value::DetectLevels) && channel.RequestAt(2)[4] == 1,
              "the sweep first, then each column asked for");
        Check(levels == std::vector<std::vector<uint16_t>>{ { 374, 384 }, { 0, 499 } },
              "levels by controller column, then row");

        channel.ReplyEcho();
        channel.Reply(Answer(c_Get, leydenjar::Value::LogicalMatrixRow, { 0, 0, 0, 0, 0x04, 0, 0, 0 }));
        channel.Reply(Answer(c_Get, leydenjar::Value::LogicalMatrixRow, { 1, 0, 0, 0, 0, 0, 0, 0 }));
        Check(Run(protocol.ReadLogicalMatrix(2)) == std::vector<uint32_t>{ 4, 0 } && channel.SeriesCount() == 2,
              "a logical scan and its rows in one series");

        channel.ReplyEcho();
        channel.Reply(Answer(c_Get, leydenjar::Value::ControllerMatrix, { 0xFF, 0xFE }));
        Check(Run(protocol.ReadControllerMatrix(2)) == std::vector<uint8_t>{ 0xFF, 0xFE } && channel.SeriesCount() == 3,
              "a controller scan and its read in one series");

        channel.ReplyEcho();
        channel.ReplyUnhandled();
        Check(Throws<ProtocolError>([&] { Run(protocol.ReadLevels(1, 2)); }), "a refusal in a series throws");
    }

    void TestUnansweredCommands()
    {
        std::printf("bootloader and EEPROM erase\n");

        FakeDeviceChannel   channel;
        leydenjar::Protocol protocol(channel);

        Run(protocol.EnterBootloader());
        Run(protocol.EraseEeprom());

        Check(channel.SentCount() == 2, "both sent with no reply awaited");
        Check(IsRequest(channel.RequestAt(0), c_Set, leydenjar::Value::EnterBootloader), "the bootloader: 0x89");
        Check(IsRequest(channel.RequestAt(1), c_Set, leydenjar::Value::EraseEeprom), "the erase: 0x8B");
    }

    void TestControllerInfo()
    {
        std::printf("the controller's information, per version\n");

        // 0.9.1, a whole board: 2 bins, a 3 x 2 controller matrix.
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0, 9, 1, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::Details, { 3, 2, 3, 2, 0, 2 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::MatrixMapping, { 0, 0, 1, 0, 1, 2 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacThreshold, { 0, 0, 100, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacReferenceLevel, { 0, 0, 50, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacThreshold, { 1, 0, 110, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacReferenceLevel, { 1, 0, 55, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::BinMap, { 0, 0, 0, 0, 1 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::BinMap, { 1, 0, 1, 1, 255 }));

            const leydenjar::ControllerInfo info = Run(leydenjar::ReadControllerInfo(protocol));
            Check(info.thresholds == std::vector<uint16_t>{ 100, 110 }, "a threshold per bin");
            Check(info.referenceLevels == std::vector<uint16_t>{ 50, 55 }, "a reference level per bin");
            Check(info.bins.size() == 2 && info.bins[0] == std::vector<uint8_t>{ 0, 0, 1 } &&
                      info.bins[1] == std::vector<uint8_t>{ 1, 1, 255 },
                  "the bins of every controller column");
            Check(info.isLeftHalf, "a whole board counts as the left half");
            Check(channel.RequestCount() == 9, "no half asked before 1.0.0");
            Check(channel.RequestAt(5)[1] == static_cast<uint8_t>(leydenjar::Value::DacThreshold) &&
                      channel.RequestAt(5)[4] == 1,
                  "thresholds and levels asked bin by bin");
        }

        // 1.0.0, a split board's right half: one bin, a 1 x 1 matrix.
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 1, 0, 0, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::Details, { 2, 1, 1, 1, 0, 1 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::MatrixMapping, { 0, 0, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacThreshold, { 0, 0, 100, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacReferenceLevel, { 0, 0, 50, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::BinMap, { 0, 0, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::IsLeftHalf, { 0 }));

            const leydenjar::ControllerInfo info = Run(leydenjar::ReadControllerInfo(protocol));
            Check(!info.isLeftHalf, "1.0.0 says which half answers");
        }

        // Before 0.9.1: the threshold of bin 0, and nothing per bin.
        {
            FakeDeviceChannel   channel;
            leydenjar::Protocol protocol(channel);

            channel.Reply(Answer(c_Get, leydenjar::Value::ProtocolVersion, { 0, 9, 0, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::Details, { 2, 2, 2, 2, 0, 0 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::MatrixMapping, { 0, 0, 1, 0, 1 }));
            channel.Reply(Answer(c_Get, leydenjar::Value::DacThreshold, { 0, 0, 0x2C, 0x01 }));

            const leydenjar::ControllerInfo info = Run(leydenjar::ReadControllerInfo(protocol));
            Check(info.thresholds == std::vector<uint16_t>{ 300 } && info.referenceLevels.empty(),
                  "one threshold, no reference levels");
            Check(info.bins.size() == 2 && info.bins[1] == std::vector<uint8_t>{ 0, 0 }, "every key in bin 0");
            Check(channel.RequestCount() == 4, "nothing else asked");
        }
    }

    // A controller wired to no matrix: every threshold at the firmware's sentinel.
    void TestWiredToNoMatrix()
    {
        std::printf("wired to no matrix\n");

        leydenjar::ControllerInfo info;
        info.details.technology = leydenjar::SwitchTechnology::BeamSpring;
        info.thresholds         = { 300 };
        Check(leydenjar::IsWiredToNoMatrix(info), "the bare B104: 300 on a Beam Spring");

        info.details.technology = leydenjar::SwitchTechnology::ModelF;
        Check(!leydenjar::IsWiredToNoMatrix(info), "300 is a threshold on a Model F");
        info.thresholds = { 1023, 1023, 1023 };
        Check(leydenjar::IsWiredToNoMatrix(info), "1023 in every bin on a Model F");
        info.thresholds = { 384, 1023, 397 };
        Check(!leydenjar::IsWiredToNoMatrix(info), "a board with one bin unused is wired");
        info.thresholds.clear();
        Check(!leydenjar::IsWiredToNoMatrix(info), "nothing read, nothing said");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestProbe();
    TestDetailsAndMapping();
    TestReadings();
    TestKeyOutput();
    TestReadingSeries();
    TestUnansweredCommands();
    TestControllerInfo();
    TestWiredToNoMatrix();

    return TestResult();
}
