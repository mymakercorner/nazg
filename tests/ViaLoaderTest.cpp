// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The VIA loader against a scripted board: every exchange a real load makes, in order,
// so what is pinned is the whole conversation -- which questions get asked, and how the
// keycode version is chosen from the answers.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaLoader.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <stdexcept>
#include <string>
#include <vector>

using nazg::FormatKeycode;
using nazg::Keyboard;
using nazg::KeyboardDefinition;
using nazg::QmkKeycodeVersion;
using nazg::QmkKeycodeVersionForVia;
using nazg::Task;
using nazg::ViaProtocol;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the load did not complete synchronously");

        return task.TakeResult();
    }

    // A 1 x 2 board -- the smallest that still shows cells landing where they should.
    KeyboardDefinition TinyDefinition()
    {
        KeyboardDefinition definition;
        definition.name          = "tiny";
        definition.matrixRows    = 1;
        definition.matrixColumns = 2;

        nazg::DefinitionKey left;
        left.row    = 0;
        left.column = 0;

        nazg::DefinitionKey right = left;
        right.x      = 1.0f;
        right.column = 1;

        definition.keys = { left, right };
        return definition;
    }

    // Two layers of two cells, as one 8-byte chunk of dynamic_keymap_get_buffer:
    // layer 0 is KC_A, MO(1); layer 1 is KC_TRNS, HF_TOGG.
    void ReplyKeymap(FakeDeviceChannel& channel)
    {
        channel.Reply({ 0x11, 2 });                       // layer count
        channel.Reply({ 0xFF });                          // layout options: none
        channel.Reply({ 0x12, 0x00, 0x00, 8,
                        0x00, 0x04, 0x52, 0x21,
                        0x00, 0x01, 0x7C, 0x42 });
    }

    void TestVersionFromProtocol()
    {
        std::printf("keycode version from the VIA protocol\n");

        Check(QmkKeycodeVersionForVia(12) == QmkKeycodeVersion::V0_0_8,
              "12 -- the Aquanaut -- takes 0.0.8, the newest a protocol-12 build can have");
        Check(QmkKeycodeVersionForVia(11) == QmkKeycodeVersion::V0_0_1, "11 came with 0.0.1");
        Check(!QmkKeycodeVersionForVia(10).has_value(), "10 is pre-renumbering: no table yet");
        Check(!QmkKeycodeVersionForVia(9).has_value(), "and so is 9");
    }

    void TestProtocol12()
    {
        std::printf("load, protocol 12\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x01, 0x00, 0x0C });              // protocol version 12
        ReplyKeymap(channel);

        const Keyboard keyboard = Run(nazg::LoadViaKeyboard(via, TinyDefinition()));

        Check(keyboard.Name() == "tiny", "the definition the caller brought is the board's");
        Check(keyboard.keycodeVersion == QmkKeycodeVersion::V0_0_8, "protocol 12 reads with 0.0.8");
        Check(keyboard.keymap.Layers() == 2, "two layers");
        Check(FormatKeycode(keyboard.keymap.At(0, 0, 0)) == "KC_A" &&
              FormatKeycode(keyboard.keymap.At(0, 0, 1)) == "MO(1)" &&
              FormatKeycode(keyboard.keymap.At(1, 0, 0)) == "KC_TRNS" &&
              FormatKeycode(keyboard.keymap.At(1, 0, 1)) == "HF_TOGG",
              "every cell decoded where it belongs");
        Check(channel.RequestCount() == 4, "four exchanges: protocol, layers, layout options, keymap");
    }

    void TestProtocol13()
    {
        std::printf("load, protocol 13\n");

        {
            FakeDeviceChannel channel;
            ViaProtocol       via(channel);

            channel.Reply({ 0x01, 0x00, 0x0D });                       // protocol version 13
            channel.Reply({ 0x02, 0x06, 0x00, 0x00, 0x00, 0x07 });     // keycodes version 0.0.7
            ReplyKeymap(channel);

            const Keyboard keyboard = Run(nazg::LoadViaKeyboard(via, TinyDefinition()));

            Check(keyboard.keycodeVersion == QmkKeycodeVersion::V0_0_7,
                  "protocol 13 is asked, and its answer is used");
            Check(channel.RequestAt(1)[0] == 0x02 && channel.RequestAt(1)[1] == 0x06,
                  "the question is id_get_keyboard_value(id_keycodes_version)");
        }

        {
            FakeDeviceChannel channel;
            ViaProtocol       via(channel);

            channel.Reply({ 0x01, 0x00, 0x0D });
            channel.Reply({ 0x02, 0x06, 0x00, 0x00, 0x00, 0x10 });     // 0.0.10, newer than this build
            ReplyKeymap(channel);

            const Keyboard keyboard = Run(nazg::LoadViaKeyboard(via, TinyDefinition()));

            Check(keyboard.keycodeVersion == nazg::c_LatestQmkKeycodeVersion,
                  "a version newer than the table reads with the newest there is");
        }
    }

    void TestPreRenumberingIsRefused()
    {
        std::printf("pre-renumbering\n");

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x01, 0x00, 0x09 });              // protocol version 9

        bool refused = false;
        try
        {
            (void)Run(nazg::LoadViaKeyboard(via, TinyDefinition()));
        }
        catch (const nazg::ProtocolError& failure)
        {
            refused = std::string(failure.what()).find("pre-renumbering") != std::string::npos;
        }

        Check(refused, "protocol 9 is refused with a reason");
        Check(channel.RequestCount() == 1, "before the keymap is fetched");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestVersionFromProtocol();
    TestProtocol12();
    TestProtocol13();
    TestPreRenumberingIsRefused();

    return TestResult();
}
