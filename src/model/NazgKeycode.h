// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Keycode - what a key DOES, with no protocol and no version in sight.
//
// A raw 16-bit value means nothing without the QMK keycode version it was written for
// (0x7110 is MI_C on one board and MI_Cs1 on another), so the model never holds one
// outside UnknownKey. Protocol adapters translate at the boundary: for QMK boards that is
// adapters/qmk/NazgQmkKeycodeCodec.h, in both directions.
//
// The shapes are the ones QMK and ZMK share -- a key with modifiers held, layer actions,
// mod-tap, layer-tap, one-shot modifiers -- plus NamedKey for every fixed keycode, and
// UnknownKey as the escape hatch that keeps any value round-tripping unchanged.
// Background in docs/research_material/keycodes.md.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace nazg
{
    // Modifiers, one bit each, in the USB HID order. Wider than QMK's own 5-bit encoding,
    // which cannot mix sides: LCtrl+RShift is expressible here and not on a QMK board, so
    // the QMK encoder refuses it rather than silently dropping one of the two.
    namespace Mod
    {
        inline constexpr uint8_t LeftCtrl   = 0x01;
        inline constexpr uint8_t LeftShift  = 0x02;
        inline constexpr uint8_t LeftAlt    = 0x04;
        inline constexpr uint8_t LeftGui    = 0x08;
        inline constexpr uint8_t RightCtrl  = 0x10;
        inline constexpr uint8_t RightShift = 0x20;
        inline constexpr uint8_t RightAlt   = 0x40;
        inline constexpr uint8_t RightGui   = 0x80;
    }

    // Every fixed keycode -- KC_A, HF_TOGG, UG_TOGG, QK_BOOT -- by its short name, which
    // is its identity (see NazgQmkKeycodes.h). The name views a static keycode table, so
    // a NamedKey is cheap to copy and never dangles.
    struct NamedKey
    {
        std::string_view name;

        bool operator==(const NamedKey&) const = default;
    };

    // A key sent with modifiers held: LCTL(KC_A). `key` names a basic keycode.
    struct ModifiedKey
    {
        uint8_t          mods;
        std::string_view key;

        bool operator==(const ModifiedKey&) const = default;
    };

    // Modifiers when held, the key when tapped: MT(MOD_LCTL, KC_A).
    struct ModTapKey
    {
        uint8_t          mods;
        std::string_view key;

        bool operator==(const ModTapKey&) const = default;
    };

    // A layer when held, the key when tapped: LT(1, KC_A).
    struct LayerTapKey
    {
        uint8_t          layer;
        std::string_view key;

        bool operator==(const LayerTapKey&) const = default;
    };

    enum class LayerOp : uint8_t
    {
        Momentary,           // MO(n)  - active while held
        Toggle,              // TG(n)  - on, then off at the next press
        To,                  // TO(n)  - switch to n, turning the others off
        Default,             // DF(n)  - set the default layer until power-off
        PersistentDefault,   // PDF(n) - same, stored in EEPROM (QMK keycodes 0.0.6+)
        OneShot,             // OSL(n) - active for the next key only
        TapToggle,           // TT(n)  - momentary when held, toggle when tapped
    };

    // MO(1), TG(2), ...
    struct LayerKey
    {
        LayerOp op;
        uint8_t layer;

        bool operator==(const LayerKey&) const = default;
    };

    // A layer with modifiers held on top: LM(1, MOD_LCTL).
    struct LayerModKey
    {
        uint8_t layer;
        uint8_t mods;

        bool operator==(const LayerModKey&) const = default;
    };

    // Modifiers applied to the next key only: OSM(MOD_LSFT).
    struct OneShotModKey
    {
        uint8_t mods;

        bool operator==(const OneShotModKey&) const = default;
    };

    // Swap hands when held, the key when tapped: SH_T(KC_A).
    struct SwapHandsTapKey
    {
        std::string_view key;

        bool operator==(const SwapHandsTapKey&) const = default;
    };

    // TD(n): tap dance entry n. What it does lives in the board's tap dance table.
    struct TapDanceKey
    {
        uint8_t index;

        bool operator==(const TapDanceKey&) const = default;
    };

    // MC_n: macro n. What it types lives in the board's macro buffer.
    struct MacroKey
    {
        uint8_t index;

        bool operator==(const MacroKey&) const = default;
    };

    // Anything the decoder could not place -- unassigned, a unicode value, a range this
    // build does not understand. Kept verbatim, so writing it back changes nothing.
    struct UnknownKey
    {
        uint16_t raw;

        bool operator==(const UnknownKey&) const = default;
    };

    using Keycode = std::variant<NamedKey,
                                 ModifiedKey,
                                 ModTapKey,
                                 LayerTapKey,
                                 LayerKey,
                                 LayerModKey,
                                 OneShotModKey,
                                 SwapHandsTapKey,
                                 TapDanceKey,
                                 MacroKey,
                                 UnknownKey>;

    // QMK keymap syntax: "KC_A", "LCTL(LSFT(KC_A))", "MT(MOD_LCTL|MOD_LSFT,KC_A)",
    // "LT(1,KC_A)", "MO(1)", "TD(3)", "MC_5", "0x8123". The notation users already
    // know from keymap.c and VIA's "Any" key -- for display, logs and test expectations.
    [[nodiscard]] std::string FormatKeycode(const Keycode& keycode);
}
