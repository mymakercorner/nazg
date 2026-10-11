// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaAudio.h"

#include <vector>

namespace nazg
{
    namespace
    {
        constexpr uint8_t c_Channel = static_cast<uint8_t>(ViaChannel::QmkAudio);

        Task<bool> ReadSetting(ViaProtocol& via, AudioSetting setting)
        {
            const std::vector<uint8_t> value = co_await via.GetCustomValue(c_Channel, static_cast<uint8_t>(setting), {}, 1);
            co_return !value.empty() && value[0] != 0;
        }
    }

    Task<AudioValues> ReadAudio(ViaProtocol& via)
    {
        AudioValues values;
        values.on     = co_await ReadSetting(via, AudioSetting::On);
        values.clicky = co_await ReadSetting(via, AudioSetting::Clicky);
        co_return values;
    }

    Task<void> WriteAudio(ViaProtocol& via, AudioSetting setting, bool value)
    {
        co_await via.SetCustomValue(c_Channel, static_cast<uint8_t>(setting), { static_cast<uint8_t>(value ? 1 : 0) });
        co_await via.SaveCustomValues(c_Channel);
    }

    Task<void> PlayIndication(ViaProtocol& via)
    {
        // The value is the byte after the id: the top byte of SetKeyboardValue's 32 bits.
        co_await via.SetKeyboardValue(ViaKeyboardValue::DeviceIndication, 0);
        co_await via.SetKeyboardValue(ViaKeyboardValue::DeviceIndication, 1u << 24);
    }
}
