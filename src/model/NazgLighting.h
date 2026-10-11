// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Lighting - QMK's own lighting systems as the Lighting section edits them (ui-design.md, "The
// Lighting section"): backlight, underglow (QMK's rgblight) and RGB Matrix, as VIA's built-in
// menus and Vial's `lighting` presets declare them. A board's own lighting menu is a custom menu,
// not this.
//
// Three ways a firmware is spoken to about them (via-vial-commands.md, "Lighting"), and the
// effects as QMK numbers them -- among those built in: an effect left out of a build moves every
// later number, so the names here are right only for a firmware with every effect built, which
// the board cannot say. VialRGB alone has fixed effect ids, and lists those the board has.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "model/NazgKeyboard.h"

namespace nazg
{
    enum class LightingKind : uint8_t
    {
        Backlight,   // one colour, under each key: brightness steps, breathing
        Underglow,   // QMK's rgblight
        RgbMatrix,   // a colour LED in each key
    };

    enum class LightingWire : uint8_t
    {
        // Mainline VIA from protocol 11 (V3's custom channels): [channel][value id][value], ids 1
        // to 4, brightness scaled to 0-255, the effect 0 while the light is off.
        Channels,

        // VIA 9 and 10, and every vial-qmk: no channel byte, ids 0x09-0x0A (backlight) and
        // 0x80-0x83 (underglow); the underglow's brightness as QMK holds it, its effect read even
        // while off -- so whether it is on cannot be read -- and an effect written while off
        // ignored: the light turns on with the one it had.
        Legacy,

        // vial-qmk's VialRGB, for RGB Matrix: commands 0x40 and up where a value id would be,
        // the effect, speed and colour set together, fixed effect ids.
        VialRgb,
    };

    struct LightingTarget
    {
        LightingKind kind = LightingKind::Underglow;
        LightingWire wire = LightingWire::Channels;

        bool operator==(const LightingTarget&) const = default;
    };

    // The systems a board's Lighting section edits, in the strip's order -- backlight, underglow,
    // RGB Matrix: those VIA V3's standard menus or Vial's and VIA V2's `lighting` preset declare,
    // spoken to by the board's protocol. None for one it cannot be spoken to about (vial-qmk's
    // RGB Matrix without VialRGB).
    [[nodiscard]] std::vector<LightingTarget> LightingTargetsOf(const Keyboard& keyboard);

    // The words for a system: "Underglow", and what it is.
    [[nodiscard]] std::string_view NameOf(LightingKind kind);
    [[nodiscard]] std::string_view WordsOf(LightingKind kind);

    // What a board says of one system. Brightness, speed, hue and saturation 0-255.
    struct LightingValues
    {
        // Whether it is lit; none when the firmware cannot say (Legacy underglow). The backlight
        // has none either: off is brightness 0.
        std::optional<bool> on;

        // Underglow: QMK's mode, 1 and up. RGB Matrix: QMK's mode (Channels) or a VialRGB id.
        // Backlight: breathing, 0 or 1. 0 while off, on a wire that says so.
        uint16_t effect = 0;

        uint8_t brightness = 0;
        uint8_t speed      = 0;
        uint8_t hue        = 0;
        uint8_t saturation = 0;

        bool operator==(const LightingValues&) const = default;
    };

    // What an effect takes from the chosen colour: hue and saturation, saturation alone -- a
    // rainbow, a cycle -- or neither: fixed colours, random ones.
    enum class ColourUse : uint8_t
    {
        None,
        Saturation,
        HueAndSaturation,
    };

    // Underglow's numbered modes are effects and their variants (rgblight_modes.h, rgblight.c):
    // Breathing 1 to 4 are four speeds, Snake 1 to 6 three speeds each way, Gradient 1 to 10 five
    // spreads each way, Twinkle 1 to 6 three speeds of one colour or random. Underglow stores a
    // speed too, and no underglow effect reads it: the speed is the variant.
    struct UnderglowEffect
    {
        std::string_view name;
        uint8_t          first = 0;   // its first mode, with every effect built
        uint8_t          count = 1;
        uint8_t          speeds  = 0;       // 0: none
        uint8_t          spreads = 0;       // Gradient: 5
        bool             directions    = false;
        bool             randomColours = false;
        ColourUse        colour        = ColourUse::HueAndSaturation;
    };

    struct UnderglowVariant
    {
        uint8_t speed     = 0;   // 0 the slowest
        uint8_t spread    = 0;   // 0 the whole hue circle, then 2/3, 1/2, 1/3, 1/4
        uint8_t direction = 0;
        bool    random    = false;

        bool operator==(const UnderglowVariant&) const = default;
    };

    [[nodiscard]] std::span<const UnderglowEffect> UnderglowEffects() noexcept;

    // The effect a mode is a variant of, and which; none for 0 or a mode past the last.
    [[nodiscard]] const UnderglowEffect* UnderglowEffectOf(uint16_t mode) noexcept;
    [[nodiscard]] UnderglowVariant       UnderglowVariantOf(uint16_t mode) noexcept;

    // The mode of an effect's variant -- a variant it lacks read as its first.
    [[nodiscard]] uint16_t UnderglowMode(const UnderglowEffect& effect, UnderglowVariant variant) noexcept;

    [[nodiscard]] ColourUse UnderglowColourUse(uint16_t mode) noexcept;

    // One RGB Matrix effect: its number on the wire and its name.
    struct RgbMatrixEffect
    {
        uint16_t         value = 0;
        std::string_view name;
        ColourUse        colour = ColourUse::HueAndSaturation;
    };

    // Channels: QMK's modes as VIA names them, with every effect built (@the-via/reader's
    // qmk_rgb_matrix), 1 and up. VialRgb: every VialRGB id from 2 -- 0 is off, 1 the direct mode
    // a host streams colours to, never offered -- of which a board lists those it has.
    [[nodiscard]] std::span<const RgbMatrixEffect> RgbMatrixEffects(LightingWire wire) noexcept;

    // The effect named `value` on `wire`; none when unknown.
    [[nodiscard]] const RgbMatrixEffect* RgbMatrixEffectOf(LightingWire wire, uint16_t value) noexcept;

    // The QMK keycodes that change a system from the keyboard, by what they change -- the
    // section's lines. Keycode names as Nazg decodes them (UG_*, RM_*, BL_*, RGB_M_*). On a board
    // with RGB Matrix and no underglow, UG_* drive the matrix (ui-design.md, "What a lighting
    // keycode drives"), so they count for it.
    enum class LightingLine : uint8_t
    {
        Any,          // every key of the system
        On,
        Effect,
        Brightness,
        Speed,
        Colour,
    };
    [[nodiscard]] bool ChangesLighting(std::string_view keycode, LightingKind kind, LightingLine line,
                                       bool boardHasUnderglow) noexcept;
}
