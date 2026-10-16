// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialLoader.h"

#include <string>
#include <utility>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/vial/NazgVialDefinition.h"

namespace nazg::vial
{
    qmk::KeycodeVersion KeycodeVersionFor(uint32_t vialProtocol) noexcept
    {
        if (vialProtocol >= 6)
            return qmk::KeycodeVersion::V0_0_7;

        return qmk::KeycodeVersion::Legacy;
    }

    Task<Keyboard> LoadKeyboard(Protocol& protocol, std::vector<uint8_t>* definitionJson)
    {
        // The payoff of the coroutine layer: a sequence of round trips reading top to
        // bottom, with the frame loop still running between each one.
        const std::optional<Identity> identity = co_await protocol.Detect();

        if (!identity)
            throw ProtocolError("this device is not a Vial board");

        const qmk::KeycodeVersion keycodeVersion = KeycodeVersionFor(identity->protocolVersion);

        const std::vector<uint8_t> json       = DecompressDefinition(co_await protocol.DownloadDefinition());
        via::KeyboardDefinition    definition = via::ParseDefinition(json);
        if (definitionJson != nullptr)
            *definitionJson = json;

        const uint8_t layers = co_await protocol.GetLayerCount();

        if (layers == 0)
            throw ProtocolError("the device reports no layers");

        // Layout options are optional: a board with no selectable layouts answers 0xFF
        // to the value id, which is a refusal rather than a failure worth propagating.
        uint32_t layoutOptions = 0;
        bool     hasLayouts    = true;

        try
        {
            layoutOptions = co_await protocol.GetKeyboardValue(via::KeyboardValue::LayoutOptions);
        }
        catch (const ProtocolError&)
        {
            hasLayouts = false;
        }

        if (!hasLayouts)
            layoutOptions = 0;

        // One bulk read rather than a call per key: 31 round trips instead of 432 for
        // a 3 x 8 x 18 board.
        const size_t byteCount = via::KeymapByteCount(layers, definition.matrixRows, definition.matrixColumns);

        const std::vector<uint8_t> keymapBytes =
            co_await protocol.GetKeymapBuffer(0, static_cast<uint16_t>(byteCount));

        Keymap keymap = via::DecodeKeymap(keymapBytes, layers, definition.matrixRows,
                                          definition.matrixColumns, keycodeVersion);

        co_return BuildKeyboard(std::move(definition), std::move(keymap), layoutOptions, keycodeVersion);
    }

    Task<BoardReport> ReadBoardReport(Protocol& protocol)
    {
        BoardReport report;
        report.viaProtocol = co_await protocol.GetProtocolVersion();

        if (const std::optional<Identity> identity = co_await protocol.Detect())
        {
            report.isVial       = true;
            report.vialProtocol = identity->protocolVersion;

            // From Vial protocol 4; an older build echoes the request, which reads as a refusal.
            try
            {
                const EntryCounts counts = co_await protocol.GetEntryCounts();
                report.tapDanceCount     = counts.tapDance;
                report.comboCount        = counts.combo;
                report.keyOverrideCount  = counts.keyOverride;
                report.altRepeatKeyCount = counts.altRepeatKey;
                report.capsWord          = counts.capsWord;
                report.layerLock         = counts.layerLock;
            }
            catch (const ProtocolError&)
            {
            }

            // Whether there are any: one query, from the start.
            if (report.vialProtocol >= 4)
            {
                try
                {
                    report.hasQmkSettings = !(co_await protocol.QueryQmkSettings(0)).empty();
                }
                catch (const ProtocolError&)
                {
                }
            }
        }

        try
        {
            report.macroCount = co_await protocol.GetMacroCount();
        }
        catch (const ProtocolError&)
        {
        }

        co_return report;
    }

    TapDance DecodeTapDance(const TapDanceEntry& entry, qmk::KeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return qmk::DecodeKeycode(raw, version);
        };
        TapDance dance;
        dance.actions     = { decode(entry.onTap), decode(entry.onHold), decode(entry.onDoubleTap), decode(entry.onTapHold) };
        dance.tappingTerm = entry.tappingTerm;
        return dance;
    }

    std::optional<TapDanceEntry> EncodeTapDance(const TapDance& dance, qmk::KeycodeVersion version)
    {
        uint16_t raw[4] = {};
        for (size_t action = 0; action < 4; ++action)
        {
            if (!dance.actions[action])
                continue;
            const std::optional<uint16_t> value = qmk::EncodeKeycode(*dance.actions[action], version);
            if (!value)
                return std::nullopt;
            raw[action] = *value;
        }
        return TapDanceEntry{ raw[0], raw[1], raw[2], raw[3], dance.tappingTerm };
    }

    Task<std::vector<TapDance>> ReadTapDances(Protocol& protocol, uint8_t count, qmk::KeycodeVersion version)
    {
        std::vector<TapDance> dances;
        for (uint8_t index = 0; index < count; ++index)
            dances.push_back(DecodeTapDance(co_await protocol.GetTapDance(index), version));
        co_return dances;
    }

    Combo DecodeCombo(const ComboEntry& entry, qmk::KeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return qmk::DecodeKeycode(raw, version);
        };
        Combo combo;
        for (size_t input = 0; input < combo.inputs.size(); ++input)
            combo.inputs[input] = decode(entry.inputs[input]);
        combo.output = decode(entry.output);
        return combo;
    }

    std::optional<ComboEntry> EncodeCombo(const Combo& combo, qmk::KeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return qmk::EncodeKeycode(*keycode, version);
        };
        ComboEntry entry;
        for (size_t input = 0; input < combo.inputs.size(); ++input)
        {
            const std::optional<uint16_t> value = encode(combo.inputs[input]);
            if (!value)
                return std::nullopt;
            entry.inputs[input] = *value;
        }
        const std::optional<uint16_t> output = encode(combo.output);
        if (!output)
            return std::nullopt;
        entry.output = *output;
        return entry;
    }

    Task<std::vector<Combo>> ReadCombos(Protocol& protocol, uint8_t count, qmk::KeycodeVersion version)
    {
        std::vector<Combo> combos;
        for (uint8_t index = 0; index < count; ++index)
            combos.push_back(DecodeCombo(co_await protocol.GetCombo(index), version));
        co_return combos;
    }

    KeyOverride DecodeKeyOverride(const KeyOverrideEntry& entry, qmk::KeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return qmk::DecodeKeycode(raw, version);
        };
        KeyOverride keyOverride;
        keyOverride.trigger     = decode(entry.trigger);
        keyOverride.replacement = decode(entry.replacement);
        keyOverride.layers      = entry.layers;
        keyOverride.held        = entry.triggerMods;
        keyOverride.notHeld     = entry.negativeModMask;
        keyOverride.hidden      = entry.suppressedMods;
        keyOverride.options     = entry.options;
        return keyOverride;
    }

    std::optional<KeyOverrideEntry> EncodeKeyOverride(const KeyOverride& keyOverride, qmk::KeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return qmk::EncodeKeycode(*keycode, version);
        };
        const std::optional<uint16_t> trigger     = encode(keyOverride.trigger);
        const std::optional<uint16_t> replacement = encode(keyOverride.replacement);
        if (!trigger || !replacement)
            return std::nullopt;

        KeyOverrideEntry entry;
        entry.trigger         = *trigger;
        entry.replacement     = *replacement;
        entry.layers          = keyOverride.layers;
        entry.triggerMods     = keyOverride.held;
        entry.negativeModMask = keyOverride.notHeld;
        entry.suppressedMods  = keyOverride.hidden;
        entry.options         = keyOverride.options;
        return entry;
    }

    Task<std::vector<KeyOverride>> ReadKeyOverrides(Protocol& protocol, uint8_t count, qmk::KeycodeVersion version)
    {
        std::vector<KeyOverride> keyOverrides;
        for (uint8_t index = 0; index < count; ++index)
            keyOverrides.push_back(DecodeKeyOverride(co_await protocol.GetKeyOverride(index), version));
        co_return keyOverrides;
    }

    AltRepeatKey DecodeAltRepeatKey(const AltRepeatKeyEntry& entry, qmk::KeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return qmk::DecodeKeycode(raw, version);
        };
        AltRepeatKey altRepeatKey;
        altRepeatKey.lastKey = decode(entry.keycode);
        altRepeatKey.altKey  = decode(entry.altKeycode);
        altRepeatKey.allowed = entry.allowedMods;
        altRepeatKey.options = entry.options;
        return altRepeatKey;
    }

    std::optional<AltRepeatKeyEntry> EncodeAltRepeatKey(const AltRepeatKey& altRepeatKey, qmk::KeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return qmk::EncodeKeycode(*keycode, version);
        };
        const std::optional<uint16_t> keycode    = encode(altRepeatKey.lastKey);
        const std::optional<uint16_t> altKeycode = encode(altRepeatKey.altKey);
        if (!keycode || !altKeycode)
            return std::nullopt;

        AltRepeatKeyEntry entry;
        entry.keycode     = *keycode;
        entry.altKeycode  = *altKeycode;
        entry.allowedMods = altRepeatKey.allowed;
        entry.options     = altRepeatKey.options;
        return entry;
    }

    Task<std::vector<AltRepeatKey>> ReadAltRepeatKeys(Protocol& protocol, uint8_t count, qmk::KeycodeVersion version)
    {
        std::vector<AltRepeatKey> altRepeatKeys;
        for (uint8_t index = 0; index < count; ++index)
            altRepeatKeys.push_back(DecodeAltRepeatKey(co_await protocol.GetAltRepeatKey(index), version));
        co_return altRepeatKeys;
    }
}
