// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgAltRepeatKey.h"

#include <array>
#include <utility>

namespace nazg
{
    namespace
    {
        bool IsNamed(const Keycode& keycode, std::string_view name)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named != nullptr && named->name == name;
        }

        // Right Ctrl and Left Ctrl alike: the modifiers by kind, on the left bits.
        uint8_t Folded(uint8_t mods)
        {
            return static_cast<uint8_t>((mods | (mods >> 4)) & 0x0F);
        }
    }

    bool AltRepeatKey::IsEmpty() const
    {
        return !lastKey && !altKey && allowed == 0;
    }

    AltRepeatKey NewAltRepeatKey()
    {
        AltRepeatKey fresh;
        fresh.options = AltRepeatOption::Enabled | AltRepeatOption::IgnoreHandedness;
        return fresh;
    }

    BaseKey BaseOf(const Keycode& keycode)
    {
        if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
            return { NamedKey{ modified->key }, modified->mods };
        if (const auto* modTap = std::get_if<ModTapKey>(&keycode))
            return { NamedKey{ modTap->key }, 0 };
        if (const auto* layerTap = std::get_if<LayerTapKey>(&keycode))
            return { NamedKey{ layerTap->key }, 0 };
        return { keycode, 0 };
    }

    std::optional<std::string_view> NeverRemembered(const Keycode& keycode)
    {
        static constexpr std::array<std::string_view, 8> c_Modifiers = { "KC_LCTL", "KC_LSFT", "KC_LALT", "KC_LGUI",
                                                                          "KC_RCTL", "KC_RSFT", "KC_RALT", "KC_RGUI" };
        for (std::string_view name : c_Modifiers)
            if (IsNamed(keycode, name))
                return "a modifier";

        // Hyper and Meh are modifiers sent with no key: LCTL(LSFT(LALT(LGUI(KC_NO)))).
        if (const auto* modified = std::get_if<ModifiedKey>(&keycode); modified != nullptr && modified->key == "KC_NO")
        {
            const uint8_t meh = Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt;
            if (modified->mods == meh || modified->mods == (meh | Mod::LeftGui))
                return "a modifier";
        }

        if (const auto* layer = std::get_if<LayerKey>(&keycode))
            switch (layer->op)
            {
            case LayerOp::Momentary:
            case LayerOp::Toggle:
            case LayerOp::To:
            case LayerOp::TapToggle:
            case LayerOp::OneShot: return "a layer key";
            case LayerOp::Default:
            case LayerOp::PersistentDefault: break;
            }
        if (std::holds_alternative<OneShotModKey>(keycode))
            return "a one-shot modifier";
        if (IsNamed(keycode, "TL_LOWR") || IsNamed(keycode, "TL_UPPR"))
            return "a layer key";
        if (IsNamed(keycode, "QK_LLCK"))
            return "Layer Lock";
        if (IsRepeatKey(keycode))
            return "a Repeat key";
        return std::nullopt;
    }

    bool IsRepeatKey(const Keycode& keycode)
    {
        return IsNamed(keycode, "QK_REP") || IsNamed(keycode, "QK_AREP");
    }

    bool SentWrong(const Keycode& keycode)
    {
        const auto* modified = std::get_if<ModifiedKey>(&keycode);
        return modified != nullptr && (modified->mods & 0xF0) != 0;
    }

    Keycode WithLeftModifiers(const Keycode& keycode)
    {
        if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
            return ModifiedKey{ Folded(modified->mods), modified->key };
        return keycode;
    }

    bool SameLastKey(const AltRepeatKey& first, const AltRepeatKey& second)
    {
        if (!first.IsOn() || !second.IsOn() || !first.lastKey || !second.lastKey)
            return false;
        const BaseKey a = BaseOf(*first.lastKey), b = BaseOf(*second.lastKey);
        if (a.key != b.key)
            return false;
        const bool alike = first.Has(AltRepeatOption::IgnoreHandedness) || second.Has(AltRepeatOption::IgnoreHandedness);
        return alike ? Folded(a.mods) == Folded(b.mods) : a.mods == b.mods;
    }

    bool QmkPairs(const Keycode& first, const Keycode& second)
    {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> c_Pairs = { {
            { "KC_LEFT", "KC_RGHT" },
            { "KC_UP", "KC_DOWN" },
            { "KC_HOME", "KC_END" },
            { "KC_PGUP", "KC_PGDN" },
            { "KC_BSPC", "KC_DEL" },
            { "KC_LBRC", "KC_RBRC" },
        } };
        for (const auto& [one, other] : c_Pairs)
            if ((IsNamed(first, one) && IsNamed(second, other)) || (IsNamed(first, other) && IsNamed(second, one)))
                return true;
        return false;
    }
}
