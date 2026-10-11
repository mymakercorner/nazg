// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaLighting.h"

#include <optional>

namespace nazg
{
    namespace
    {
        // V3's value ids, the same on every lighting channel (quantum/via.h).
        constexpr uint8_t c_Brightness = 1;
        constexpr uint8_t c_Effect     = 2;
        constexpr uint8_t c_Speed      = 3;
        constexpr uint8_t c_Colour     = 4;

        // The ids before V3, which vial-qmk keeps.
        constexpr uint8_t c_LegacyBacklightBrightness = 0x09;
        constexpr uint8_t c_LegacyBacklightEffect     = 0x0A;
        constexpr uint8_t c_LegacyBrightness          = 0x80;
        constexpr uint8_t c_LegacyEffect              = 0x81;
        constexpr uint8_t c_LegacySpeed               = 0x82;
        constexpr uint8_t c_LegacyColour              = 0x83;

        // VialRGB's commands (vial-qmk quantum/vialrgb.h), in the value id's place.
        constexpr uint8_t c_VialRgbGetInfo      = 0x40;
        constexpr uint8_t c_VialRgbMode         = 0x41;   // get and set
        constexpr uint8_t c_VialRgbGetSupported = 0x42;

        std::optional<uint8_t> ChannelOf(LightingTarget target)
        {
            if (target.wire != LightingWire::Channels)
                return std::nullopt;
            switch (target.kind)
            {
            case LightingKind::Backlight: return static_cast<uint8_t>(ViaChannel::QmkBacklight);
            case LightingKind::Underglow: return static_cast<uint8_t>(ViaChannel::QmkRgbLight);
            default:                      return static_cast<uint8_t>(ViaChannel::QmkRgbMatrix);
            }
        }

        uint8_t Scale(unsigned value, unsigned from, unsigned to)
        {
            return from == 0 ? 0 : static_cast<uint8_t>((value * to + from / 2) / from);
        }

        Task<uint8_t> GetByte(ViaProtocol& via, std::optional<uint8_t> channel, uint8_t id)
        {
            co_return (co_await via.GetCustomValue(channel, id, {}, 1)).at(0);
        }
    }

    Task<VialRgbInfo> ReadVialRgbInfo(ViaProtocol& via)
    {
        const std::vector<uint8_t> reply = co_await via.GetCustomValue(std::nullopt, c_VialRgbGetInfo, {}, 3);
        co_return VialRgbInfo{ static_cast<uint16_t>(reply.at(0) | reply.at(1) << 8), reply.at(2) };
    }

    Task<std::vector<uint16_t>> ReadVialRgbEffects(ViaProtocol& via)
    {
        // Up to 15 ids a reply, each little-endian, 0xFFFF after the last: asked again from the
        // last while a reply is full.
        std::vector<uint16_t> effects;
        uint16_t              after = 0;
        for (;;)
        {
            const std::vector<uint8_t> reply = co_await via.GetCustomValue(
                std::nullopt, c_VialRgbGetSupported, { static_cast<uint8_t>(after), static_cast<uint8_t>(after >> 8) }, 30);
            size_t count = 0;
            for (size_t at = 0; at + 1 < reply.size(); at += 2)
            {
                const uint16_t id = static_cast<uint16_t>(reply[at] | reply[at + 1] << 8);
                if (id == 0xFFFF || id <= after)
                    break;
                if (id != 0)
                    effects.push_back(id);
                after = id;
                ++count;
            }
            if (count < reply.size() / 2)
                break;
        }
        co_return effects;
    }

    Task<LightingValues> ReadLighting(ViaProtocol& via, LightingTarget target, uint8_t maxBrightness)
    {
        LightingValues values;
        const auto     channel = ChannelOf(target);

        if (target.wire == LightingWire::VialRgb)
        {
            const std::vector<uint8_t> mode = co_await via.GetCustomValue(std::nullopt, c_VialRgbMode, {}, 6);
            values.effect     = static_cast<uint16_t>(mode.at(0) | mode.at(1) << 8);
            values.on         = values.effect != 0;
            values.speed      = mode.at(2);
            values.hue        = mode.at(3);
            values.saturation = mode.at(4);
            values.brightness = Scale(mode.at(5), maxBrightness, 255);
            co_return values;
        }

        const bool legacy = target.wire == LightingWire::Legacy;
        if (target.kind == LightingKind::Backlight)
        {
            values.brightness = co_await GetByte(via, channel, legacy ? c_LegacyBacklightBrightness : c_Brightness);
            values.effect     = co_await GetByte(via, channel, legacy ? c_LegacyBacklightEffect : c_Effect);
            co_return values;
        }

        values.brightness = co_await GetByte(via, channel, legacy ? c_LegacyBrightness : c_Brightness);
        values.effect     = co_await GetByte(via, channel, legacy ? c_LegacyEffect : c_Effect);
        values.speed      = co_await GetByte(via, channel, legacy ? c_LegacySpeed : c_Speed);
        const std::vector<uint8_t> colour = co_await via.GetCustomValue(channel, legacy ? c_LegacyColour : c_Colour, {}, 2);
        values.hue        = colour.at(0);
        values.saturation = colour.at(1);
        // Mainline reads 0 while off; the Legacy wire reads the mode whatever the state.
        if (!legacy)
            values.on = values.effect != 0;
        co_return values;
    }

    Task<void> WriteLighting(ViaProtocol& via, LightingTarget target, LightingLine line, const LightingValues& values,
                             uint8_t maxBrightness)
    {
        const auto channel = ChannelOf(target);
        const bool off     = values.on.has_value() && !*values.on;

        if (target.wire == LightingWire::VialRgb)
        {
            const uint16_t effect = off ? 0 : values.effect;
            co_await via.SetCustomValue(std::nullopt, c_VialRgbMode,
                                        { static_cast<uint8_t>(effect), static_cast<uint8_t>(effect >> 8), values.speed,
                                          values.hue, values.saturation, Scale(values.brightness, 255, maxBrightness) });
            co_return;
        }

        const bool legacy = target.wire == LightingWire::Legacy;
        if (target.kind == LightingKind::Backlight)
        {
            if (line == LightingLine::Brightness)
                co_await via.SetCustomValue(channel, legacy ? c_LegacyBacklightBrightness : c_Brightness, { values.brightness });
            else if (line == LightingLine::Effect)
                co_await via.SetCustomValue(channel, legacy ? c_LegacyBacklightEffect : c_Effect,
                                            { static_cast<uint8_t>(values.effect) });
            co_return;
        }

        switch (line)
        {
        case LightingLine::On:
        case LightingLine::Effect:
        {
            const uint8_t effect = off ? 0 : static_cast<uint8_t>(values.effect);
            co_await via.SetCustomValue(channel, legacy ? c_LegacyEffect : c_Effect, { effect });
            if (legacy && effect != 0)
                co_await via.SetCustomValue(channel, c_LegacyEffect, { effect });
            break;
        }
        case LightingLine::Brightness:
            co_await via.SetCustomValue(channel, legacy ? c_LegacyBrightness : c_Brightness, { values.brightness });
            break;
        case LightingLine::Speed:
            co_await via.SetCustomValue(channel, legacy ? c_LegacySpeed : c_Speed, { values.speed });
            break;
        case LightingLine::Colour:
            co_await via.SetCustomValue(channel, legacy ? c_LegacyColour : c_Colour, { values.hue, values.saturation });
            break;
        default:
            break;
        }
    }

    Task<void> SaveLighting(ViaProtocol& via, LightingTarget target)
    {
        co_await via.SaveCustomValues(ChannelOf(target));
    }
}
