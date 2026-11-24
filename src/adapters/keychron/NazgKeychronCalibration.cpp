// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeychronCalibration.h"

#include <algorithm>

#include "adapters/NazgProtocolError.h"

namespace nazg::keychron
{
    namespace
    {
        // The rest takes 8 scans and storing a write: a few readings. Far more than either, and it is stuck.
        constexpr int c_ReadingsMost = 400;
    }

    size_t CalibratedCount(const CalibrationState& state, const std::vector<KeyPosition>& keys)
    {
        return static_cast<size_t>(
            std::count_if(keys.begin(), keys.end(), [&](const KeyPosition key) { return state.IsDone(key); }));
    }

    bool MayEndCalibration(const CalibrationState& state, const std::vector<KeyPosition>& keys)
    {
        return state.phase == CalibrationPhase::Bottom && CalibratedCount(state, keys) < keys.size();
    }

    Task<CalibrationState> BeginCalibration(Protocol& protocol)
    {
        CalibrationState state = co_await protocol.GetCalibrationState();
        if (state.phase == CalibrationPhase::Off)
        {
            co_await protocol.StartCalibration();
            state = co_await protocol.GetCalibrationState();
        }
        for (int reading = 0; state.phase != CalibrationPhase::Bottom; ++reading)
        {
            if (state.phase == CalibrationPhase::Off)
                throw ProtocolError("Keychron: the board left the calibration as soon as it was started");
            if (reading >= c_ReadingsMost)
                throw ProtocolError("Keychron: the board never finished measuring its keys at rest");
            state = co_await protocol.GetCalibrationState();
        }
        protocol.SetCalibrationFarewell(true);
        co_return state;
    }

    Task<CalibrationState> WatchCalibration(Protocol& protocol, const std::vector<KeyPosition>& keys)
    {
        const CalibrationState state = co_await protocol.GetCalibrationState();
        if (!MayEndCalibration(state, keys))
            protocol.SetCalibrationFarewell(false);
        co_return state;
    }

    Task<CalibrationState> EndCalibration(Protocol& protocol, std::vector<KeyPosition> keys)
    {
        CalibrationState done = co_await protocol.GetCalibrationState();
        if (!MayEndCalibration(done, keys))
            co_return done;

        co_await protocol.StopCalibration();
        protocol.SetCalibrationFarewell(false);
        for (int reading = 0; done.phase != CalibrationPhase::Off; ++reading)
        {
            if (reading >= c_ReadingsMost)
                throw ProtocolError("Keychron: the board is still calibrating after it was told to stop");
            done.phase = (co_await protocol.GetCalibrationState()).phase;
        }
        co_return done;
    }
}
