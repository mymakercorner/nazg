// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Fonts - the interface's, the legends' and the icons', loaded into ImGui's atlas from a fonts
// folder -- fonts/ beside the executable, where the build copies resources/fonts/ (see its
// README.md) -- and handed to the theme, the board and the section column. ImGui only, so
// every backend loads them the same way.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <functional>
#include <string>

struct ImGuiIO;

namespace nazg
{
    // All three, the interface's first: the first font added is ImGui's default. `fontsFolder`
    // ends with a separator. Each font missing is said through `logError`, and replaced as
    // NazgFonts.cpp describes; nothing here fails.
    void LoadFonts(ImGuiIO& io, const std::string& fontsFolder, const std::function<void(const std::string&)>& logError);
}
