// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialLoader.h"

#include <string>
#include <utility>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/vial/NazgVialDefinition.h"

namespace nazg
{
    QmkKeycodeVersion QmkKeycodeVersionForVial(uint32_t vialProtocol) noexcept
    {
        if (vialProtocol >= 6)
            return QmkKeycodeVersion::V0_0_7;

        return QmkKeycodeVersion::Legacy;
    }

    Task<Keyboard> LoadVialKeyboard(VialProtocol& protocol, std::vector<uint8_t>* definitionJson)
    {
        // The payoff of the coroutine layer: a sequence of round trips reading top to
        // bottom, with the frame loop still running between each one.
        const std::optional<VialIdentity> identity = co_await protocol.Detect();

        if (!identity)
            throw ProtocolError("this device is not a Vial board");

        const QmkKeycodeVersion keycodeVersion = QmkKeycodeVersionForVial(identity->protocolVersion);

        const std::vector<uint8_t> json       = DecompressDefinition(co_await protocol.DownloadDefinition());
        KeyboardDefinition         definition = ParseDefinition(json);
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
            layoutOptions = co_await protocol.GetKeyboardValue(ViaKeyboardValue::LayoutOptions);
        }
        catch (const ProtocolError&)
        {
            hasLayouts = false;
        }

        if (!hasLayouts)
            layoutOptions = 0;

        // One bulk read rather than a call per key: 31 round trips instead of 432 for
        // a 3 x 8 x 18 board.
        const size_t byteCount = ViaKeymapByteCount(layers, definition.matrixRows, definition.matrixColumns);

        const std::vector<uint8_t> keymapBytes =
            co_await protocol.GetKeymapBuffer(0, static_cast<uint16_t>(byteCount));

        Keymap keymap = DecodeViaKeymap(keymapBytes, layers, definition.matrixRows,
                                        definition.matrixColumns, keycodeVersion);

        co_return BuildKeyboard(std::move(definition), std::move(keymap), layoutOptions, keycodeVersion);
    }

    Task<BoardReport> ReadBoardReport(VialProtocol& protocol)
    {
        BoardReport report;
        report.viaProtocol = co_await protocol.GetProtocolVersion();

        if (const std::optional<VialIdentity> identity = co_await protocol.Detect())
        {
            report.isVial       = true;
            report.vialProtocol = identity->protocolVersion;

            // From Vial protocol 4; an older build echoes the request, which reads as a refusal.
            try
            {
                const VialEntryCounts counts = co_await protocol.GetEntryCounts();
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

    TapDance DecodeTapDance(const VialTapDanceEntry& entry, QmkKeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return DecodeQmkKeycode(raw, version);
        };
        TapDance dance;
        dance.actions     = { decode(entry.onTap), decode(entry.onHold), decode(entry.onDoubleTap), decode(entry.onTapHold) };
        dance.tappingTerm = entry.tappingTerm;
        return dance;
    }

    std::optional<VialTapDanceEntry> EncodeTapDance(const TapDance& dance, QmkKeycodeVersion version)
    {
        uint16_t raw[4] = {};
        for (size_t action = 0; action < 4; ++action)
        {
            if (!dance.actions[action])
                continue;
            const std::optional<uint16_t> value = EncodeQmkKeycode(*dance.actions[action], version);
            if (!value)
                return std::nullopt;
            raw[action] = *value;
        }
        return VialTapDanceEntry{ raw[0], raw[1], raw[2], raw[3], dance.tappingTerm };
    }

    Task<std::vector<TapDance>> ReadTapDances(VialProtocol& protocol, uint8_t count, QmkKeycodeVersion version)
    {
        std::vector<TapDance> dances;
        for (uint8_t index = 0; index < count; ++index)
            dances.push_back(DecodeTapDance(co_await protocol.GetTapDance(index), version));
        co_return dances;
    }

    Combo DecodeCombo(const VialComboEntry& entry, QmkKeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return DecodeQmkKeycode(raw, version);
        };
        Combo combo;
        for (size_t input = 0; input < combo.inputs.size(); ++input)
            combo.inputs[input] = decode(entry.inputs[input]);
        combo.output = decode(entry.output);
        return combo;
    }

    std::optional<VialComboEntry> EncodeCombo(const Combo& combo, QmkKeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return EncodeQmkKeycode(*keycode, version);
        };
        VialComboEntry entry;
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

    Task<std::vector<Combo>> ReadCombos(VialProtocol& protocol, uint8_t count, QmkKeycodeVersion version)
    {
        std::vector<Combo> combos;
        for (uint8_t index = 0; index < count; ++index)
            combos.push_back(DecodeCombo(co_await protocol.GetCombo(index), version));
        co_return combos;
    }

    KeyOverride DecodeKeyOverride(const VialKeyOverrideEntry& entry, QmkKeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return DecodeQmkKeycode(raw, version);
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

    std::optional<VialKeyOverrideEntry> EncodeKeyOverride(const KeyOverride& keyOverride, QmkKeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return EncodeQmkKeycode(*keycode, version);
        };
        const std::optional<uint16_t> trigger     = encode(keyOverride.trigger);
        const std::optional<uint16_t> replacement = encode(keyOverride.replacement);
        if (!trigger || !replacement)
            return std::nullopt;

        VialKeyOverrideEntry entry;
        entry.trigger         = *trigger;
        entry.replacement     = *replacement;
        entry.layers          = keyOverride.layers;
        entry.triggerMods     = keyOverride.held;
        entry.negativeModMask = keyOverride.notHeld;
        entry.suppressedMods  = keyOverride.hidden;
        entry.options         = keyOverride.options;
        return entry;
    }

    Task<std::vector<KeyOverride>> ReadKeyOverrides(VialProtocol& protocol, uint8_t count, QmkKeycodeVersion version)
    {
        std::vector<KeyOverride> keyOverrides;
        for (uint8_t index = 0; index < count; ++index)
            keyOverrides.push_back(DecodeKeyOverride(co_await protocol.GetKeyOverride(index), version));
        co_return keyOverrides;
    }

    AltRepeatKey DecodeAltRepeatKey(const VialAltRepeatKeyEntry& entry, QmkKeycodeVersion version)
    {
        const auto decode = [version](uint16_t raw) -> std::optional<Keycode>
        {
            if (raw == 0)
                return std::nullopt;
            return DecodeQmkKeycode(raw, version);
        };
        AltRepeatKey altRepeatKey;
        altRepeatKey.lastKey = decode(entry.keycode);
        altRepeatKey.altKey  = decode(entry.altKeycode);
        altRepeatKey.allowed = entry.allowedMods;
        altRepeatKey.options = entry.options;
        return altRepeatKey;
    }

    std::optional<VialAltRepeatKeyEntry> EncodeAltRepeatKey(const AltRepeatKey& altRepeatKey, QmkKeycodeVersion version)
    {
        const auto encode = [version](const std::optional<Keycode>& keycode) -> std::optional<uint16_t>
        {
            if (!keycode)
                return uint16_t{ 0 };
            return EncodeQmkKeycode(*keycode, version);
        };
        const std::optional<uint16_t> keycode    = encode(altRepeatKey.lastKey);
        const std::optional<uint16_t> altKeycode = encode(altRepeatKey.altKey);
        if (!keycode || !altKeycode)
            return std::nullopt;

        VialAltRepeatKeyEntry entry;
        entry.keycode     = *keycode;
        entry.altKeycode  = *altKeycode;
        entry.allowedMods = altRepeatKey.allowed;
        entry.options     = altRepeatKey.options;
        return entry;
    }

    Task<std::vector<AltRepeatKey>> ReadAltRepeatKeys(VialProtocol& protocol, uint8_t count, QmkKeycodeVersion version)
    {
        std::vector<AltRepeatKey> altRepeatKeys;
        for (uint8_t index = 0; index < count; ++index)
            altRepeatKeys.push_back(DecodeAltRepeatKey(co_await protocol.GetAltRepeatKey(index), version));
        co_return altRepeatKeys;
    }
}
