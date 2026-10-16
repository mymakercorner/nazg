// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLighting.h"

#include <algorithm>
#include <array>

namespace nazg
{
    namespace
    {
        using enum ColourUse;

        // QMK's underglow modes with every effect built (rgblight_modes.h), and what each variant
        // is (rgblight.c, 2026): Breathing, Rainbow Mood and Knight by speed; Rainbow Swirl and
        // Snake by speed (offset / 2) and direction (offset % 2); Gradient by spread (offset / 2,
        // RGBLED_GRADIENT_RANGES) and direction; Twinkle by speed (offset % 3) and random colours
        // (offset / 3). Every interval table runs from the slowest.
        constexpr std::array<UnderglowEffect, 11> c_Underglow{ {
            { .name = "Solid Color", .first = 1 },
            { .name = "Breathing", .first = 2, .count = 4, .speeds = 4 },
            { .name = "Rainbow Mood", .first = 6, .count = 3, .speeds = 3, .colour = Saturation },
            { .name = "Rainbow Swirl", .first = 9, .count = 6, .speeds = 3, .directions = true, .colour = Saturation },
            { .name = "Snake", .first = 15, .count = 6, .speeds = 3, .directions = true },
            { .name = "Knight", .first = 21, .count = 3, .speeds = 3 },
            { .name = "Christmas", .first = 24, .colour = Saturation },
            { .name = "Gradient", .first = 25, .count = 10, .spreads = 5, .directions = true },
            { .name = "RGB Test", .first = 35, .colour = None },
            { .name = "Alternating", .first = 36 },
            { .name = "Twinkle", .first = 37, .count = 6, .speeds = 3, .randomColours = true },
        } };

        UnderglowVariant Decode(const UnderglowEffect& effect, uint8_t offset)
        {
            UnderglowVariant variant;
            if (effect.randomColours)
            {
                variant.speed  = static_cast<uint8_t>(offset % 3);
                variant.random = offset / 3 != 0;
            }
            else if (effect.spreads != 0)
            {
                variant.spread    = static_cast<uint8_t>(offset / 2);
                variant.direction = static_cast<uint8_t>(offset % 2);
            }
            else if (effect.directions)
            {
                variant.speed     = static_cast<uint8_t>(offset / 2);
                variant.direction = static_cast<uint8_t>(offset % 2);
            }
            else if (effect.speeds != 0)
                variant.speed = offset;
            return variant;
        }

        // VIA's names for QMK's RGB Matrix modes (@the-via/reader 1.14.4, qmk_rgb_matrix), what
        // each takes of the colour from QMK's rgb_matrix/animations (2026).
        constexpr std::array<RgbMatrixEffect, 44> c_ViaRgbMatrix{ {
            { 1, "Solid Color" },
            { 2, "Alphas Mods" },
            { 3, "Gradient Up/Down" },
            { 4, "Gradient Left/Right" },
            { 5, "Breathing" },
            { 6, "Band Sat." },
            { 7, "Band Val." },
            { 8, "Pinwheel Sat." },
            { 9, "Pinwheel Val." },
            { 10, "Spiral Sat." },
            { 11, "Spiral Val." },
            { 12, "Cycle All", Saturation },
            { 13, "Cycle Left/Right", Saturation },
            { 14, "Cycle Up/Down", Saturation },
            { 15, "Rainbow Moving Chevron", Saturation },
            { 16, "Cycle Out/In", Saturation },
            { 17, "Cycle Out/In Dual", Saturation },
            { 18, "Cycle Pinwheel", Saturation },
            { 19, "Cycle Spiral", Saturation },
            { 20, "Dual Beacon" },
            { 21, "Rainbow Beacon", Saturation },
            { 22, "Rainbow Pinwheels", Saturation },
            { 23, "Raindrops" },
            { 24, "Jellybean Raindrops", None },
            { 25, "Hue Breathing" },
            { 26, "Hue Pendulum" },
            { 27, "Hue Wave" },
            { 28, "Pixel Rain", None },
            { 29, "Pixel Flow", None },
            { 30, "Pixel Fractal" },
            { 31, "Typing Heatmap", Saturation },
            { 32, "Digital Rain", None },
            { 33, "Solid Reactive Simple" },
            { 34, "Solid Reactive" },
            { 35, "Solid Reactive Wide" },
            { 36, "Solid Reactive Multi Wide" },
            { 37, "Solid Reactive Cross" },
            { 38, "Solid Reactive Multi Cross" },
            { 39, "Solid Reactive Nexus" },
            { 40, "Solid Reactive Multi Nexus" },
            { 41, "Splash" },
            { 42, "Multi Splash" },
            { 43, "Solid Splash" },
            { 44, "Solid Multi Splash" },
        } };

        // VialRGB's ids (vial-qmk quantum/vialrgb_effects.inc -- never renumbered, only added to),
        // vial-gui's names.
        constexpr std::array<RgbMatrixEffect, 43> c_VialRgb{ {
            { 2, "Solid Color" },
            { 3, "Alphas Mods" },
            { 4, "Gradient Up Down" },
            { 5, "Gradient Left Right" },
            { 6, "Breathing" },
            { 7, "Band Sat" },
            { 8, "Band Val" },
            { 9, "Band Pinwheel Sat" },
            { 10, "Band Pinwheel Val" },
            { 11, "Band Spiral Sat" },
            { 12, "Band Spiral Val" },
            { 13, "Cycle All", Saturation },
            { 14, "Cycle Left Right", Saturation },
            { 15, "Cycle Up Down", Saturation },
            { 16, "Rainbow Moving Chevron", Saturation },
            { 17, "Cycle Out In", Saturation },
            { 18, "Cycle Out In Dual", Saturation },
            { 19, "Cycle Pinwheel", Saturation },
            { 20, "Cycle Spiral", Saturation },
            { 21, "Dual Beacon" },
            { 22, "Rainbow Beacon", Saturation },
            { 23, "Rainbow Pinwheels", Saturation },
            { 24, "Raindrops" },
            { 25, "Jellybean Raindrops", None },
            { 26, "Hue Breathing" },
            { 27, "Hue Pendulum" },
            { 28, "Hue Wave" },
            { 29, "Typing Heatmap", Saturation },
            { 30, "Digital Rain", None },
            { 31, "Solid Reactive Simple" },
            { 32, "Solid Reactive" },
            { 33, "Solid Reactive Wide" },
            { 34, "Solid Reactive Multiwide" },
            { 35, "Solid Reactive Cross" },
            { 36, "Solid Reactive Multicross" },
            { 37, "Solid Reactive Nexus" },
            { 38, "Solid Reactive Multinexus" },
            { 39, "Splash" },
            { 40, "Multisplash" },
            { 41, "Solid Splash" },
            { 42, "Solid Multisplash" },
            { 43, "Pixel Rain", None },
            { 44, "Pixel Fractal" },
        } };

        bool Contains(std::span<const std::string> ids, std::string_view id)
        {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        bool StartsWith(std::string_view text, std::string_view start)
        {
            return text.substr(0, start.size()) == start;
        }

        bool EndsWithAny(std::string_view text, std::initializer_list<std::string_view> ends)
        {
            for (std::string_view end : ends)
                if (text.size() >= end.size() && text.substr(text.size() - end.size()) == end)
                    return true;
            return false;
        }
    }

    std::vector<LightingTarget> LightingTargetsOf(const Keyboard& keyboard)
    {
        const via::KeyboardDefinition& definition = keyboard.definition;
        const std::string&             preset     = definition.lighting;
        const auto                     declares   = [&](std::string_view id) { return preset == id || Contains(definition.menuIds, id); };

        const bool backlight = declares("qmk_backlight") || declares("qmk_backlight_rgblight");
        const bool underglow = declares("qmk_rgblight") || declares("qmk_backlight_rgblight");
        const bool vialRgb   = preset == "vialrgb";
        const bool matrix    = Contains(definition.menuIds, "qmk_rgb_matrix");

        // vial-qmk always reports VIA 9 and speaks the format before V3; so does mainline before 11.
        const bool         channels = !keyboard.report.isVial && keyboard.report.viaProtocol >= 11;
        const LightingWire wire     = channels ? LightingWire::Channels : LightingWire::Legacy;

        std::vector<LightingTarget> targets;
        if (backlight)
            targets.push_back({ LightingKind::Backlight, wire });
        if (underglow)
            targets.push_back({ LightingKind::Underglow, wire });
        if (vialRgb)
            targets.push_back({ LightingKind::RgbMatrix, LightingWire::VialRgb });
        else if (matrix && channels)
            targets.push_back({ LightingKind::RgbMatrix, LightingWire::Channels });
        return targets;
    }

    std::string_view NameOf(LightingKind kind)
    {
        switch (kind)
        {
        case LightingKind::Backlight: return "Backlight";
        case LightingKind::Underglow: return "Underglow";
        default:                      return "RGB Matrix";
        }
    }

    std::string_view WordsOf(LightingKind kind)
    {
        switch (kind)
        {
        case LightingKind::Backlight: return "one colour, under each key";
        case LightingKind::Underglow: return "the LEDs under the board";
        default:                      return "a colour LED in each key";
        }
    }

    std::span<const UnderglowEffect> UnderglowEffects() noexcept
    {
        return c_Underglow;
    }

    const UnderglowEffect* UnderglowEffectOf(uint16_t mode) noexcept
    {
        for (const UnderglowEffect& effect : c_Underglow)
            if (mode >= effect.first && mode < effect.first + effect.count)
                return &effect;
        return nullptr;
    }

    UnderglowVariant UnderglowVariantOf(uint16_t mode) noexcept
    {
        const UnderglowEffect* effect = UnderglowEffectOf(mode);
        return effect ? Decode(*effect, static_cast<uint8_t>(mode - effect->first)) : UnderglowVariant{};
    }

    uint16_t UnderglowMode(const UnderglowEffect& effect, UnderglowVariant variant) noexcept
    {
        for (uint8_t offset = 0; offset < effect.count; ++offset)
            if (Decode(effect, offset) == variant)
                return static_cast<uint16_t>(effect.first + offset);
        return effect.first;
    }

    ColourUse UnderglowColourUse(uint16_t mode) noexcept
    {
        const UnderglowEffect* effect = UnderglowEffectOf(mode);
        if (!effect)
            return ColourUse::HueAndSaturation;
        // Random twinkles pick their hues, and take the saturation as a range.
        if (effect->randomColours && UnderglowVariantOf(mode).random)
            return ColourUse::Saturation;
        return effect->colour;
    }

    std::span<const RgbMatrixEffect> RgbMatrixEffects(LightingWire wire) noexcept
    {
        if (wire == LightingWire::VialRgb)
            return c_VialRgb;
        return c_ViaRgbMatrix;
    }

    const RgbMatrixEffect* RgbMatrixEffectOf(LightingWire wire, uint16_t value) noexcept
    {
        for (const RgbMatrixEffect& effect : RgbMatrixEffects(wire))
            if (effect.value == value)
                return &effect;
        return nullptr;
    }

    bool ChangesLighting(std::string_view keycode, LightingKind kind, LightingLine line, bool boardHasUnderglow) noexcept
    {
        // The keycode's system first.
        bool ours = false;
        switch (kind)
        {
        case LightingKind::Backlight:
            ours = StartsWith(keycode, "BL_");
            break;
        case LightingKind::Underglow:
            ours = StartsWith(keycode, "UG_") || StartsWith(keycode, "RGB_M_");
            break;
        case LightingKind::RgbMatrix:
            ours = StartsWith(keycode, "RM_") || (!boardHasUnderglow && StartsWith(keycode, "UG_"));
            break;
        }
        if (!ours)
            return false;

        switch (line)
        {
        case LightingLine::On:
            return EndsWithAny(keycode, { "_TOGG", "_ON", "_OFF" });
        case LightingLine::Effect:
            return EndsWithAny(keycode, { "_NEXT", "_PREV", "_BRTG" }) || StartsWith(keycode, "RGB_M_");
        case LightingLine::Brightness:
            return EndsWithAny(keycode, { "_VALU", "_VALD", "_UP", "_DOWN", "_STEP" });
        case LightingLine::Speed:
            return EndsWithAny(keycode, { "_SPDU", "_SPDD" });
        case LightingLine::Colour:
            return EndsWithAny(keycode, { "_HUEU", "_HUED", "_SATU", "_SATD" });
        default:
            return true;
        }
    }
}
