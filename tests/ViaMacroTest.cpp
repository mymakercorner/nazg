// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the macro buffer's formats (adapters/via/NazgViaMacro.h) against the bytes each firmware
// plays -- docs/research_material/via-vial-commands.md, "Macros -- the buffer and its byte format",
// whose examples are the ones of ui-design/macro-editors-vial-via.html -- and the protocol's read,
// which stops at the last macro, and write, inside the firmware's guard.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaMacro.h"
#include "adapters/via/NazgViaProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace via = nazg::via;

using Kind = via::MacroAction::Kind;

namespace
{
    template <typename T>
    T Run(nazg::Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");
        return task.TakeResult();
    }

    void Run(nazg::Task<void> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");
        task.TakeResult();
    }

    via::MacroActions Text(const std::string& text)
    {
        via::MacroActions actions;
        for (char c : text)
            actions.push_back({ Kind::Character, static_cast<uint8_t>(c) });
        return actions;
    }

    via::MacroActions Join(std::initializer_list<via::MacroActions> parts)
    {
        via::MacroActions all;
        for (const via::MacroActions& part : parts)
            all.insert(all.end(), part.begin(), part.end());
        return all;
    }

    // The examples: M0 "Best regards," Enter "Rico"; M1 Win+R, 300 ms, "cmd", Enter -- as Vial
    // stores Win+R, press, tap, release; M2 Alt held over two Tabs.
    const via::MacroActions c_Signature = Join({ Text("Best regards,"), { { Kind::Tap, 0x28 } }, Text("Rico") });
    const via::MacroActions c_RunCmd    = Join({ { { Kind::Press, 0xE3 }, { Kind::Tap, 0x15 }, { Kind::Release, 0xE3 },
                                             { Kind::Wait, 300 } },
                                           Text("cmd"), { { Kind::Tap, 0x28 } } });
    const via::MacroActions c_AltTab    = { { Kind::Press, 0xE2 }, { Kind::Tap, 0x2B }, { Kind::Tap, 0x2B }, { Kind::Release, 0xE2 } };

    void TestFormatOfBoard()
    {
        std::printf("format of a board\n");

        nazg::BoardReport report;
        report.isVial       = true;
        report.viaProtocol  = 9;
        report.vialProtocol = 6;
        Check(via::MacroFormatFor(report) == via::MacroFormat::VialExtended, "Vial 6: extended, whatever VIA it reports");
        report.vialProtocol = 4;
        Check(via::MacroFormatFor(report) == via::MacroFormat::VialBasic, "Vial 4: basic");
        report.vialProtocol = 1;
        Check(via::MacroFormatFor(report) == via::MacroFormat::Unprefixed, "Vial 1: unprefixed");

        report.isVial      = false;
        report.viaProtocol = 12;
        Check(via::MacroFormatFor(report) == via::MacroFormat::Via, "VIA 12: prefixed");
        report.viaProtocol = 10;
        Check(via::MacroFormatFor(report) == via::MacroFormat::Unprefixed, "VIA 10: unprefixed");

        Check(!via::HasWaits(via::MacroFormat::Unprefixed) && via::HasWaits(via::MacroFormat::Via), "waits from VIA 11");
        Check(via::LongestWait(via::MacroFormat::Via) == 9999 && via::LongestWait(via::MacroFormat::VialBasic) == 65024,
              "VIA waits to 9 999 ms, Vial to 65 024");
    }

    void TestVialBytes()
    {
        std::printf("Vial's bytes\n");

        const std::vector<uint8_t> runCmd = { 0x01, 0x02, 0xE3, 0x01, 0x01, 0x15, 0x01, 0x03, 0xE3, 0x01, 0x04, 0x2E, 0x02,
                                              'c', 'm', 'd', 0x01, 0x01, 0x28, 0x00 };
        Check(via::EncodeMacros({ c_RunCmd }, via::MacroFormat::VialExtended) == runCmd,
              "Win+R, 300 ms, cmd, Enter: 300 ms is 2E 02");
        Check(via::DecodeMacros(runCmd, 1, via::MacroFormat::VialExtended) == std::vector<via::MacroActions>{ c_RunCmd },
              "and reads back the same");

        // Ctrl+Z twice, one 16-bit tap each: 01 05 1D 01.
        const via::MacroActions undo = { { Kind::Tap, 0x011D }, { Kind::Tap, 0x011D } };
        const std::vector<uint8_t> undoBytes = { 0x01, 0x05, 0x1D, 0x01, 0x01, 0x05, 0x1D, 0x01, 0x00 };
        Check(via::EncodeMacros({ undo }, via::MacroFormat::VialExtended) == undoBytes, "LCTL(KC_Z): 01 05 1D 01");
        Check(via::DecodeMacros(undoBytes, 1, via::MacroFormat::VialExtended) == std::vector<via::MacroActions>{ undo },
              "read back");

        // A keycode whose low byte is 0 travels as FF and its high byte -- MO(1) is 0x5221, so
        // take 0x5200, TO(0): bytes 52 FF.
        const via::MacroActions to0 = { { Kind::Press, 0x5200 } };
        const std::vector<uint8_t> to0Bytes = { 0x01, 0x06, 0x52, 0xFF, 0x00 };
        Check(via::EncodeMacros({ to0 }, via::MacroFormat::VialExtended) == to0Bytes, "0x5200 as 52 FF: no 0 byte");
        Check(via::DecodeMacros(to0Bytes, 1, via::MacroFormat::VialExtended) == std::vector<via::MacroActions>{ to0 },
              "decoded back to 0x5200");

        std::string error;
        Check(via::EncodeMacros({ undo }, via::MacroFormat::VialBasic, &error).empty() && !error.empty(),
              "Vial 2-4 refuses a 16-bit key, saying why");

        const via::MacroActions longest = { { Kind::Wait, 65024 } };
        const std::vector<uint8_t> longestBytes = via::EncodeMacros({ longest }, via::MacroFormat::VialBasic);
        Check(longestBytes == std::vector<uint8_t>{ 0x01, 0x04, 0xFF, 0xFF, 0x00 }, "65 024 ms is FF FF");
        Check(via::EncodeMacros({ { { Kind::Wait, 65025 } } }, via::MacroFormat::VialBasic).empty(), "65 025 is refused");
    }

    void TestViaBytes()
    {
        std::printf("VIA's bytes\n");

        // VIA writes Win+R as a chord, press press release release; its wait in digits and '|'.
        const via::MacroActions chord = { { Kind::Press, 0xE3 }, { Kind::Press, 0x15 }, { Kind::Release, 0x15 },
                                     { Kind::Release, 0xE3 }, { Kind::Wait, 300 } };
        const std::vector<uint8_t> bytes = { 0x01, 0x02, 0xE3, 0x01, 0x02, 0x15, 0x01, 0x03, 0x15, 0x01, 0x03, 0xE3,
                                             0x01, 0x04, '3', '0', '0', '|', 0x00 };
        Check(via::EncodeMacros({ chord }, via::MacroFormat::Via) == bytes, "300 ms as '3' '0' '0' '|'");
        Check(via::DecodeMacros(bytes, 1, via::MacroFormat::Via) == std::vector<via::MacroActions>{ chord }, "read back");

        Check(via::EncodeMacros({ { { Kind::Tap, 0x011D } } }, via::MacroFormat::Via).empty(), "VIA keys are 8-bit");
        Check(via::EncodeMacros({ { { Kind::Wait, 10000 } } }, via::MacroFormat::Via).empty(),
              "10 000 ms is refused: QMK of protocol 12 stops at a fifth digit");

        // The same bytes read as Vial's: the digits are taken for a two-byte wait. Formats are
        // never guessed.
        const std::vector<via::MacroActions> misread = via::DecodeMacros(bytes, 1, via::MacroFormat::VialBasic);
        Check(misread.size() == 1 && misread[0] != chord, "VIA's wait read as Vial's is wrong -- the format is the board's");

        // VIA 10 and Vial 0-1: the codes without their prefix, and no wait at all.
        const std::vector<uint8_t> old = { 0x02, 0xE2, 0x01, 0x2B, 0x01, 0x2B, 0x03, 0xE2, 0x00 };
        Check(via::EncodeMacros({ c_AltTab }, via::MacroFormat::Unprefixed) == old, "unprefixed: 02 E2 01 2B 01 2B 03 E2");
        Check(via::DecodeMacros(old, 1, via::MacroFormat::Unprefixed) == std::vector<via::MacroActions>{ c_AltTab }, "read back");
        Check(via::EncodeMacros({ c_RunCmd }, via::MacroFormat::Unprefixed).empty(), "no wait before VIA 11");
    }

    void TestBuffer()
    {
        std::printf("the buffer\n");

        const std::vector<via::MacroActions> macros = { c_Signature, c_RunCmd, c_AltTab, {}, {} };
        const std::vector<uint8_t> bytes = via::EncodeMacros(macros, via::MacroFormat::VialExtended);
        Check(bytes.size() == 21 + 20 + 13 + 1 + 1, "M0 21 bytes, M1 20, M2 13, two empty: 1 each");
        Check(via::DecodeMacros(bytes, 5, via::MacroFormat::VialExtended) == macros, "five macros read back");

        Check(via::MacrosEnd(bytes, 5) == bytes.size(), "the fifth 0 ends them");
        Check(via::MacrosEnd(bytes, 2) == size_t{ 41 }, "the second ends at byte 41");
        Check(!via::MacrosEnd({ 'a', 0 }, 3).has_value(), "too few 0s: not ended");

        // A buffer that ends early gives empty macros; anything past the count is not read.
        std::vector<uint8_t> trailing = bytes;
        trailing.insert(trailing.end(), { 'x', 'y', 0 });
        Check(via::DecodeMacros(trailing, 5, via::MacroFormat::VialExtended) == macros, "bytes past the last macro ignored");
        const std::vector<via::MacroActions> padded = via::DecodeMacros({ 'a', 0 }, 3, via::MacroFormat::Via);
        Check(padded.size() == 3 && padded[1].empty() && padded[2].empty(), "missing macros are empty");

        Check(via::ActionSize({ Kind::Character, 0x01 }, via::MacroFormat::Via) == std::nullopt,
              "character 0x01 is the prefix, not a character");
        Check(via::ActionSize({ Kind::Character, 0xE9 }, via::MacroFormat::Via) == std::nullopt,
              "no character above 0x7F: the firmware's table has 128");
        Check(via::ActionSize({ Kind::Wait, 300 }, via::MacroFormat::Via) == size_t{ 6 }, "VIA's 300 ms: 6 bytes");
    }

    void TestRead()
    {
        std::printf("reading stops at the last macro\n");

        // Three macros in a big buffer: "ab", "", "c" -- six bytes, one read of 28.
        FakeDeviceChannel channel;
        std::vector<uint8_t> reply = { 0x0E, 0x00, 0x00, 28, 'a', 'b', 0, 0, 'c', 0, 'z', 'z' };
        channel.Reply(reply);

        via::Protocol via(channel);
        const std::vector<uint8_t> bytes = Run(via.ReadMacros(3, 15000));
        Check(channel.RequestCount() == 1, "one request for a 15 000-byte buffer");
        Check(bytes == std::vector<uint8_t>{ 'a', 'b', 0, 0, 'c', 0 }, "up to the third 0, nothing after");

        // Macros that run past the first chunk ask for the next.
        FakeDeviceChannel two;
        std::vector<uint8_t> first = { 0x0E, 0x00, 0x00, 28 };
        first.resize(4 + 28, 'x');
        two.Reply(first);
        two.Reply({ 0x0E, 0x00, 28, 28, 'y', 0 });
        via::Protocol via2(two);
        const std::vector<uint8_t> long_ = Run(via2.ReadMacros(1, 15000));
        Check(two.RequestCount() == 2 && long_.size() == 30, "a 30-byte macro: two reads");
        Check(two.RequestAt(1)[1] == 0 && two.RequestAt(1)[2] == 28, "the second from offset 28");
    }

    void TestWrite()
    {
        std::printf("writing inside the guard\n");

        // The stored buffer "ab" "cd"; the new one "ab" "xyz": from byte 3 on.
        const std::vector<uint8_t> stored = { 'a', 'b', 0, 'c', 'd', 0 };
        const std::vector<uint8_t> wanted = { 'a', 'b', 0, 'x', 'y', 'z', 0 };

        FakeDeviceChannel channel;
        for (int i = 0; i < 3; ++i)
            channel.ReplyEcho();
        via::Protocol via(channel);
        Run(via.WriteMacros(stored, wanted, 1000));

        Check(channel.RequestCount() == 3, "guard, data, guard");
        const std::vector<uint8_t>& guard = channel.RequestAt(0);
        Check(guard[0] == 0x0F && guard[1] == 0x03 && guard[2] == 0xE7 && guard[3] == 1 && guard[4] == 0xFF,
              "first the last byte, 999, set to FF");
        const std::vector<uint8_t>& data = channel.RequestAt(1);
        Check(data[1] == 0 && data[2] == 3 && data[3] == 4 && data[4] == 'x' && data[7] == 0,
              "then from offset 3: x y z 0");
        const std::vector<uint8_t>& done = channel.RequestAt(2);
        Check(done[1] == 0x03 && done[2] == 0xE7 && done[4] == 0x00, "then the last byte back to 0");

        FakeDeviceChannel same;
        via::Protocol via2(same);
        Run(via2.WriteMacros(stored, stored, 1000));
        Check(same.RequestCount() == 0, "nothing changed: nothing written");

        FakeDeviceChannel big;
        for (int i = 0; i < 5; ++i)
            big.ReplyEcho();
        via::Protocol via3(big);
        Run(via3.WriteMacros({}, std::vector<uint8_t>(40, 'q'), 1000));
        Check(big.RequestCount() == 4 && big.RequestAt(1)[3] == 28 && big.RequestAt(2)[3] == 12,
              "40 bytes: 28, then 12");

        bool refused = false;
        try
        {
            FakeDeviceChannel small;
            via::Protocol via4(small);
            Run(via4.WriteMacros({}, std::vector<uint8_t>(20, 'q'), 10));
        }
        catch (const nazg::ProtocolError&)
        {
            refused = true;
        }
        Check(refused, "more bytes than the buffer holds: refused before writing");
    }
}

int main()
{
    ConfigureCrtReporting();
    TestFormatOfBoard();
    TestVialBytes();
    TestViaBytes();
    TestBuffer();
    TestRead();
    TestWrite();
    return TestResult();
}
