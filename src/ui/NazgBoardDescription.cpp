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

    Legend& BoardKey::operator[](LegendSlot slot)
    {
        if (!std::holds_alternative<SlotLegends>(legends))
            legends = SlotLegends{};
        return std::get<SlotLegends>(legends)[static_cast<size_t>(slot)];
    }

    void DescribeLegends(BoardDescription& board, const Keyboard& keyboard, uint8_t layer,
                         const LegendSettings& settings)
    {
        const float   line     = SideLine(board.keys);
        const uint8_t lighting = LightingSystemsOf(keyboard);

        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;

            const LegendContext context{ settings.Layout(), settings.modifierNames, SideOf(key.geometry, line),
                                         lighting };
            key.legends = LegendFor(keyboard.KeycodeFor(key.geometry, layer), context);
        }
    }

    uint8_t LightingSystemsOf(const Keyboard& keyboard)
    {
        using namespace LightingSystem;

        const struct
        {
            std::string_view id;
            uint8_t          systems;
        } c_Ids[] = {
            // Vial and VIA V2 presets; Vial's RGB Matrix is VialRGB.
            { "qmk_backlight", Backlight },
            { "qmk_rgblight", Underglow },
            { "qmk_backlight_rgblight", Backlight | Underglow },
            { "vialrgb", RgbMatrix },
            // VIA V3's standard menus, and its keycode modules.
            { "qmk_rgb_matrix", RgbMatrix },
            { "qmk_backlight_keycodes", Backlight },
            { "qmk_rgblight_keycodes", Underglow },
            { "qmk_rgb_matrix_keycodes", RgbMatrix },
            { "qmk_backlight_rgblight_keycodes", Backlight | Underglow },
        };

        const KeyboardDefinition& definition = keyboard.definition;
        uint8_t                   systems    = 0;
        const auto                add        = [&](std::string_view id)
        {
            for (const auto& known : c_Ids)
                if (known.id == id)
                    systems |= known.systems;
        };

        add(definition.lighting);
        for (const std::string& id : definition.keycodeModules)
            add(id);
        for (const std::string& id : definition.menuIds)
            add(id);

        const Keymap& keymap = keyboard.keymap;
        for (uint8_t layer = 0; layer < keymap.Layers(); ++layer)
            for (uint8_t row = 0; row < keymap.Rows(); ++row)
                for (uint8_t column = 0; column < keymap.Columns(); ++column)
                    if (const auto* named = std::get_if<NamedKey>(&keymap.At(layer, row, column));
                        named != nullptr && named->name.substr(0, 3) == "LM_")
                        systems |= LedMatrix;

        return systems;
    }

    float SideLine(const std::vector<BoardKey>& keys)
    {
        const BoardKey* widest = nullptr;
        float           minX   = 0.0f;
        float           maxX   = 0.0f;
        bool            any    = false;

        for (const BoardKey& key : keys)
        {
            if (key.geometry.decal)
                continue;

            const DefinitionKey& geometry = key.geometry;
            if (widest == nullptr || geometry.width > widest->geometry.width)
                widest = &key;

            const float centre = KeyCentre(geometry).first;
            const float half   = geometry.width / 2.0f;
            minX               = any ? std::min(minX, centre - half) : centre - half;
            maxX               = any ? std::max(maxX, centre + half) : centre + half;
            any                = true;
        }

        if (widest != nullptr && widest->geometry.width >= 3.0f)
            return KeyCentre(widest->geometry).first;
        return (minX + maxX) / 2.0f;
    }

    KeySide SideOf(const DefinitionKey& key, float line)
    {
        const float centre = KeyCentre(key).first;
        const float half   = key.width / 2.0f;
        if (centre - half < line && centre + half > line)
            return KeySide::Neither;
        return centre < line ? KeySide::Left : KeySide::Right;
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
