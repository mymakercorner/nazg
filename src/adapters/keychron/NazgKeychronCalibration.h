// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Calibrating a Hall-effect Keychron (keychron-commands.md, "Calibration"; run and left early in Keychron's
// Launcher on a K10 HE V3, 2026-11-24). The board measures and stores by itself: every key at rest, then each as
// it is pressed to the bottom, then saved about 3 s after the last. A client starts it, watches, and may end it
// early. Nothing types meanwhile, and no calibration can be written back.
//
// Here: the sequence, kept away from two traps of the published firmware -- ended while the rest is measured,
// the board is dead until it is replugged; ended once every key is done, the next calibration is spoilt.
//
// No window.

#pragma once

#include <cstddef>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"

namespace nazg::keychron
{
    // How many of `keys` are done, by the board's own bits -- the last calibration's, kept after it.
    [[nodiscard]] size_t CalibratedCount(const CalibrationState& state, const std::vector<KeyPosition>& keys);

    // Whether it may be ended now, keeping what is done: waiting for the keys, and not all of them done.
    [[nodiscard]] bool MayEndCalibration(const CalibrationState& state, const std::vector<KeyPosition>& keys);

    // Starts it and waits for the rest to be measured, the board then waiting for the keys; one already running
    // -- left by another program -- is taken up. Until it ends, closing the channel ends it, its farewell.
    // Throws ProtocolError when the board refuses, or never gets there.
    [[nodiscard]] Task<CalibrationState> BeginCalibration(Protocol& protocol);

    // One look at it: the state, the farewell forgotten once the board ends it by itself.
    [[nodiscard]] Task<CalibrationState> WatchCalibration(Protocol& protocol, const std::vector<KeyPosition>& keys);

    // Ends it early where that is safe -- MayEndCalibration(), asked of the board again first -- and waits for the
    // board to be out of it. The state with the keys that were done; Bottom where it could not be ended.
    [[nodiscard]] Task<CalibrationState> EndCalibration(Protocol& protocol, std::vector<KeyPosition> keys);
}
