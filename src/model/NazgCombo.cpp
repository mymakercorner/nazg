// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgCombo.h"

#include <algorithm>
#include <string_view>
#include <variant>

namespace nazg
{
    namespace
    {
        bool Contains(const std::vector<Keycode>& keys, const Keycode& keycode)
        {
            return std::find(keys.begin(), keys.end(), keycode) != keys.end();
        }

        // The tap of a tap-hold key -- MT, LT, SH_T -- by name; none for any other key.
        std::optional<std::string_view> TapOf(const Keycode& keycode)
        {
            if (const auto* modTap = std::get_if<ModTapKey>(&keycode))
                return modTap->key;
            if (const auto* layerTap = std::get_if<LayerTapKey>(&keycode))
                return layerTap->key;
            if (const auto* swapTap = std::get_if<SwapHandsTapKey>(&keycode))
                return swapTap->key;
            return std::nullopt;
        }
    }

    bool Combo::IsEmpty() const
    {
        return !output && std::none_of(inputs.begin(), inputs.end(), [](const auto& input) { return input.has_value(); });
    }

    std::vector<Keycode> Combo::MatchedInputs() const
    {
        std::vector<Keycode> matched;
        for (const std::optional<Keycode>& input : inputs)
        {
            if (!input)
                break;
            matched.push_back(*input);
        }
        return matched;
    }

    bool Combo::HasGap() const
    {
        bool emptySeen = false;
        for (const std::optional<Keycode>& input : inputs)
        {
            if (!input)
                emptySeen = true;
            else if (emptySeen)
                return true;
        }
        return false;
    }

    void Combo::CloseGaps()
    {
        std::array<std::optional<Keycode>, 4> closed;
        size_t                                next = 0;
        for (const std::optional<Keycode>& input : inputs)
            if (input)
                closed[next++] = input;
        inputs = closed;
    }

    ComboRelation RelationOf(const Combo& first, const Combo& second)
    {
        const std::vector<Keycode> a = first.MatchedInputs();
        const std::vector<Keycode> b = second.MatchedInputs();
        if (a.empty() || b.empty())
            return ComboRelation::None;

        const bool aInB = std::all_of(a.begin(), a.end(), [&](const Keycode& key) { return Contains(b, key); });
        const bool bInA = std::all_of(b.begin(), b.end(), [&](const Keycode& key) { return Contains(a, key); });
        if (aInB && bInA)
            return ComboRelation::SameKeys;
        if (aInB)
            return ComboRelation::Inside;
        if (bInA)
            return ComboRelation::Holds;
        return ComboRelation::None;
    }

    std::optional<uint8_t> LayerSending(const Keyboard& keyboard, const Keycode& keycode)
    {
        for (int layer = 0; layer < keyboard.keymap.Layers(); ++layer)
            for (const DefinitionKey& key : keyboard.definition.keys)
                if (!key.decal && keyboard.IsKeyVisible(key) &&
                    keyboard.KeycodeFor(key, static_cast<uint8_t>(layer)) == keycode)
                    return static_cast<uint8_t>(layer);
        return std::nullopt;
    }

    std::optional<Keycode> TapHoldSending(const Keyboard& keyboard, const Keycode& keycode)
    {
        const auto* named = std::get_if<NamedKey>(&keycode);
        if (!named || LayerSending(keyboard, keycode))
            return std::nullopt;
        for (const DefinitionKey& key : keyboard.definition.keys)
        {
            if (key.decal || !keyboard.IsKeyVisible(key))
                continue;
            const Keycode sent = keyboard.KeycodeFor(key, 0);
            if (const std::optional<std::string_view> tap = TapOf(sent); tap && *tap == named->name)
                return sent;
        }
        return std::nullopt;
    }
}
