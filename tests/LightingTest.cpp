// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Lighting section's model and wire (model/NazgLighting.h, adapters/via/NazgViaLighting.h):
// which systems a board gets and how it is spoken to; underglow's modes as effects and variants;
// the keys that change each line; and the exact bytes of the three formats -- VIA V3's channels,
// the format before it that vial-qmk keeps, and VialRGB -- against a scripted channel.
//
// Byte layouts from QMK's quantum/via.c and vial-qmk's quantum/via.c and vialrgb.c
// (via-vial-commands.md, "Lighting").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaLighting.h"
#include "model/NazgLighting.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace via = nazg::via;

using nazg::Keyboard;
using nazg::LightingKind;
using nazg::LightingLine;
using nazg::LightingTarget;
using nazg::LightingValues;
using nazg::LightingWire;
using nazg::Task;

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
        if (frame.size() != via::c_ReportSize)
            return false;
        for (size_t at = 0; at < bytes.size(); ++at)
            if (frame[at] != bytes[at])
                return false;
        for (size_t at = bytes.size(); at < frame.size(); ++at)
            if (frame[at] != 0)
                return false;
        return true;
    }

    Keyboard BoardWith(std::string lighting, std::vector<std::string> menus, bool vial, uint16_t via)
    {
        Keyboard keyboard;
        keyboard.definition.lighting = std::move(lighting);
        keyboard.definition.menuIds  = std::move(menus);
        keyboard.report.isVial       = vial;
        keyboard.report.viaProtocol  = via;
        return keyboard;
    }

    void TestTargets()
    {
        std::printf("which systems, spoken to how\n");

        using T = std::vector<LightingTarget>;
        Check(nazg::LightingTargetsOf(BoardWith("", { "qmk_rgblight" }, false, 12)) ==
                  T{ { LightingKind::Underglow, LightingWire::Channels } },
              "the Phoenix: VIA 12's underglow menu, on channels");
        Check(nazg::LightingTargetsOf(BoardWith("", { "qmk_backlight_rgblight" }, false, 12)) ==
                  T{ { LightingKind::Backlight, LightingWire::Channels }, { LightingKind::Underglow, LightingWire::Channels } },
              "backlight + underglow, backlight first");
        Check(nazg::LightingTargetsOf(BoardWith("", { "qmk_rgb_matrix" }, false, 12)) ==
                  T{ { LightingKind::RgbMatrix, LightingWire::Channels } },
              "VIA's RGB Matrix menu");
        Check(nazg::LightingTargetsOf(BoardWith("qmk_rgblight", {}, false, 10)) ==
                  T{ { LightingKind::Underglow, LightingWire::Legacy } },
              "VIA 10: the format before V3");
        Check(nazg::LightingTargetsOf(BoardWith("qmk_backlight_rgblight", {}, true, 9)) ==
                  T{ { LightingKind::Backlight, LightingWire::Legacy }, { LightingKind::Underglow, LightingWire::Legacy } },
              "Vial: the format before V3, whatever it reports");
        Check(nazg::LightingTargetsOf(BoardWith("vialrgb", {}, true, 9)) == T{ { LightingKind::RgbMatrix, LightingWire::VialRgb } },
              "Vial's VialRGB");
        Check(nazg::LightingTargetsOf(BoardWith("qmk_rgb_matrix", {}, true, 9)).empty(),
              "vial-qmk's RGB Matrix without VialRGB: none");
        Check(nazg::LightingTargetsOf(BoardWith("none", { "qmk_audio" }, false, 12)).empty(), "no lighting: none");
    }

    void TestObjectPreset()
    {
        std::printf("a lighting preset given as an object\n");

        const std::string json = R"({"name":"Crkbd","lighting":{"extends":"qmk_rgblight"},"matrix":{"rows":1,"cols":1},)"
                                 R"("layouts":{"keymap":[["0,0"]]}})";
        const via::KeyboardDefinition definition = via::ParseDefinition(std::vector<uint8_t>(json.begin(), json.end()));
        Check(definition.lighting == "qmk_rgblight", "the preset it extends, as the Corne's vial.json writes it");
    }

    void TestUnderglowEffects()
    {
        std::printf("underglow's modes as effects and variants\n");

        const auto* breathing = nazg::UnderglowEffectOf(4);
        Check(breathing && breathing->name == "Breathing" && nazg::UnderglowVariantOf(4).speed == 2,
              "mode 4 is Breathing 3: the third speed");
        const auto* swirl = nazg::UnderglowEffectOf(12);
        const auto  v12   = nazg::UnderglowVariantOf(12);
        Check(swirl && swirl->name == "Rainbow Swirl" && v12.speed == 1 && v12.direction == 1,
              "mode 12 is Rainbow Swirl 4: the second speed, the other way");
        const auto v30 = nazg::UnderglowVariantOf(30);
        Check(nazg::UnderglowEffectOf(30)->name == "Gradient" && v30.spread == 2 && v30.direction == 1,
              "mode 30 is Gradient 6: the third spread, the other way");
        const auto v41 = nazg::UnderglowVariantOf(41);
        Check(nazg::UnderglowEffectOf(41)->name == "Twinkle" && v41.speed == 1 && v41.random,
              "mode 41 is Twinkle 5: the second speed, random colours");
        Check(nazg::UnderglowEffectOf(0) == nullptr && nazg::UnderglowEffectOf(43) == nullptr, "0 and past 42: none");

        bool roundTrip = true;
        for (uint16_t mode = 1; mode <= 42; ++mode)
            roundTrip = roundTrip && nazg::UnderglowMode(*nazg::UnderglowEffectOf(mode), nazg::UnderglowVariantOf(mode)) == mode;
        Check(roundTrip, "every mode back from its effect and variant");

        Check(nazg::UnderglowColourUse(1) == nazg::ColourUse::HueAndSaturation &&
                  nazg::UnderglowColourUse(7) == nazg::ColourUse::Saturation &&
                  nazg::UnderglowColourUse(35) == nazg::ColourUse::None &&
                  nazg::UnderglowColourUse(37) == nazg::ColourUse::HueAndSaturation &&
                  nazg::UnderglowColourUse(40) == nazg::ColourUse::Saturation,
              "the colour each takes: Solid both, Rainbow Mood saturation, RGB Test none, Twinkle both, random saturation");
    }

    void TestKeys()
    {
        std::printf("the keys that change a line\n");

        using nazg::ChangesLighting;
        Check(ChangesLighting("UG_VALU", LightingKind::Underglow, LightingLine::Brightness, true) &&
                  !ChangesLighting("UG_VALU", LightingKind::Underglow, LightingLine::Colour, true),
              "Bri + changes underglow's brightness, not its colour");
        Check(ChangesLighting("UG_TOGG", LightingKind::Underglow, LightingLine::On, true) &&
                  ChangesLighting("UG_NEXT", LightingKind::Underglow, LightingLine::Effect, true) &&
                  ChangesLighting("RGB_M_SW", LightingKind::Underglow, LightingLine::Effect, true) &&
                  ChangesLighting("UG_SATD", LightingKind::Underglow, LightingLine::Colour, true),
              "On/Off, Next, a mode key, Sat −");
        Check(!ChangesLighting("UG_TOGG", LightingKind::RgbMatrix, LightingLine::Any, true) &&
                  ChangesLighting("UG_TOGG", LightingKind::RgbMatrix, LightingLine::Any, false),
              "UG_* drive an RGB Matrix only where there is no underglow");
        Check(ChangesLighting("BL_STEP", LightingKind::Backlight, LightingLine::Brightness, false) &&
                  ChangesLighting("BL_BRTG", LightingKind::Backlight, LightingLine::Effect, false) &&
                  !ChangesLighting("KC_A", LightingKind::Backlight, LightingLine::Any, false),
              "the backlight's Step and Breathe; a letter changes nothing");
    }

    void TestChannels()
    {
        std::printf("VIA V3's channels\n");

        const LightingTarget target{ LightingKind::Underglow, LightingWire::Channels };
        FakeDeviceChannel    channel;
        via::Protocol        via(channel);

        channel.Reply({ 0x08, 2, 1, 200 });
        channel.Reply({ 0x08, 2, 2, 3 });
        channel.Reply({ 0x08, 2, 3, 0 });
        channel.Reply({ 0x08, 2, 4, 170, 255 });
        const LightingValues read = Run(via::ReadLighting(via, target));
        Check(Starts(channel.RequestAt(0), { 0x08, 2, 1 }) && Starts(channel.RequestAt(3), { 0x08, 2, 4 }),
              "get: [0x08][channel 2][value id]");
        Check(read.brightness == 200 && read.effect == 3 && read.hue == 170 && read.saturation == 255 && read.on == true,
              "brightness, Breathing 2, blue, on");

        channel.Reply({ 0x08, 2, 1, 0 });
        channel.Reply({ 0x08, 2, 2, 0 });
        channel.Reply({ 0x08, 2, 3, 0 });
        channel.Reply({ 0x08, 2, 4, 0, 0 });
        Check(Run(via::ReadLighting(via, target)).on == false, "effect 0: off");

        LightingValues values = read;
        values.brightness     = 128;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, target, LightingLine::Brightness, values));
        Check(Starts(channel.Requests().back(), { 0x07, 2, 1, 128 }), "set brightness: [0x07][2][1][value]");

        values.on = false;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, target, LightingLine::On, values));
        Check(Starts(channel.Requests().back(), { 0x07, 2, 2, 0 }), "off: effect 0");

        values.on     = true;
        values.effect = 9;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, target, LightingLine::Effect, values));
        Check(Starts(channel.Requests().back(), { 0x07, 2, 2, 9 }), "an effect, once");

        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, target, LightingLine::Colour, values));
        Check(Starts(channel.Requests().back(), { 0x07, 2, 4, 170, 255 }), "colour: hue then saturation");

        channel.Reply({ 0x09 });
        Run(via::SaveLighting(via, target));
        Check(Starts(channel.Requests().back(), { 0x09, 2 }), "save: [0x09][channel]");

        channel.ReplyUnhandled();
        bool refused = false;
        try
        {
            Run(via::SaveLighting(via, target));
        }
        catch (const nazg::ProtocolError&)
        {
            refused = true;
        }
        Check(refused, "a channel the firmware lacks: refused");
    }

    void TestLegacy()
    {
        std::printf("the format before V3, as vial-qmk speaks it\n");

        FakeDeviceChannel channel;
        via::Protocol     via(channel);

        const LightingTarget underglow{ LightingKind::Underglow, LightingWire::Legacy };
        channel.Reply({ 0x08, 0x80, 120 });
        channel.Reply({ 0x08, 0x81, 1 });
        channel.Reply({ 0x08, 0x82, 0 });
        channel.Reply({ 0x08, 0x83, 85, 200 });
        const LightingValues read = Run(via::ReadLighting(via, underglow));
        Check(Starts(channel.RequestAt(0), { 0x08, 0x80 }) && Starts(channel.RequestAt(3), { 0x08, 0x83 }),
              "get: [0x08][value id], ids 0x80-0x83");
        Check(read.brightness == 120 && read.effect == 1 && read.hue == 85 && !read.on.has_value(),
              "whether it is on cannot be read");

        LightingValues values = read;
        values.on             = true;
        values.effect         = 6;
        channel.Reply({ 0x07 });
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, underglow, LightingLine::Effect, values));
        Check(channel.RequestCount() == 6 && Starts(channel.RequestAt(4), { 0x07, 0x81, 6 }) &&
                  Starts(channel.RequestAt(5), { 0x07, 0x81, 6 }),
              "an effect twice: the first only turns on a light that was off");

        values.on = false;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, underglow, LightingLine::On, values));
        Check(channel.RequestCount() == 7 && Starts(channel.Requests().back(), { 0x07, 0x81, 0 }), "off: effect 0, once");

        const LightingTarget backlight{ LightingKind::Backlight, LightingWire::Legacy };
        values.brightness = 170;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, backlight, LightingLine::Brightness, values));
        Check(Starts(channel.Requests().back(), { 0x07, 0x09, 170 }), "the backlight's brightness: id 0x09");

        channel.Reply({ 0x09 });
        Run(via::SaveLighting(via, backlight));
        Check(Starts(channel.Requests().back(), { 0x09 }), "save: [0x09], no channel");
    }

    void TestVialRgb()
    {
        std::printf("VialRGB\n");

        FakeDeviceChannel    channel;
        via::Protocol        via(channel);
        const LightingTarget target{ LightingKind::RgbMatrix, LightingWire::VialRgb };

        channel.Reply({ 0x08, 0x40, 1, 0, 200 });
        const via::VialRgbInfo info = Run(via::ReadVialRgbInfo(via));
        Check(Starts(channel.RequestAt(0), { 0x08, 0x40 }) && info.version == 1 && info.maxBrightness == 200,
              "info: version 1, brightness up to 200");

        // 16 effects: a full reply of 15, then the rest.
        std::vector<uint8_t> full{ 0x08, 0x42 };
        for (uint16_t id = 2; id <= 16; ++id)
            full.insert(full.end(), { static_cast<uint8_t>(id), 0 });
        channel.Reply(full);
        channel.Reply({ 0x08, 0x42, 17, 0, 0xFF, 0xFF });
        const std::vector<uint16_t> effects = Run(via::ReadVialRgbEffects(via));
        Check(effects.size() == 16 && effects.front() == 2 && effects.back() == 17, "every effect, over two replies");
        Check(Starts(channel.RequestAt(2), { 0x08, 0x42, 16, 0 }), "asked again from the last");

        channel.Reply({ 0x08, 0x41, 14, 0, 128, 10, 255, 100 });
        const LightingValues read = Run(via::ReadLighting(via, target, 200));
        Check(read.effect == 14 && read.speed == 128 && read.hue == 10 && read.saturation == 255 && read.brightness == 128 &&
                  read.on == true,
              "mode: Cycle Left Right, speed, colour, brightness 100 of 200 as 128");

        LightingValues values = read;
        values.on             = false;
        channel.Reply({ 0x07 });
        Run(via::WriteLighting(via, target, LightingLine::On, values, 200));
        Check(Starts(channel.Requests().back(), { 0x07, 0x41, 0, 0, 128, 10, 255, 100 }),
              "off: effect 0, the rest kept, brightness back on its scale");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestTargets();
    TestObjectPreset();
    TestUnderglowEffects();
    TestKeys();
    TestChannels();
    TestLegacy();
    TestVialRgb();

    return TestResult();
}
