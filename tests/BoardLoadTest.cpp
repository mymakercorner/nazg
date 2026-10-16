// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Opening a board against a scripted one: which protocol it is taken for, which definition
// draws a VIA board -- the only one, the remembered choice, or none until the picker asks --
// and the Vial board that must be unplugged first. The conversation is pinned as a whole, so
// a question added to the load shows here.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "board/NazgBoardLoad.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"
#include "ViaBundleFixture.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace via  = nazg::via;
namespace vial = nazg::vial;

using nazg::BoardLoad;
using nazg::DefinitionCandidate;
using nazg::DefinitionRef;
using nazg::DeviceIdentity;
using nazg::Task;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the load did not complete synchronously");

        return task.TakeResult();
    }

    // A 1 x 2 board -- the smallest that still has a keymap.
    via::KeyboardDefinition TinyDefinition(const std::string& name)
    {
        via::KeyboardDefinition definition;
        definition.name          = name;
        definition.matrixRows    = 1;
        definition.matrixColumns = 2;

        via::DefinitionKey left;
        left.row    = 0;
        left.column = 0;

        via::DefinitionKey right = left;
        right.x      = 1.0f;
        right.column = 1;

        definition.keys = { left, right };
        return definition;
    }

    DefinitionCandidate UserCandidate(uint32_t id, const std::string& name)
    {
        return { DefinitionRef::User(id), TinyDefinition(name), "C:/definitions/" + name + ".json", {} };
    }

    DeviceIdentity Board(uint16_t vendorId = 0xFEED, uint16_t productId = 0x0001)
    {
        return { vendorId, productId, "maker", "tiny", 0x0100, {} };
    }

    // Everything a VIA board answers once its definition is settled: the load -- protocol 12,
    // two layers, no layout options, the keymap -- then the board report: the protocol again,
    // not Vial, four macros.
    void ReplyViaBoard(FakeDeviceChannel& channel)
    {
        channel.Reply({ 0x01, 0x00, 0x0C });
        channel.Reply({ 0x11, 2 });
        channel.Reply({ 0xFF });
        channel.Reply({ 0x12, 0x00, 0x00, 8, 0x00, 0x04, 0x52, 0x21, 0x00, 0x01, 0x7C, 0x42 });

        channel.Reply({ 0x01, 0x00, 0x0C });
        channel.ReplyUnhandled();
        channel.Reply({ 0x0C, 4 });
    }

    void TestNoDefinition()
    {
        std::printf("a VIA board nothing describes\n");

        FakeDeviceChannel channel;
        vial::Protocol    protocol(channel);
        channel.ReplyUnhandled();   // not Vial

        const BoardLoad load = Run(nazg::LoadBoard(protocol, Board(), {}, std::nullopt, {}));

        Check(!load.isVial && !load.keyboard && load.candidates.empty() && !load.used,
              "no candidate: no keyboard, and the picker says so");
        Check(channel.RequestCount() == 1, "the Vial probe only: nothing else is asked");
    }

    void TestOnlyCandidate()
    {
        std::printf("a VIA board with one definition\n");

        FakeDeviceChannel channel;
        vial::Protocol    protocol(channel);
        channel.ReplyUnhandled();
        ReplyViaBoard(channel);

        const BoardLoad load =
            Run(nazg::LoadBoard(protocol, Board(), { UserCandidate(7, "tiny") }, std::nullopt, {}));

        Check(load.keyboard.has_value() && load.keyboard->Name() == "tiny", "the only candidate draws it");
        Check(load.used == 0 && load.candidates[0].ref == DefinitionRef::User(7), "and is the one in use");
        Check(load.exportable.empty(), "a user definition's export is the library's copy, not the load's");
        Check(load.keyboard->report.macroCount == 4 && !load.keyboard->report.isVial, "the board report is read");
        Check(channel.RequestCount() == 8, "probe, load, report: eight exchanges");
    }

    void TestRememberedChoice()
    {
        std::printf("a VIA board with a remembered choice\n");

        FakeDeviceChannel channel;
        vial::Protocol    protocol(channel);
        channel.ReplyUnhandled();
        ReplyViaBoard(channel);

        const BoardLoad load = Run(nazg::LoadBoard(protocol, Board(),
                                                   { UserCandidate(1, "tiny"), UserCandidate(2, "tiny b") },
                                                   DefinitionRef::User(2), {}));

        Check(load.keyboard.has_value() && load.keyboard->Name() == "tiny b", "the choice draws it");
        Check(load.used && load.candidates[*load.used].ref == DefinitionRef::User(2), "and is the one in use");
    }

    void TestPickerAsks()
    {
        std::printf("a VIA board with several definitions and no choice\n");

        const via::DefinitionBundle official(ViaBundleFixture());
        const DeviceIdentity        isoMacro = Board(0x4D65, 0x1200);

        FakeDeviceChannel channel;
        vial::Protocol    protocol(channel);
        channel.ReplyUnhandled();
        channel.Reply({ 0x01, 0x00, 0x0C });   // the protocol, which picks the V3 file

        const BoardLoad load =
            Run(nazg::LoadBoard(protocol, isoMacro, { UserCandidate(3, "mine") }, std::nullopt, { &official, nullptr }));

        Check(!load.keyboard && !load.used, "two candidates and no choice: nothing drawn until the picker answers");
        Check(load.candidates.size() == 2 && load.candidates[0].ref == DefinitionRef::User(3) &&
                  load.candidates[1].ref.kind == DefinitionRef::Kind::Official,
              "the user definition first, VIA's after it");
        Check(load.bundled.size() == 1 && load.bundled[0].ref == load.candidates[1].ref,
              "the official one is kept apart too, for the list to be rebuilt");
        Check(channel.RequestCount() == 2, "the probe and the protocol: no keymap is read yet");
    }

    void TestUnlockUnfinished()
    {
        std::printf("a Vial board waiting for an unlock\n");

        FakeDeviceChannel channel;
        vial::Protocol    protocol(channel);
        channel.Reply({ 0x06, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0 });   // Vial protocol 6
        channel.Reply({ 0x00, 0x01 });                                // locked, an unlock in progress

        bool refused = false;
        try
        {
            (void)Run(nazg::LoadBoard(protocol, Board(), {}, std::nullopt, {}));
        }
        catch (const std::runtime_error& failure)
        {
            refused = std::string(failure.what()).find("unlock") != std::string::npos;
        }

        Check(refused, "it must be unplugged first: the load says so");
        Check(channel.RequestCount() == 2, "and asks nothing past the lock, whose answers would be echoes");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestNoDefinition();
    TestOnlyCandidate();
    TestRememberedChoice();
    TestPickerAsks();
    TestUnlockUnfinished();

    return TestResult();
}
