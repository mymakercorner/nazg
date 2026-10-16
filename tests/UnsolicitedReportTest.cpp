// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Reports nobody asked for, arriving between a request and its reply. A Keychron 2025q3 board
// sends [0xA3, default layer, highest layer] on every layer change (keychron-commands.md,
// "Unsolicited reports"); before the reply check, a layer key held during a load or the
// matrix live test was read as the reply and broke it.
//
// The VIA protocol hands the channel a check -- the first bytes of the request echoed, or 0xFF
// -- and the channel reads past what fails it. FakeDeviceChannel drops a failing report as
// HidTransport's worker does; the worker itself is not covered here, since the transport has
// no substitutable device layer (TransportRequestTest.cpp).
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaProtocol.h"
#include "adapters/vial/NazgVialProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <vector>

namespace via  = nazg::via;
namespace vial = nazg::vial;

using nazg::HidTransportError;
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

    // What a call ended in: its result, a timeout from the channel, or a protocol error.
    enum class Outcome
    {
        Answered,
        TimedOut,
        Refused,
    };

    template <typename TCall>
    Outcome OutcomeOf(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const HidTransportError&)
        {
            return Outcome::TimedOut;
        }
        catch (const ProtocolError&)
        {
            return Outcome::Refused;
        }
        return Outcome::Answered;
    }

    // Keychron's layer report: default layer 0, layer 1 now the highest.
    const std::vector<uint8_t> c_LayerReport = { 0xA3, 0x00, 0x01 };

    void TestLayerReportBeforeTheReply()
    {
        std::printf("a layer report before the reply\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply(c_LayerReport);
        channel.Reply({ 0x01, 0x00, 0x0C });

        uint16_t      version = 0;
        const Outcome outcome = OutcomeOf([&] { version = Run(via.GetProtocolVersion()); });

        Check(outcome == Outcome::Answered, "the request is answered");
        Check(version == 0x000C, "by the reply, not by the layer report");
        Check(channel.DroppedCount() == 1, "the layer report was dropped");
        Check(channel.RequestCount() == 1, "and nothing was asked twice");
    }

    void TestNoReplyTimesOut()
    {
        std::printf("only reports that are not the reply\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply(c_LayerReport);
        channel.Reply({ 0xA7, 0x0D, 0x00, 0x03, 0x7F });   // Keychron's report rate changed

        const Outcome outcome = OutcomeOf([&] { Run(via.GetLayerCount()); });

        Check(outcome == Outcome::TimedOut, "the request times out, as a device that never answered");
        Check(channel.DroppedCount() == 2, "both reports were dropped");
    }

    // A refusal is the reply whatever follows its 0xFF, so a firmware zeroing the rest is still
    // heard -- FakeDeviceChannel's ReplyUnhandled() does exactly that.
    void TestRefusalIsTheReply()
    {
        std::printf("a refusal is the reply\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply(c_LayerReport);
        channel.ReplyUnhandled();

        const Outcome outcome = OutcomeOf([&] { Run(via.GetKeyboardValue(via::KeyboardValue::KeycodesVersion)); });

        Check(outcome == Outcome::Refused, "0xFF still reads as the firmware refusing");
        Check(channel.DroppedCount() == 1, "after the layer report was dropped");
    }

    // The address after the command id is echoed too, so an answer for another key -- a late
    // second answer, say -- is not taken for this one.
    void TestAnotherKeysAnswer()
    {
        std::printf("an answer for another key\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply({ 0x04, 0, 1, 3, 0x00, 0x05 });   // layer 0, row 1, column 3
        channel.Reply(c_LayerReport);
        channel.Reply({ 0x04, 0, 1, 2, 0x00, 0x04 });   // layer 0, row 1, column 2: asked for

        uint16_t      keycode = 0;
        const Outcome outcome = OutcomeOf([&] { keycode = Run(via.GetKeycode(0, 1, 2)); });

        Check(outcome == Outcome::Answered && keycode == 0x0004, "the asked key's keycode is read");
        Check(channel.DroppedCount() == 2, "the other key's answer and the layer report were dropped");
    }

    // The paged matrix echoes its first row at byte 2. Keychron's Hall-effect boards answer the
    // matrix twice; a second answer for page 0 arriving while page 9 is asked must not be read
    // as page 9.
    void TestPagedMatrixLateAnswer()
    {
        std::printf("a late answer for another page of the matrix\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        // 10 x 17: three bytes a row, nine rows a page, so two pages.
        channel.Reply({ 0x02, 0x03, 0, 0x00, 0x00, 0x01 });   // page 0: row 0, column 0
        channel.Reply({ 0x02, 0x03, 0, 0x00, 0x00, 0x01 });   // page 0 again, late
        channel.Reply({ 0x02, 0x03, 9, 0x01, 0x00, 0x00 });   // page 9: row 9, column 16

        via::SwitchMatrixState state;
        const Outcome          outcome =
            OutcomeOf([&] { state = Run(via.GetSwitchMatrixState(via::SwitchMatrixFormat::Paged, 10, 17)); });

        Check(outcome == Outcome::Answered, "the matrix is read");
        Check(state.IsPressed(0, 0) && state.IsPressed(9, 16), "each page from its own answer");
        Check(!state.IsPressed(9, 0), "row 9 not taken from page 0's second answer");
        Check(channel.DroppedCount() == 1, "the late answer was dropped");
    }

    // The custom values keep their address: channel and value id, or the value id alone in the
    // format before V3 -- where byte 2 is already the value, and must not be compared.
    void TestCustomValueAddress()
    {
        std::printf("custom values, with a channel and without\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply({ 0x08, 3, 2, 0x07 });   // channel 3, value id 2: not asked for
        channel.Reply({ 0x08, 3, 1, 0x80 });   // channel 3, value id 1

        std::vector<uint8_t> value;
        Outcome              outcome = OutcomeOf([&] { value = Run(via.GetCustomValue(3, 1, {}, 1)); });

        Check(outcome == Outcome::Answered && value == std::vector<uint8_t>{ 0x80 },
              "with a channel: the asked value id's answer is read");
        Check(channel.DroppedCount() == 1, "the other value id's answer was dropped");

        channel.Reply({ 0x08, 0x80, 0x12, 0x34 });   // the format before V3: value id 0x80, the value

        outcome = OutcomeOf([&] { value = Run(via.GetCustomValue(std::nullopt, 0x80, {}, 2)); });

        Check(outcome == Outcome::Answered && value == std::vector<uint8_t>{ 0x12, 0x34 },
              "without a channel: the value from byte 2 is not mistaken for an address");
        Check(channel.DroppedCount() == 1, "nothing more was dropped");
    }

    // Vial overwrites the buffer from byte 0, so no reply check is possible: the first report
    // is the reply, whatever it starts with. Vial firmware sends no reports of its own.
    void TestVialTakesTheFirstReport()
    {
        std::printf("Vial: the first report is the reply\n");

        FakeDeviceChannel channel;
        vial::Protocol    vial(channel);

        channel.Reply({ 0xA3, 0x02, 0x00, 0x00 });   // a definition size of 0x02A3

        uint32_t      size    = 0;
        const Outcome outcome = OutcomeOf([&] { size = Run(vial.GetDefinitionSize()); });

        Check(outcome == Outcome::Answered && size == 0x02A3, "a reply starting 0xA3 is read as data");
        Check(channel.DroppedCount() == 0, "nothing was dropped");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestLayerReportBeforeTheReply();
    TestNoReplyTimesOut();
    TestRefusalIsTheReply();
    TestAnotherKeysAnswer();
    TestPagedMatrixLateAnswer();
    TestCustomValueAddress();
    TestVialTakesTheFirstReport();

    return TestResult();
}
