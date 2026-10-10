// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaMacro.h"

namespace nazg
{
    namespace
    {
        constexpr uint8_t c_Prefix   = 0x01;   // SS_QMK_PREFIX
        constexpr uint8_t c_Tap      = 0x01;   // SS_TAP_CODE, SS_DOWN_CODE, SS_UP_CODE, SS_DELAY_CODE
        constexpr uint8_t c_Press    = 0x02;
        constexpr uint8_t c_Release  = 0x03;
        constexpr uint8_t c_Wait     = 0x04;
        constexpr uint8_t c_Extended = 0x04;   // VIAL_MACRO_EXT_TAP is 5: the 8-bit code plus this
        constexpr uint8_t c_WaitEnd  = '|';    // after VIA's digits

        bool IsPrefixed(MacroFormat format) noexcept { return format != MacroFormat::Unprefixed; }

        MacroAction::Kind KindOfCode(uint8_t code) noexcept
        {
            return code == c_Tap ? MacroAction::Kind::Tap : code == c_Press ? MacroAction::Kind::Press
                                                                            : MacroAction::Kind::Release;
        }

        uint8_t CodeOfKind(MacroAction::Kind kind) noexcept
        {
            return kind == MacroAction::Kind::Tap ? c_Tap : kind == MacroAction::Kind::Press ? c_Press : c_Release;
        }

        bool IsKey(MacroAction::Kind kind) noexcept
        {
            return kind == MacroAction::Kind::Tap || kind == MacroAction::Kind::Press ||
                   kind == MacroAction::Kind::Release;
        }

        // One macro's bytes, its 0 not included.
        MacroActions DecodeOne(const uint8_t* at, const uint8_t* end, MacroFormat format)
        {
            MacroActions actions;
            while (at < end)
            {
                const uint8_t byte = *at++;

                // VIA's old format: the action's code itself, then the key.
                if (!IsPrefixed(format) && (byte == c_Tap || byte == c_Press || byte == c_Release))
                {
                    if (at == end)
                        break;
                    actions.push_back({ KindOfCode(byte), *at++ });
                    continue;
                }
                if (!IsPrefixed(format) || byte != c_Prefix)
                {
                    actions.push_back({ MacroAction::Kind::Character, byte });
                    continue;
                }

                if (at == end)
                    break;
                const uint8_t code = *at++;
                if (code == c_Tap || code == c_Press || code == c_Release)
                {
                    if (at == end)
                        break;
                    actions.push_back({ KindOfCode(code), *at++ });
                }
                else if (code == c_Wait && format == MacroFormat::Via)
                {
                    // Digits, then the one character that ends them, consumed as the firmware does.
                    uint32_t milliseconds = 0;
                    while (at < end && *at >= '0' && *at <= '9')
                        milliseconds = milliseconds * 10 + (*at++ - '0');
                    if (at < end)
                        ++at;
                    actions.push_back({ MacroAction::Kind::Wait, milliseconds });
                }
                else if (code == c_Wait)
                {
                    if (end - at < 2)
                        break;
                    const uint32_t low = at[0], high = at[1];
                    at += 2;
                    if (low != 0 && high != 0)
                        actions.push_back({ MacroAction::Kind::Wait, (low - 1) + (high - 1) * 255 });
                }
                else if (format == MacroFormat::VialExtended && code >= c_Tap + c_Extended && code <= c_Release + c_Extended)
                {
                    if (end - at < 2)
                        break;
                    uint32_t keycode = at[0] | (static_cast<uint32_t>(at[1]) << 8);
                    at += 2;
                    // No byte may be 0, so a keycode whose low byte is 0 travels as 0xFF00 | high
                    // -- vial-qmk's decode_keycode().
                    if (keycode > 0xFF00)
                        keycode = (keycode & 0xFF) << 8;
                    actions.push_back({ KindOfCode(static_cast<uint8_t>(code - c_Extended)), keycode });
                }
                // Anything else is malformed: the two bytes are skipped.
            }
            return actions;
        }
    }

    MacroFormat MacroFormatFor(const BoardReport& report) noexcept
    {
        if (report.isVial)
            return report.vialProtocol >= 5 ? MacroFormat::VialExtended
                 : report.vialProtocol >= 2 ? MacroFormat::VialBasic
                                            : MacroFormat::Unprefixed;
        return report.viaProtocol >= 11 ? MacroFormat::Via : MacroFormat::Unprefixed;
    }

    bool HasWaits(MacroFormat format) noexcept
    {
        return format != MacroFormat::Unprefixed;
    }

    bool HoldsAnyKeycode(MacroFormat format) noexcept
    {
        return format == MacroFormat::VialExtended;
    }

    uint32_t LongestWait(MacroFormat format) noexcept
    {
        switch (format)
        {
        case MacroFormat::Unprefixed:   return 0;
        // QMK of protocol 12 stops the macro at a fifth digit; later QMK reads any number.
        case MacroFormat::Via:          return 9999;
        case MacroFormat::VialBasic:
        case MacroFormat::VialExtended: return 254 + 254 * 255;
        }
        return 0;
    }

    std::optional<size_t> MacrosEnd(const std::vector<uint8_t>& bytes, size_t count) noexcept
    {
        if (count == 0)
            return size_t{ 0 };
        size_t seen = 0;
        for (size_t at = 0; at < bytes.size(); ++at)
            if (bytes[at] == 0 && ++seen == count)
                return at + 1;
        return std::nullopt;
    }

    std::vector<MacroActions> DecodeMacros(const std::vector<uint8_t>& bytes, size_t count, MacroFormat format)
    {
        std::vector<MacroActions> macros;
        macros.reserve(count);

        const uint8_t* at  = bytes.data();
        const uint8_t* end = bytes.data() + bytes.size();
        while (macros.size() < count)
        {
            const uint8_t* stop = at;
            while (stop < end && *stop != 0)
                ++stop;
            macros.push_back(DecodeOne(at, stop, format));
            at = stop < end ? stop + 1 : end;
        }
        return macros;
    }

    std::optional<size_t> ActionSize(const MacroAction& action, MacroFormat format) noexcept
    {
        const size_t prefix = IsPrefixed(format) ? 1 : 0;
        switch (action.kind)
        {
        case MacroAction::Kind::Character:
            // A byte that is not 0, nor an action's start.
            if (action.value == 0 || action.value > 0x7F || (IsPrefixed(format) ? action.value == c_Prefix
                                                                                 : action.value <= c_Release))
                return std::nullopt;
            return size_t{ 1 };

        case MacroAction::Kind::Tap:
        case MacroAction::Kind::Press:
        case MacroAction::Kind::Release:
            if (action.value > 0xFFFF || action.value == 0)
                return std::nullopt;
            if (action.value <= 0xFF)
                return prefix + 2;
            if (format != MacroFormat::VialExtended)
                return std::nullopt;
            return size_t{ 4 };

        case MacroAction::Kind::Wait:
            if (!HasWaits(format) || action.value > LongestWait(format))
                return std::nullopt;
            if (format == MacroFormat::Via)
                return 2 + std::to_string(action.value).size() + 1;
            return size_t{ 4 };
        }
        return std::nullopt;
    }

    std::vector<uint8_t> EncodeMacros(const std::vector<MacroActions>& macros, MacroFormat format, std::string* error)
    {
        std::vector<uint8_t> bytes;
        for (size_t index = 0; index < macros.size(); ++index)
        {
            for (const MacroAction& action : macros[index])
            {
                if (!ActionSize(action, format))
                {
                    if (error)
                        *error = "macro " + std::to_string(index) + " holds " +
                                 (action.kind == MacroAction::Kind::Wait ? "a wait" : IsKey(action.kind) ? "a key"
                                                                                                         : "a character") +
                                 " this board's firmware cannot store";
                    return {};
                }

                if (action.kind == MacroAction::Kind::Character)
                {
                    bytes.push_back(static_cast<uint8_t>(action.value));
                }
                else if (action.kind == MacroAction::Kind::Wait)
                {
                    bytes.push_back(c_Prefix);
                    bytes.push_back(c_Wait);
                    if (format == MacroFormat::Via)
                    {
                        for (char digit : std::to_string(action.value))
                            bytes.push_back(static_cast<uint8_t>(digit));
                        bytes.push_back(c_WaitEnd);
                    }
                    else
                    {
                        bytes.push_back(static_cast<uint8_t>(action.value % 255 + 1));
                        bytes.push_back(static_cast<uint8_t>(action.value / 255 + 1));
                    }
                }
                else if (action.value <= 0xFF)
                {
                    if (IsPrefixed(format))
                        bytes.push_back(c_Prefix);
                    bytes.push_back(CodeOfKind(action.kind));
                    bytes.push_back(static_cast<uint8_t>(action.value));
                }
                else
                {
                    const uint32_t keycode = (action.value & 0xFF) != 0 ? action.value : 0xFF00 | (action.value >> 8);
                    bytes.push_back(c_Prefix);
                    bytes.push_back(static_cast<uint8_t>(CodeOfKind(action.kind) + c_Extended));
                    bytes.push_back(static_cast<uint8_t>(keycode & 0xFF));
                    bytes.push_back(static_cast<uint8_t>(keycode >> 8));
                }
            }
            bytes.push_back(0);
        }
        return bytes;
    }
}
