// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyboardList - the screen shown when no board is open: the keyboards found, each with its
// protocol and Open (ui-design.md, screen 1 and "Getting back to the keyboard list").
//
// "Show all HID devices" lists every other HID interface under the keyboards, dimmed and not
// openable, with the technical columns that exist only in that view. Its state is the
// caller's, off at every start.
//
// It only draws and reports what was clicked; opening a board is the caller's. ImGui only,
// no SDL.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "transport/NazgHidTransport.h"

namespace nazg
{
    // A keyboard Nazg can talk to, as far as enumeration tells: it exposes VIA's raw HID
    // interface (usage page 0xFF60, usage 0x61), one per board. Necessary, not sufficient --
    // a QMK build with raw HID and no VIA has it too, and does not answer.
    [[nodiscard]] bool IsViaInterface(const HidDeviceInfo& device) noexcept;

    struct KeyboardListAction
    {
        std::optional<size_t> open;   // index into the devices
        bool                  refresh = false;
    };

    // `protocols` runs parallel to `devices`: "Vial", "VIA", "no answer", or empty while the
    // board has not been asked yet. `isListing` while enumerating; `canRefresh` once the
    // boards have all been asked; `canOpen` is false while something still works on a board.
    [[nodiscard]] KeyboardListAction DrawKeyboardList(const std::vector<HidDeviceInfo>& devices,
                                                      const std::vector<std::string>&   protocols,
                                                      bool&                             showAllHidDevices,
                                                      bool                              isListing,
                                                      bool                              canRefresh,
                                                      bool                              canOpen);
}
