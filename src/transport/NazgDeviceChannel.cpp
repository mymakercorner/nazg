// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDeviceChannel.h"

#include <utility>

namespace nazg
{
    Task<std::vector<uint8_t>> HidDeviceChannel::Request(std::vector<uint8_t> payload)
    {
        co_return co_await m_Transport.Request(m_Device, std::move(payload), m_TimeoutMs);
    }
}
