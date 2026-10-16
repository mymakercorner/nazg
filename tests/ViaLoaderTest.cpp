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

namespace qmk = nazg::qmk;
namespace via = nazg::via;

using nazg::FormatKeycode;
using nazg::Keyboard;
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

    // A 1 x 2 board -- the smallest that still shows cells landing where they should.
    via::KeyboardDefinition TinyDefinition()
    {
        via::KeyboardDefinition definition;
        definition.name          = "tiny";
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

        Check(via::KeycodeVersionFor(12) == qmk::KeycodeVersion::V0_0_8,
              "12 -- the Aquanaut -- takes 0.0.8, the newest a protocol-12 build can have");
        Check(via::KeycodeVersionFor(11) == qmk::KeycodeVersion::V0_0_1, "11 came with 0.0.1");
        Check(via::KeycodeVersionFor(10) == qmk::KeycodeVersion::LegacyVia10,
              "10 is pre-renumbering, with TO(n) as 0x5000 | n");
        Check(via::KeycodeVersionFor(9) == qmk::KeycodeVersion::Legacy, "9 and below carry TO's ON_PRESS bit");
    }

    void TestProtocol12()
    {
        std::printf("load, protocol 12\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply({ 0x01, 0x00, 0x0C });              // protocol version 12
        ReplyKeymap(channel);

        const Keyboard keyboard = Run(via::LoadKeyboard(via, TinyDefinition()));

        Check(keyboard.Name() == "tiny", "the definition the caller brought is the board's");
        Check(keyboard.keycodeVersion == qmk::KeycodeVersion::V0_0_8, "protocol 12 reads with 0.0.8");
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
            via::Protocol     via(channel);

            channel.Reply({ 0x01, 0x00, 0x0D });                       // protocol version 13
            channel.Reply({ 0x02, 0x06, 0x00, 0x00, 0x00, 0x07 });     // keycodes version 0.0.7
            ReplyKeymap(channel);

            const Keyboard keyboard = Run(via::LoadKeyboard(via, TinyDefinition()));

            Check(keyboard.keycodeVersion == qmk::KeycodeVersion::V0_0_7,
                  "protocol 13 is asked, and its answer is used");
            Check(channel.RequestAt(1)[0] == 0x02 && channel.RequestAt(1)[1] == 0x06,
                  "the question is id_get_keyboard_value(id_keycodes_version)");
        }

        {
            FakeDeviceChannel channel;
            via::Protocol     via(channel);

            channel.Reply({ 0x01, 0x00, 0x0D });
            channel.Reply({ 0x02, 0x06, 0x00, 0x00, 0x00, 0x10 });     // 0.0.10, newer than this build
            ReplyKeymap(channel);

            const Keyboard keyboard = Run(via::LoadKeyboard(via, TinyDefinition()));

            Check(keyboard.keycodeVersion == qmk::c_LatestKeycodeVersion,
                  "a version newer than the table reads with the newest there is");
        }
    }

    // A protocol-9 board speaks the numbering from before QMK's renumbering: MO(1) is
    // 0x5101 there, and QK_BOOT -- RESET, back then -- is 0x5C00.
    void TestPreRenumbering()
    {
        std::printf("load, pre-renumbering\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        channel.Reply({ 0x01, 0x00, 0x09 });              // protocol version 9
        channel.Reply({ 0x11, 2 });
        channel.Reply({ 0xFF });
        channel.Reply({ 0x12, 0x00, 0x00, 8,
                        0x00, 0x04, 0x51, 0x01,
                        0x00, 0x01, 0x5C, 0x00 });

        const Keyboard keyboard = Run(via::LoadKeyboard(via, TinyDefinition()));

        Check(keyboard.keycodeVersion == qmk::KeycodeVersion::Legacy, "protocol 9 reads with the Legacy table");
        Check(FormatKeycode(keyboard.keymap.At(0, 0, 0)) == "KC_A" &&
              FormatKeycode(keyboard.keymap.At(0, 0, 1)) == "MO(1)" &&
              FormatKeycode(keyboard.keymap.At(1, 0, 1)) == "QK_BOOT",
              "old values arrive as today's keycodes");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestVersionFromProtocol();
    TestProtocol12();
    TestProtocol13();
    TestPreRenumbering();

    return TestResult();
}
