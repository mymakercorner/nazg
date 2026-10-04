// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Any entry's reader (adapters/qmk/NazgQmkExpression.h): whatever FormatKeycode() prints,
// it reads back -- every named keycode of every version, every parameterised shape -- and the
// spellings QMK keymaps use besides: short wrappers, mod-tap shorthands, any case and spacing.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/qmk/NazgQmkExpression.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "TestSupport.h"

#include <cstdio>
#include <string>
#include <variant>

using nazg::Keycode;
using nazg::QmkKeycodeVersion;

namespace
{
    constexpr QmkKeycodeVersion c_Version = QmkKeycodeVersion::V0_0_8;

    std::string Read(std::string_view text, QmkKeycodeVersion version = c_Version)
    {
        const std::optional<Keycode> keycode = nazg::ParseQmkExpression(text, version);
        return keycode ? nazg::FormatKeycode(*keycode) : std::string("(none)");
    }

    void TestRoundTrip()
    {
        std::printf("round trip\n");

        for (QmkKeycodeVersion version : { QmkKeycodeVersion::Legacy, QmkKeycodeVersion::V0_0_1, QmkKeycodeVersion::V0_0_9 })
        {
            int failures = 0;
            for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
            {
                if (!row.ExistsIn(version))
                    continue;
                const Keycode                keycode = nazg::DecodeQmkKeycode(row.value, version);
                const std::optional<Keycode> read    = nazg::ParseQmkExpression(nazg::FormatKeycode(keycode), version);
                if (!read || *read != keycode)
                {
                    if (++failures <= 10)
                        std::printf("    %s reads back as %s\n", nazg::FormatKeycode(keycode).c_str(),
                                    read ? nazg::FormatKeycode(*read).c_str() : "nothing");
                }
            }
            Check(failures == 0, ("every named keycode of " + std::string(nazg::QmkKeycodeVersionName(version)) +
                                  " reads back as itself").c_str());
        }

        int failures = 0;
        const auto check = [&](const Keycode& keycode)
        {
            const std::optional<Keycode> read = nazg::ParseQmkExpression(nazg::FormatKeycode(keycode), c_Version);
            if (!read || *read != keycode)
            {
                ++failures;
                std::printf("    %s reads back wrong\n", nazg::FormatKeycode(keycode).c_str());
            }
        };
        for (uint8_t layer : { 0, 1, 15, 31 })
        {
            for (int op = 0; op <= static_cast<int>(nazg::LayerOp::TapToggle); ++op)
                check(nazg::LayerKey{ static_cast<nazg::LayerOp>(op), layer });
            check(nazg::LayerTapKey{ static_cast<uint8_t>(layer & 15), "KC_SPC" });
            check(nazg::LayerModKey{ static_cast<uint8_t>(layer & 15), nazg::Mod::LeftCtrl | nazg::Mod::LeftAlt });
        }
        for (uint8_t mods = 1; mods < 16; ++mods)
            for (uint8_t sided : { mods, static_cast<uint8_t>(mods << 4) })
            {
                check(nazg::ModifiedKey{ sided, "KC_A" });
                check(nazg::ModTapKey{ sided, "KC_ESC" });
                check(nazg::OneShotModKey{ sided });
            }
        check(nazg::SwapHandsTapKey{ "KC_F" });
        check(nazg::TapDanceKey{ 12 });
        check(nazg::MacroKey{ 5 });

        // A value this version leaves unassigned stays a raw value.
        for (uint32_t value = 0x7E00; value < 0x8000; ++value)
            if (const Keycode keycode = nazg::DecodeQmkKeycode(static_cast<uint16_t>(value), c_Version);
                std::holds_alternative<nazg::UnknownKey>(keycode))
            {
                check(keycode);
                break;
            }
        Check(failures == 0, "every parameterised shape reads back as itself");
    }

    void TestSpellings()
    {
        std::printf("QMK's other spellings\n");

        Check(Read("c(s(kc_t))") == "LCTL(LSFT(KC_T))", "short wrappers, any case");
        Check(Read(" LT ( 1 , KC_SPC ) ") == "LT(1,KC_SPC)", "any spacing");
        Check(Read("LCTL_T(KC_ESC)") == "MT(MOD_LCTL,KC_ESC)" && Read("SFT_T(KC_A)") == "MT(MOD_LSFT,KC_A)",
              "mod-tap shorthands");
        Check(Read("HYPR(KC_H)") == "LCTL(LSFT(LALT(LGUI(KC_H))))" && Read("MEH_T(KC_M)") == "MT(MOD_LCTL|MOD_LSFT|MOD_LALT,KC_M)",
              "Hyper and Meh");
        Check(Read("MT(MOD_HYPR,KC_A)") == "MT(MOD_LCTL|MOD_LSFT|MOD_LALT|MOD_LGUI,KC_A)", "MOD_HYPR");
        Check(Read("_______") == "KC_TRNS" && Read("XXXXXXX") == "KC_NO", "the keymaps' blanks");
        Check(Read("mi_cs") == "MI_Cs", "a mixed-case QMK name in any case");
        Check(Read("0x0004") == "KC_A" && Read("0x7E40") == "QK_USER_0", "a raw value, decoded for the version");
    }

    void TestRefusals()
    {
        std::printf("refusals\n");

        for (const char* text : { "", "KC_NOPE", "LT(1,LCTL(KC_A))", "MT(KC_A,KC_B)", "MO(x)", "MO(1", "MO(1))",
                                  "LCTL(MO(1))", "OSM(0)", "TD(300)", "KC_A KC_B" })
            Check(!nazg::ParseQmkExpression(text, c_Version).has_value(), ("not a keycode: \"" + std::string(text) + "\"").c_str());

        Check(!nazg::ParseQmkExpression("QK_LLCK", QmkKeycodeVersion::V0_0_1).has_value(),
              "a name the board's version does not have");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRoundTrip();
    TestSpellings();
    TestRefusals();

    return TestResult();
}
