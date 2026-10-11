// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QmkSettings - Vial's QMK Settings: options QMK fixes when a firmware is built, made changeable
// on the board by vial-qmk, each a numbered setting (a qsid) the firmware lists. What each number
// means, how wide it is on the wire, and the two readings that need more than one setting:
// ui-design.md, "The QMK Settings section", from vial-qmk's quantum/qmk_settings.c.
//
// Two generations: until vial-qmk 2025-06-21, qsid 8 is one byte of four tap-hold bits; from then
// on it is gone and 22-27 take over, each a setting of its own. A board lists one or the other.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

namespace nazg
{
    namespace QmkSetting
    {
        inline constexpr uint16_t GraveEscape      = 1;    // bits: Alt, Ctrl, Gui, Shift override
        inline constexpr uint16_t ComboTerm        = 2;
        inline constexpr uint16_t AutoShift        = 3;    // bits, AutoShiftBit
        inline constexpr uint16_t AutoShiftTimeout = 4;
        inline constexpr uint16_t OneShotTapToggle = 5;
        inline constexpr uint16_t OneShotTimeout   = 6;
        inline constexpr uint16_t TappingTerm      = 7;
        inline constexpr uint16_t Tapping          = 8;    // until 2025-06: bits, TappingBit
        inline constexpr uint16_t MouseDelay       = 9;    // 9-17: mouse keys, when built in
        inline constexpr uint16_t MouseInterval    = 10;
        inline constexpr uint16_t MouseMoveDelta   = 11;
        inline constexpr uint16_t MouseMaxSpeed    = 12;
        inline constexpr uint16_t MouseTimeToMax   = 13;
        inline constexpr uint16_t WheelDelay       = 14;
        inline constexpr uint16_t WheelInterval    = 15;
        inline constexpr uint16_t WheelMaxSpeed    = 16;
        inline constexpr uint16_t WheelTimeToMax   = 17;
        inline constexpr uint16_t TapCodeDelay     = 18;
        inline constexpr uint16_t TapHoldCapsDelay = 19;
        inline constexpr uint16_t TappingToggle    = 20;
        inline constexpr uint16_t Magic            = 21;   // QMK's keymap_config, bits, MagicBit
        inline constexpr uint16_t PermissiveHold   = 22;   // from 2025-06, 22-27
        inline constexpr uint16_t HoldOnOtherKey   = 23;
        inline constexpr uint16_t RetroTapping     = 24;
        inline constexpr uint16_t QuickTapTerm     = 25;
        inline constexpr uint16_t ChordalHold      = 26;
        inline constexpr uint16_t FlowTap          = 27;
    }

    // The bits of qsid 8, as the firmware reads them (qmk_settings.c, 2023): bit 1 set means Hold
    // On Other Key Press OFF -- vial-gui's "Ignore Mod Tap Interrupt" -- and bit 2 a quick tap
    // term of 0 -- its "Tapping Force Hold".
    namespace TappingBit
    {
        inline constexpr int PermissiveHold     = 0;
        inline constexpr int NotHoldOnOtherKey  = 1;
        inline constexpr int NoQuickTap         = 2;
        inline constexpr int RetroTapping       = 3;
    }

    // The bits of qsid 3; the three "No Auto Shift" ones are turned round.
    namespace AutoShiftBit
    {
        inline constexpr int Enable        = 0;
        inline constexpr int Modifiers     = 1;
        inline constexpr int NoSpecial     = 2;
        inline constexpr int NoNumeric     = 3;
        inline constexpr int NoAlpha       = 4;
        inline constexpr int Repeat        = 5;
        inline constexpr int NoAutoRepeat  = 6;
    }

    // The bits of qsid 21, in keymap_config's order as vial-qmk packs them.
    namespace MagicBit
    {
        inline constexpr int SwapControlCapsLock  = 0;
        inline constexpr int CapsLockToControl    = 1;
        inline constexpr int SwapLeftAltGui       = 2;
        inline constexpr int SwapRightAltGui      = 3;
        inline constexpr int NoGui                = 4;
        inline constexpr int SwapGraveEscape      = 5;
        inline constexpr int SwapBackslashBksp    = 6;
        inline constexpr int Nkro                 = 7;
        inline constexpr int SwapLeftControlGui   = 8;
        inline constexpr int SwapRightControlGui  = 9;
    }

    // A setting's width on the wire, in bytes -- what GetQmkSetting() must read: the firmware writes
    // only that many, and the reply's next bytes are the request's own. 0 for a qsid Nazg does not
    // know, which is not shown.
    [[nodiscard]] size_t QmkSettingWidth(uint16_t qsid);

    // The settings as the board holds them, by qsid -- only those it lists.
    using QmkSettings = std::map<uint16_t, uint32_t>;

    [[nodiscard]] bool QmkSettingBit(const QmkSettings& settings, uint16_t qsid, int bit);

    // `value` with `bit` set or cleared.
    [[nodiscard]] uint32_t WithBit(uint32_t value, int bit, bool on);

    // What a tap-hold key does when another key is pressed while it is down -- QMK's two settings
    // are three ways in truth: both on, Hold On Other Key Press decides at that press, before
    // Permissive Hold could at its release.
    enum class HoldDecision : uint8_t
    {
        WaitForTerm,      // neither: it holds only once the tapping term has passed
        Permissive,       // Permissive Hold: another key pressed and released inside it
        OtherKeyPress,    // Hold On Other Key Press: another key pressed
    };

    [[nodiscard]] HoldDecision HoldDecisionOf(const QmkSettings& settings);

    // The settings to write for `decision`, by qsid, whichever generation the board has: qsid 8's
    // byte with the two bits, or 22 and 23.
    [[nodiscard]] QmkSettings HoldDecisionWrites(const QmkSettings& settings, HoldDecision decision);

    // What the key named `key` sends while qsid 21 holds `magic`, as QMK's keycode_config() turns
    // it -- "KC_NO" for nothing, a Gui key off -- or none when it sends itself.
    [[nodiscard]] std::optional<std::string_view> MagicKeycode(std::string_view key, uint32_t magic);

    // QMK's guess of the hand each key is on, for Chordal Hold ('L', 'R', or '*' for either --
    // keyboard_c.py, _gen_chordal_hold_layout): a board whose key centres mirror about their middle
    // splits there; one with a single key 6u wide or more splits on a line through it, leaning with
    // the rows' stagger, and the key itself is either; any other at its widest gap between key
    // centres. Positions before rotation, as QMK's layouts give them. A guess: the firmware's
    // build used its own layout, and may say a key's hand outright.
    [[nodiscard]] std::vector<char> GuessHands(const std::vector<DefinitionKey>& keys);
}
