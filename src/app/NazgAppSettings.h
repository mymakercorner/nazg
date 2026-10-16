// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// AppSettings - what Nazg remembers between runs, and where: imgui.ini, in the data folder.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "settings/NazgSettings.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    // The look the settings ask for -- the defaults for ids this build does not know.
    [[nodiscard]] BoardStyle StyleOf(const AppSettings& settings);

    // Reads and writes `settings` with ImGui's own: call before ImGui first reads imgui.ini. The
    // settings must outlive the ImGui context.
    void RegisterSettings(AppSettings& settings);

    // Where ImGui keeps its settings -- window layout and Nazg's own. In the data folder; the
    // first time, an imgui.ini where Nazg was started is copied there, which is where earlier
    // builds kept it, so nothing is lost. The working directory's imgui.ini, the default, when
    // there is no data folder.
    [[nodiscard]] std::string SettingsFile(const std::optional<std::filesystem::path>& dataFolder);
}
