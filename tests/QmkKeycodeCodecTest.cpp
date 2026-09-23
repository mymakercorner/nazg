// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The QMK keycode codec: raw values to the model's Keycode and back.
//
// Expectations are written in QMK's own keymap syntax through FormatKeycode, so each case
// reads the way the key would appear in a keymap.c -- "LT(1,KC_A)" rather than a
// LayerTapKey with fields spelled out.
//
// The strongest check is the exhaustive one: every 16-bit value, in every keycode
// version, must survive decode-then-encode unchanged. That is the property that makes it
// safe to hold a Keycode in the model instead of the raw value.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/qmk/NazgQmkKeycodeCodec.h"

#include "TestSupport.h"

#include <optional>
#include <string>

using nazg::DecodeQmkKeycode;
using nazg::EncodeQmkKeycode;
using nazg::FormatKeycode;
using nazg::Keycode;
using nazg::QmkKeycodeVersion;

namespace Mod = nazg::Mod;

namespace
{
    constexpr QmkKeycodeVersion c_Latest = nazg::c_LatestQmkKeycodeVersion;

    std::string Decoded(uint16_t value, QmkKeycodeVersion version = c_Latest)
    {
        return FormatKeycode(DecodeQmkKeycode(value, version));
    }

    // -1 for "cannot be stored", so a failed encode compares cleanly in a Check.
    int Encoded(const Keycode& keycode, QmkKeycodeVersion version = c_Latest)
    {
        const std::optional<uint16_t> value = EncodeQmkKeycode(keycode, version);
        return value ? *value : -1;
    }

    void TestFixedKeycodes()
    {
        std::printf("fixed keycodes\n");

        Check(Decoded(0x0004) == "KC_A", "0x0004 is KC_A");
        Check(Decoded(0x0000) == "KC_NO" && Decoded(0x0001) == "KC_TRNS", "KC_NO and KC_TRNS");
        Check(Decoded(0x7C42) == "HF_TOGG", "the haptic toggle has a name");
        Check(Decoded(0x56F0) == "SH_TOGG", "a fixed swap-hands keycode inside the SH_T range");
        Check(Decoded(0x7110, QmkKeycodeVersion::V0_0_1) == "MI_C" && Decoded(0x7110) == "MI_Cs1",
              "the same value decodes per version");
    }

    void TestModifiers()
    {
        std::printf("modifiers\n");

        Check(Decoded(0x0104) == "LCTL(KC_A)", "0x0104 is LCTL(KC_A)");
        Check(Decoded(0x0304) == "LCTL(LSFT(KC_A))", "two modifiers nest");
        Check(Decoded(0x1204) == "RSFT(KC_A)", "bit 4 of the mods makes them right-hand");
        Check(Decoded(0x1F04) == "RCTL(RSFT(RALT(RGUI(KC_A))))", "all four on the right");
        Check(Decoded(0x1004) == "0x1004", "the right-hand flag with no modifier is not a keycode");

        Check(Encoded(nazg::ModifiedKey{ Mod::LeftCtrl, "KC_A" }) == 0x0104, "LCTL(KC_A) encodes to 0x0104");
        Check(Encoded(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::RightShift, "KC_A" }) == -1,
              "mixing sides cannot be stored on QMK");
        Check(FormatKeycode(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::RightShift, "KC_A" }) == "LCTL(RSFT(KC_A))",
              "but can still be shown");
        Check(Encoded(nazg::ModifiedKey{ Mod::LeftShift, "HF_TOGG" }) == -1,
              "only a basic keycode fits inside a modifier wrapper");
    }

    void TestTapHold()
    {
        std::printf("tap-hold\n");

        Check(Decoded(0x2104) == "MT(MOD_LCTL,KC_A)", "0x2104 is MT(MOD_LCTL,KC_A)");
        Check(Decoded(0x3204) == "MT(MOD_RSFT,KC_A)", "right-hand mod-tap");
        Check(Decoded(0x4104) == "LT(1,KC_A)", "0x4104 is LT(1,KC_A)");
        Check(Decoded(0x4F00) == "LT(15,KC_NO)", "LT reaches layer 15, and KC_NO is a valid tap");
        Check(Decoded(0x5604) == "SH_T(KC_A)", "0x5604 is SH_T(KC_A)");

        Check(Encoded(nazg::LayerTapKey{ 16, "KC_A" }) == -1, "LT has no room for layer 16");
        Check(Encoded(nazg::ModTapKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_ESC" }) == 0x2329,
              "MT(MOD_LCTL|MOD_LSFT,KC_ESC) encodes");
    }

    void TestLayers()
    {
        std::printf("layers\n");

        Check(Decoded(0x5200) == "TO(0)", "TO");
        Check(Decoded(0x5221) == "MO(1)", "MO");
        Check(Decoded(0x5242) == "DF(2)", "DF");
        Check(Decoded(0x5263) == "TG(3)", "TG");
        Check(Decoded(0x5281) == "OSL(1)", "OSL");
        Check(Decoded(0x52A2) == "OSM(MOD_LSFT)", "OSM sits among the layer blocks but carries mods");
        Check(Decoded(0x52C4) == "TT(4)", "TT");
        Check(Decoded(0x5022) == "LM(1,MOD_LSFT)", "LM");

        Check(Decoded(0x52E1, QmkKeycodeVersion::V0_0_6) == "PDF(1)", "PDF from keycode spec 0.0.6");
        Check(Decoded(0x52E1, QmkKeycodeVersion::V0_0_5) == "0x52E1", "and nothing before");
        Check(Encoded(nazg::LayerKey{ nazg::LayerOp::PersistentDefault, 1 }, QmkKeycodeVersion::V0_0_5) == -1,
              "so a 0.0.5 board cannot store PDF");

        Check(Encoded(nazg::LayerKey{ nazg::LayerOp::Momentary, 31 }) == 0x523F, "MO reaches layer 31");
        Check(Encoded(nazg::LayerKey{ nazg::LayerOp::Momentary, 32 }) == -1, "but not 32");
    }

    void TestIndexed()
    {
        std::printf("tap dance and macros\n");

        Check(Decoded(0x5703) == "TD(3)", "0x5703 is TD(3)");
        Check(Decoded(0x7705) == "MC_5", "0x7705 is macro 5");
        Check(Decoded(0x7740) == "MC_64", "macros past 31 have no QMK name but are still macros");
        Check(Encoded(nazg::MacroKey{ 128 }) == -1, "the macro range ends at 127");
    }

    void TestUnknown()
    {
        std::printf("unknown values\n");

        Check(Decoded(0x0002) == "0x0002", "an unassigned value is kept raw");
        Check(Decoded(0x8123) == "0x8123", "unicode is not decoded yet");
        Check(Encoded(nazg::UnknownKey{ 0x8123 }) == 0x8123, "and writes back unchanged");
        Check(Encoded(nazg::NamedKey{ "QK_LLCK" }, QmkKeycodeVersion::V0_0_5) == -1,
              "a named keycode newer than the board cannot be stored");
    }

    // The point of holding a Keycode rather than a value: it carries its meaning to a
    // board on another keycode version.
    void TestAcrossVersions()
    {
        std::printf("across versions\n");

        const Keycode midiC = DecodeQmkKeycode(0x7110, QmkKeycodeVersion::V0_0_1);
        Check(Encoded(midiC, QmkKeycodeVersion::V0_0_9) == 0x7103,
              "MI_C read off a 0.0.1 board writes to a 0.0.9 board at its new value");

        const Keycode outputAuto = DecodeQmkKeycode(0x7C20, QmkKeycodeVersion::V0_0_5);
        Check(Encoded(outputAuto, QmkKeycodeVersion::V0_0_6) == 0x7780, "OU_AUTO follows its move in 0.0.6");
    }

    void TestRoundTrip()
    {
        std::printf("round trip, every value in every version\n");

        const QmkKeycodeVersion versions[] = {
            QmkKeycodeVersion::V0_0_1, QmkKeycodeVersion::V0_0_2, QmkKeycodeVersion::V0_0_3,
            QmkKeycodeVersion::V0_0_4, QmkKeycodeVersion::V0_0_5, QmkKeycodeVersion::V0_0_6,
            QmkKeycodeVersion::V0_0_7, QmkKeycodeVersion::V0_0_8, QmkKeycodeVersion::V0_0_9,
        };

        for (QmkKeycodeVersion version : versions)
        {
            int      failures = 0;
            uint32_t first    = 0;

            for (uint32_t value = 0; value <= 0xFFFF; ++value)
            {
                const Keycode                 keycode = DecodeQmkKeycode(static_cast<uint16_t>(value), version);
                const std::optional<uint16_t> back    = EncodeQmkKeycode(keycode, version);

                if (!back || *back != value)
                {
                    if (failures++ == 0)
                        first = value;
                }
            }

            std::string description = std::string("all 65536 values survive decode and encode in ") +
                                      nazg::QmkKeycodeVersionName(version);
            if (failures != 0)
                description += " (" + std::to_string(failures) + " failed, first " + std::to_string(first) + ")";

            Check(failures == 0, description.c_str());
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestFixedKeycodes();
    TestModifiers();
    TestTapHold();
    TestLayers();
    TestIndexed();
    TestUnknown();
    TestAcrossVersions();
    TestRoundTrip();

    return TestResult();
}
