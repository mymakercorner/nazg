// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDeviceList.h"

#include <algorithm>

namespace nazg
{
    std::vector<std::string> KeptProtocols(const std::vector<HidDeviceInfo>& before,
                                           const std::vector<std::string>&   protocols,
                                           const std::vector<HidDeviceInfo>& now)
    {
        std::vector<std::string> kept(now.size());
        for (size_t index = 0; index < now.size(); ++index)
        {
            const auto listed = std::find_if(before.begin(), before.end(), [&](const HidDeviceInfo& device)
                                             { return device.path == now[index].path; });
            const auto was    = static_cast<size_t>(listed - before.begin());
            if (listed != before.end() && was < protocols.size())
                kept[index] = protocols[was];
        }
        return kept;
    }

    bool IsListed(const std::vector<HidDeviceInfo>& devices, const std::string& path)
    {
        return std::any_of(devices.begin(), devices.end(),
                           [&](const HidDeviceInfo& device) { return device.path == path; });
    }
}
