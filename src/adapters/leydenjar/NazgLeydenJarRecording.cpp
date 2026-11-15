// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLeydenJarRecording.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace nazg::leydenjar
{
    std::string RecordingJson(const Recording& recording)
    {
        const ControllerInfo& info    = recording.info;
        const Details&        details = info.details;

        nlohmann::ordered_json json;
        json["board"] = recording.board;

        nlohmann::ordered_json about;
        about["version"]           = std::to_string(info.version.major) + "." + std::to_string(info.version.mid) + "." +
                           std::to_string(info.version.minor);
        about["beamSpring"]        = details.technology == SwitchTechnology::BeamSpring;
        about["logicalRows"]       = details.logicalRows;
        about["logicalColumns"]    = details.logicalColumns;
        about["controllerRows"]    = details.controllerRows;
        about["controllerColumns"] = details.controllerColumns;
        about["wiring"]            = info.mapping.wiring;
        about["isLeftHalf"]        = info.isLeftHalf;
        about["thresholds"]        = info.thresholds;
        about["referenceLevels"]   = info.referenceLevels;
        about["mappingColumns"]    = info.mapping.columns;
        about["mappingRows"]       = info.mapping.rows;
        about["bins"]              = info.bins;
        json["info"]               = std::move(about);

        json["definition"] = nlohmann::ordered_json::parse(recording.definition.begin(), recording.definition.end(), nullptr,
                                                           false);
        if (json["definition"].is_discarded())
            json["definition"] = nullptr;

        nlohmann::ordered_json keymap = nlohmann::ordered_json::object();
        for (const auto& [cell, name] : recording.keymap)
            keymap[cell] = name;
        json["keymap"] = std::move(keymap);

        // As the tests' recordings: levels[controller column][controller row]; the time to the
        // microsecond, which is all a reading 155 ms apart needs.
        const size_t rows     = details.controllerRows;
        const size_t columns  = details.controllerColumns;
        nlohmann::ordered_json readings = nlohmann::ordered_json::array();
        for (size_t reading = 0; reading < recording.times.size(); ++reading)
        {
            nlohmann::ordered_json levels = nlohmann::ordered_json::array();
            for (size_t column = 0; column < columns; ++column)
            {
                nlohmann::ordered_json one = nlohmann::ordered_json::array();
                for (size_t row = 0; row < rows; ++row)
                {
                    const size_t position = column * rows + row;
                    one.push_back(position < recording.traces.size() && reading < recording.traces[position].size()
                                      ? recording.traces[position][reading]
                                      : 0);
                }
                levels.push_back(std::move(one));
            }
            nlohmann::ordered_json entry;
            entry["t"]      = std::round(recording.times[reading] * 1e6) / 1e6;
            entry["levels"] = std::move(levels);
            readings.push_back(std::move(entry));
        }
        json["readings"] = std::move(readings);

        return json.dump();
    }
}
