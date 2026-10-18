// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgHostKeys.h"

#include <algorithm>
#include <variant>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"

namespace nazg
{
    namespace
    {
        // The keyboard page's keys a QMK basic keycode sends as its own value -- QMK numbers them
        // as USB does, in every version: the keys to KC_EXSEL, then the modifiers.
        bool IsKeyboardKey(uint32_t usage) noexcept
        {
            return (usage >= 0x04 && usage <= 0xA4) || (usage >= 0xE0 && usage <= 0xE7);
        }

        // Media keys: QMK sends them on the consumer page, and the computer may report them there
        // or as the keyboard page's own -- SDL turns the volume into the keyboard page's.
        struct MediaKey
        {
            uint32_t         usage;
            std::string_view name;
        };

        constexpr MediaKey c_Media[] = {
            { HidUsage(c_KeyboardPage, 0x7F), "KC_MUTE" }, { HidUsage(c_KeyboardPage, 0x80), "KC_VOLU" },
            { HidUsage(c_KeyboardPage, 0x81), "KC_VOLD" }, { HidUsage(c_ConsumerPage, 0xE2), "KC_MUTE" },
            { HidUsage(c_ConsumerPage, 0xE9), "KC_VOLU" }, { HidUsage(c_ConsumerPage, 0xEA), "KC_VOLD" },
            { HidUsage(c_ConsumerPage, 0xB5), "KC_MNXT" }, { HidUsage(c_ConsumerPage, 0xB6), "KC_MPRV" },
            { HidUsage(c_ConsumerPage, 0xB7), "KC_MSTP" }, { HidUsage(c_ConsumerPage, 0xCD), "KC_MPLY" },
        };

        std::optional<std::string_view> NameOf(const Keycode& keycode)
        {
            if (const auto* named = std::get_if<NamedKey>(&keycode))
                return named->name;
            if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
                return modified->key;
            if (const auto* modTap = std::get_if<ModTapKey>(&keycode))
                return modTap->key;
            if (const auto* layerTap = std::get_if<LayerTapKey>(&keycode))
                return layerTap->key;
            return std::nullopt;
        }
    }

    std::vector<std::string_view> KeycodesSending(uint32_t usage, qmk::KeycodeVersion version)
    {
        std::vector<std::string_view> names;
        if ((usage >> 16) == c_KeyboardPage && IsKeyboardKey(usage & 0xFFFF))
            if (const std::optional<std::string_view> name =
                    NameOf(qmk::DecodeKeycode(static_cast<uint16_t>(usage & 0xFFFF), version)))
                names.push_back(*name);
        for (const MediaKey& media : c_Media)
            if (media.usage == usage && std::find(names.begin(), names.end(), media.name) == names.end())
                names.push_back(media.name);
        return names;
    }

    std::optional<std::string_view> TestedName(const Keycode& keycode, qmk::KeycodeVersion version)
    {
        const std::optional<std::string_view> name = NameOf(keycode);
        if (!name)
            return std::nullopt;

        // A name some usage arrives as: a basic key of the keyboard page, or a media key.
        if (std::any_of(std::begin(c_Media), std::end(c_Media), [&](const MediaKey& media) { return media.name == *name; }))
            return name;
        const std::optional<uint16_t> value = qmk::EncodeKeycode(NamedKey{ *name }, version);
        if (value && IsKeyboardKey(*value))
            return name;
        return std::nullopt;
    }
}
