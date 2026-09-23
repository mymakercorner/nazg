// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgQmkKeycodeCodec.h"

#include <variant>

namespace nazg
{
    namespace
    {
        // The QMK ranges this codec takes apart. Stable since keycode spec 0.0.1; the
        // bases and masks are QMK's own (quantum/keycodes.h, quantum/quantum_keycodes.h).
        constexpr uint16_t c_QkMods          = 0x0100;   // .. 0x1FFF  mods << 8 | kc
        constexpr uint16_t c_QkModTap        = 0x2000;   // .. 0x3FFF  mods << 8 | kc
        constexpr uint16_t c_QkLayerTap      = 0x4000;   // .. 0x4FFF  layer << 8 | kc
        constexpr uint16_t c_QkLayerMod      = 0x5000;   // .. 0x51FF  layer << 5 | mods
        constexpr uint16_t c_QkLayerOps      = 0x5200;   // .. 0x52FF  eight 32-value blocks
        constexpr uint16_t c_QkSwapHands     = 0x5600;   // .. 0x56EF  kc; 0x56F0+ are fixed
        constexpr uint16_t c_QkTapDance      = 0x5700;   // .. 0x57FF  index
        constexpr uint16_t c_QkMacro         = 0x7700;   // .. 0x777F  index

        constexpr uint8_t  c_SwapHandsFixed  = 0xF0;     // SH_TOGG and friends start here

        // The eight blocks of 0x5200, in order. OSM sits among them but carries mods, not
        // a layer, so it is decoded separately.
        enum class LayerBlock : uint8_t { To, Momentary, Default, Toggle, OneShotLayer, OneShotMod, TapToggle, PersistentDefault };

        // QMK packs modifiers into 5 bits: four for ctrl/shift/alt/gui and a fifth that
        // says "these are the right-hand ones". Hence no way to mix sides.
        std::optional<uint8_t> ModsFromQmk(uint8_t qmkMods) noexcept
        {
            const uint8_t which = qmkMods & 0x0F;
            if (which == 0)
                return std::nullopt;

            return (qmkMods & 0x10) != 0 ? static_cast<uint8_t>(which << 4) : which;
        }

        std::optional<uint8_t> ModsToQmk(uint8_t mods) noexcept
        {
            const uint8_t left  = mods & 0x0F;
            const uint8_t right = mods >> 4;

            if ((left == 0) == (right == 0))   // none at all, or both sides at once
                return std::nullopt;

            return right != 0 ? static_cast<uint8_t>(0x10 | right) : left;
        }

        // The basic keycode a wrapper holds in its low byte, by name. nullptr if that
        // byte is not a keycode in this version.
        const char* BasicKeyName(uint16_t value, QmkKeycodeVersion version) noexcept
        {
            const QmkKeycode* keycode = FindQmkKeycode(value & 0xFF, version);
            return keycode != nullptr ? keycode->name : nullptr;
        }

        std::optional<uint8_t> BasicKeyValue(std::string_view name, QmkKeycodeVersion version) noexcept
        {
            const QmkKeycode* keycode = FindQmkKeycodeByName(name, version);
            if (keycode == nullptr || keycode->value > 0xFF)
                return std::nullopt;

            return static_cast<uint8_t>(keycode->value);
        }

        Keycode DecodeLayerOps(uint16_t value, QmkKeycodeVersion version)
        {
            const auto    block = static_cast<LayerBlock>((value >> 5) & 0x07);
            const uint8_t low   = value & 0x1F;

            switch (block)
            {
            case LayerBlock::To:           return LayerKey{ LayerOp::To, low };
            case LayerBlock::Momentary:    return LayerKey{ LayerOp::Momentary, low };
            case LayerBlock::Default:      return LayerKey{ LayerOp::Default, low };
            case LayerBlock::Toggle:       return LayerKey{ LayerOp::Toggle, low };
            case LayerBlock::OneShotLayer: return LayerKey{ LayerOp::OneShot, low };
            case LayerBlock::TapToggle:    return LayerKey{ LayerOp::TapToggle, low };

            case LayerBlock::OneShotMod:
                if (const auto mods = ModsFromQmk(low))
                    return OneShotModKey{ *mods };
                break;

            case LayerBlock::PersistentDefault:
                if (version >= QmkKeycodeVersion::V0_0_6)
                    return LayerKey{ LayerOp::PersistentDefault, low };
                break;
            }

            return UnknownKey{ value };
        }

        // Everything below 0x5800 that is not a fixed keycode.
        Keycode DecodeRange(uint16_t value, QmkKeycodeVersion version)
        {
            if (value >= c_QkMods && value < c_QkLayerMod)
            {
                const char* key = BasicKeyName(value, version);
                if (key == nullptr)
                    return UnknownKey{ value };

                if (value >= c_QkLayerTap)
                    return LayerTapKey{ static_cast<uint8_t>((value >> 8) & 0x0F), key };

                const auto mods = ModsFromQmk((value >> 8) & 0x1F);
                if (!mods)
                    return UnknownKey{ value };

                if (value >= c_QkModTap)
                    return ModTapKey{ *mods, key };

                return ModifiedKey{ *mods, key };
            }

            if (value >= c_QkLayerMod && value < c_QkLayerOps)
            {
                if (const auto mods = ModsFromQmk(value & 0x1F))
                    return LayerModKey{ static_cast<uint8_t>((value >> 5) & 0x0F), *mods };

                return UnknownKey{ value };
            }

            if (value >= c_QkLayerOps && value < c_QkLayerOps + 0x100)
                return DecodeLayerOps(value, version);

            if (value >= c_QkSwapHands && value < c_QkSwapHands + c_SwapHandsFixed)
            {
                if (const char* key = BasicKeyName(value, version))
                    return SwapHandsTapKey{ key };

                return UnknownKey{ value };
            }

            if (value >= c_QkTapDance && value < c_QkTapDance + 0x100)
                return TapDanceKey{ static_cast<uint8_t>(value & 0xFF) };

            return UnknownKey{ value };
        }

        // One overload per Keycode alternative; std::visit picks the right one.
        struct Encoder
        {
            QmkKeycodeVersion version;

            std::optional<uint16_t> operator()(const NamedKey& k) const
            {
                const QmkKeycode* keycode = FindQmkKeycodeByName(k.name, version);
                if (keycode == nullptr)
                    return std::nullopt;

                return keycode->value;
            }

            std::optional<uint16_t> operator()(const ModifiedKey& k) const { return Wrap(0x0000, k.mods, k.key); }

            std::optional<uint16_t> operator()(const ModTapKey& k) const { return Wrap(c_QkModTap, k.mods, k.key); }

            std::optional<uint16_t> operator()(const LayerTapKey& k) const
            {
                const auto key = BasicKeyValue(k.key, version);
                if (!key || k.layer > 0x0F)
                    return std::nullopt;

                return static_cast<uint16_t>(c_QkLayerTap | (k.layer << 8) | *key);
            }

            std::optional<uint16_t> operator()(const LayerKey& k) const
            {
                if (k.layer > 0x1F)
                    return std::nullopt;

                LayerBlock block = LayerBlock::To;
                switch (k.op)
                {
                case LayerOp::To:        block = LayerBlock::To;           break;
                case LayerOp::Momentary: block = LayerBlock::Momentary;    break;
                case LayerOp::Default:   block = LayerBlock::Default;      break;
                case LayerOp::Toggle:    block = LayerBlock::Toggle;       break;
                case LayerOp::OneShot:   block = LayerBlock::OneShotLayer; break;
                case LayerOp::TapToggle: block = LayerBlock::TapToggle;    break;

                case LayerOp::PersistentDefault:
                    if (version < QmkKeycodeVersion::V0_0_6)
                        return std::nullopt;
                    block = LayerBlock::PersistentDefault;
                    break;
                }

                return static_cast<uint16_t>(c_QkLayerOps | (static_cast<uint16_t>(block) << 5) | k.layer);
            }

            std::optional<uint16_t> operator()(const LayerModKey& k) const
            {
                const auto mods = ModsToQmk(k.mods);
                if (!mods || k.layer > 0x0F)
                    return std::nullopt;

                return static_cast<uint16_t>(c_QkLayerMod | (k.layer << 5) | *mods);
            }

            std::optional<uint16_t> operator()(const OneShotModKey& k) const
            {
                const auto mods = ModsToQmk(k.mods);
                if (!mods)
                    return std::nullopt;

                return static_cast<uint16_t>(c_QkLayerOps | (static_cast<uint16_t>(LayerBlock::OneShotMod) << 5) | *mods);
            }

            std::optional<uint16_t> operator()(const SwapHandsTapKey& k) const
            {
                const auto key = BasicKeyValue(k.key, version);
                if (!key || *key >= c_SwapHandsFixed)
                    return std::nullopt;

                return static_cast<uint16_t>(c_QkSwapHands | *key);
            }

            std::optional<uint16_t> operator()(const TapDanceKey& k) const
            {
                return static_cast<uint16_t>(c_QkTapDance | k.index);
            }

            std::optional<uint16_t> operator()(const MacroKey& k) const
            {
                if (k.index > 0x7F)
                    return std::nullopt;

                return static_cast<uint16_t>(c_QkMacro | k.index);
            }

            std::optional<uint16_t> operator()(const UnknownKey& k) const { return k.raw; }

        private:
            // LCTL(kc) and MT(mods, kc) share a layout; only the base differs.
            std::optional<uint16_t> Wrap(uint16_t base, uint8_t mods, std::string_view name) const
            {
                const auto qmkMods = ModsToQmk(mods);
                const auto key     = BasicKeyValue(name, version);
                if (!qmkMods || !key)
                    return std::nullopt;

                return static_cast<uint16_t>(base | (*qmkMods << 8) | *key);
            }
        };
    }

    Keycode DecodeQmkKeycode(uint16_t value, QmkKeycodeVersion version)
    {
        // Macros first: MC_0..MC_31 are table entries too, but a macro is a macro by its
        // index, whatever the table names it -- and indices past 31 have no name at all.
        if (value >= c_QkMacro && value < c_QkMacro + 0x80)
            return MacroKey{ static_cast<uint8_t>(value & 0x7F) };

        if (const QmkKeycode* keycode = FindQmkKeycode(value, version))
            return NamedKey{ keycode->name };

        return DecodeRange(value, version);
    }

    std::optional<uint16_t> EncodeQmkKeycode(const Keycode& keycode, QmkKeycodeVersion version)
    {
        return std::visit(Encoder{ version }, keycode);
    }
}
