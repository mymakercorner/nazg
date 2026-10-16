// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DeviceList - the keyboard list read again after a device came or went (ui-design.md,
// "Hotplug"): what each device already answered is kept, so only a path not seen before is
// asked which protocol it speaks. Never the open board: a second handle on it would take a copy
// of every reply meant for the first.

#pragma once

#include <string>
#include <vector>

#include "transport/NazgHidTransport.h"

namespace nazg
{
    // `protocols` runs parallel to `before`, the result parallel to `now`: a path listed before
    // keeps its answer -- "no answer" included, asked again only once it is unplugged -- and a
    // new one is empty, still to be asked.
    [[nodiscard]] std::vector<std::string> KeptProtocols(const std::vector<HidDeviceInfo>& before,
                                                         const std::vector<std::string>&   protocols,
                                                         const std::vector<HidDeviceInfo>& now);

    [[nodiscard]] bool IsListed(const std::vector<HidDeviceInfo>& devices, const std::string& path);
}
