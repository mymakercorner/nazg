// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardDescription.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <string_view>
#include <variant>

namespace nazg
{
    namespace
    {
        // The keys that print a character, whatever the host layout makes of it.
        bool IsCharacterKey(std::string_view name)
        {
            static constexpr std::string_view c_Characters[] = {
                "KC_MINS", "KC_EQL",  "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_NUHS", "KC_SCLN", "KC_QUOT",
                "KC_GRV",  "KC_COMM", "KC_DOT",  "KC_SLSH", "KC_NUBS", "KC_INT1", "KC_INT2", "KC_INT3",
                "KC_INT4", "KC_INT5", "KC_SPC",  "KC_PDOT", "KC_PCMM",
            };
            if (std::find(std::begin(c_Characters), std::end(c_Characters), name) != std::end(c_Characters))
                return true;

            // KC_A to KC_Z, KC_0 to KC_9, KC_P0 to KC_P9.
            const std::string_view rest = name.substr(name.size() > 3 ? 3 : name.size());
            if (name.rfind("KC_", 0) != 0)
                return false;
            if (rest.size() == 1)
                return (rest[0] >= 'A' && rest[0] <= 'Z') || (rest[0] >= '0' && rest[0] <= '9');
            return rest.size() == 2 && rest[0] == 'P' && rest[1] >= '0' && rest[1] <= '9';
        }

        KeyFill ClassOfNamed(std::string_view name, const DefinitionKey& key)
        {
            if (name == "KC_NO" || name == "KC_TRNS")
                return key.width > 1.25f ? KeyFill::Modifier : KeyFill::Alpha;
            if (name == "KC_ESC" || name == "KC_ENT" || name == "KC_PENT")
                return KeyFill::Accent;
            return IsCharacterKey(name) ? KeyFill::Alpha : KeyFill::Modifier;
        }
    }

    BoardDescription DescribeKeyboard(const Keyboard& keyboard)
    {
        BoardDescription board;
        for (const DefinitionKey& key : PlaceKeys(keyboard.definition, keyboard.layoutSelection))
        {
            BoardKey described;
            described.geometry = key;
            described.fill     = KeycapClassOf(keyboard.KeycodeFor(key, 0), key);
            board.keys.push_back(std::move(described));
        }
        return board;
    }

    KeyFill KeycapClassOf(const Keycode& base, const DefinitionKey& key)
    {
        constexpr uint8_t c_Shift = Mod::LeftShift | Mod::RightShift;

        if (const auto* named = std::get_if<NamedKey>(&base))
            return ClassOfNamed(named->name, key);
        if (const auto* modTap = std::get_if<ModTapKey>(&base))
            return ClassOfNamed(modTap->key, key);
        if (const auto* layerTap = std::get_if<LayerTapKey>(&base))
            return ClassOfNamed(layerTap->key, key);
        if (const auto* swapTap = std::get_if<SwapHandsTapKey>(&base))
            return ClassOfNamed(swapTap->key, key);
        if (const auto* modified = std::get_if<ModifiedKey>(&base); modified && (modified->mods & ~c_Shift) == 0)
            return ClassOfNamed(modified->key, key);
        return KeyFill::Modifier;
    }

    std::pair<float, float> KeyCentre(const DefinitionKey& key)
    {
        const float x = key.x + key.width / 2.0f;
        const float y = key.y + key.height / 2.0f;
        if (key.rotation == 0.0f)
            return { x, y };

        // Clockwise on screen, since y grows downward -- as the renderer turns it.
        const float radians = key.rotation * 3.14159265358979f / 180.0f;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        const float dx      = x - key.rotationX;
        const float dy      = y - key.rotationY;
        return { key.rotationX + dx * cosine - dy * sine, key.rotationY + dx * sine + dy * cosine };
    }

    std::vector<float> SpreadApart(const std::vector<float>& wanted, const std::vector<float>& extents, float gap)
    {
        std::vector<size_t> order(wanted.size());
        std::iota(order.begin(), order.end(), size_t{ 0 });
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return wanted[a] < wanted[b]; });

        std::vector<float> placed(wanted);
        for (size_t i = 1; i < order.size(); ++i)
        {
            const size_t previous = order[i - 1];
            const size_t current  = order[i];
            const float  nearest  = placed[previous] + (extents[previous] + extents[current]) / 2.0f + gap;
            placed[current]       = std::max(placed[current], nearest);
        }
        return placed;
    }
}
