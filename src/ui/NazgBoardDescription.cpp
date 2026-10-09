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

    ResolvedKey ResolveKey(const Keyboard& keyboard, const DefinitionKey& key, uint8_t layer)
    {
        const auto isNamed = [](const Keycode& keycode, std::string_view name)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named != nullptr && named->name == name;
        };

        ResolvedKey resolved{ Fallthrough::None, keyboard.KeycodeFor(key, layer), layer };
        if (isNamed(resolved.keycode, "KC_TRNS"))
            resolved.fallthrough = Fallthrough::Transparent;
        else if (isNamed(resolved.keycode, "KC_NO"))
            resolved.fallthrough = Fallthrough::Disabled;
        else
            return resolved;

        for (int below = layer - 1; below >= 0; --below)
        {
            Keycode keycode = keyboard.KeycodeFor(key, static_cast<uint8_t>(below));
            if (isNamed(keycode, "KC_TRNS"))
                continue;
            if (isNamed(keycode, "KC_NO"))
            {
                resolved.fallthrough = Fallthrough::Disabled;
                continue;
            }

            resolved.keycode = std::move(keycode);
            resolved.layer   = static_cast<uint8_t>(below);
            return resolved;
        }

        return { Fallthrough::Disabled, NamedKey{ "KC_NO" }, std::nullopt };
    }

    void DescribeLegends(BoardDescription& board, const Keyboard& keyboard, uint8_t layer,
                         const LegendSettings& settings)
    {
        const float              line     = SideLine(board.keys);
        const uint8_t            lighting = LightingSystemsOf(keyboard);
        const std::vector<Words> custom   = CustomKeycodeWordsOf(keyboard);

        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;

            const LegendContext context{ settings.Layout(), settings.modifierNames, SideOf(key.geometry, line),
                                         lighting, custom };
            const ResolvedKey   resolved = ResolveKey(keyboard, key.geometry, layer);
            key.legends     = LegendFor(resolved.keycode, context);
            key.fallthrough = resolved.fallthrough;
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

    std::vector<Words> CustomKeycodeWordsOf(const Keyboard& keyboard)
    {
        const auto oneLine = [](std::string text)
        {
            std::replace(text.begin(), text.end(), '\n', ' ');
            return text;
        };

        std::vector<Words> words;
        for (const KeyboardDefinition::CustomKeycode& custom : keyboard.definition.customKeycodes)
            words.push_back({ oneLine(custom.name), oneLine(custom.shortName) });
        return words;
    }

    LightingFirmware LightingFirmwareOf(const Keyboard& keyboard)
    {
        const BoardReport& report = keyboard.report;

        // A Vial board by its Vial protocol only: vial-qmk always reports VIA 9.
        if (report.isVial)
        {
            if (report.vialProtocol <= 5)
                return LightingFirmware::Old;
            return report.capsWord || report.layerLock ? LightingFirmware::New : LightingFirmware::Unknown;
        }

        if (report.viaProtocol >= 13)
            return LightingFirmware::New;
        if (report.viaProtocol >= 9 && report.viaProtocol <= 11)
            return LightingFirmware::Old;

        if (report.viaProtocol == 12)
        {
            const Keymap& keymap = keyboard.keymap;
            for (uint8_t layer = 0; layer < keymap.Layers(); ++layer)
                for (uint8_t row = 0; row < keymap.Rows(); ++row)
                    for (uint8_t column = 0; column < keymap.Columns(); ++column)
                        if (const auto* named = std::get_if<NamedKey>(&keymap.At(layer, row, column));
                            named != nullptr && named->name.substr(0, 3) == "RM_")
                            return LightingFirmware::New;
        }

        return LightingFirmware::Unknown;
    }

    LightingNote LightingNoteOf(const Keycode& keycode, const Keyboard& keyboard)
    {
        // Only these three families: the board's state is not worked out for any other key.
        const auto* named = std::get_if<NamedKey>(&keycode);
        if (named == nullptr)
            return {};
        const std::string_view name   = named->name;
        const bool             isRm   = name.rfind("RM_", 0) == 0;
        const bool             isMode = name.rfind("RGB_M_", 0) == 0;
        const bool             isGlow = name.rfind("UG_", 0) == 0;
        if (!isRm && !isMode && !isGlow)
            return {};

        using namespace LightingSystem;
        const LightingFirmware firmware = LightingFirmwareOf(keyboard);
        const uint8_t          systems  = LightingSystemsOf(keyboard);
        const bool             glow     = (systems & Underglow) != 0;
        const bool             matrix   = (systems & RgbMatrix) != 0;

        // QMK's RGB keycode overhaul reached vial-qmk three months after QMK.
        const std::string overhaul = keyboard.report.isVial ? "vial-qmk's February 2025 merge of QMK's RGB overhaul"
                                                            : "QMK's November 2024 RGB overhaul";

        if (isRm && firmware == LightingFirmware::Old)
            return { "does nothing on this firmware, from before " + overhaul, true };
        if (isRm && firmware == LightingFirmware::Unknown)
            return { "does nothing on firmware from before " + overhaul, true };

        if (isMode && firmware == LightingFirmware::New)
            return { "does nothing on this firmware: the RGB_M modes went with " + overhaul, true };
        if (isMode && firmware == LightingFirmware::Unknown)
            return { "does nothing on firmware from after " + overhaul, true };
        if (isMode && matrix && !glow)
        {
            // Old firmware: four modes reach an RGB Matrix, the rest are an underglow's.
            for (std::string_view reaches : { "RGB_M_P", "RGB_M_B", "RGB_M_R", "RGB_M_SW" })
                if (name == reaches)
                    return {};
            return { "does nothing on this board: only an underglow has this mode", true };
        }

        if (isGlow && glow && matrix)
            return { firmware == LightingFirmware::Old
                         ? "drives the underglow and the RGB Matrix"
                         : "drives the underglow, and the RGB Matrix unless the firmware opts out",
                     false };

        return {};
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
        return DescribeKeyboard(keyboard, keyboard.layoutSelection);
    }

    BoardDescription DescribeKeyboard(const Keyboard& keyboard, const std::vector<uint8_t>& selection)
    {
        BoardDescription board;
        for (const DefinitionKey& key : PlaceKeys(keyboard.definition, selection))
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
