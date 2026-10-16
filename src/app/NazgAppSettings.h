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
#include <vector>

#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"
#include "ui/NazgWorkspace.h"

namespace nazg
{
    // Settings that outlive a run. Kept in imgui.ini through ImGui's own settings-handler
    // hook -- one [Nazg][Settings] section -- rather than a settings file of Nazg's own,
    // which can come when there is more than one setting worth a file.
    struct AppSettings
    {
        // The host layout and the modifier names (ui/NazgKeycapLegend.h). The names start from
        // the computer's OS, as the platform says, and are the user's choice once saved.
        LegendSettings legends;

        // The look (ui/NazgTheme.h), by id. An id this build does not know -- saved by a newer
        // one -- is kept as it is, and drawn with the default.
        std::string theme        = "dark";
        std::string keycapStyle  = "outlined";
        std::string legendFamily = "cylindrical";

        // The board menu's Advanced submenu: tools for designers and debugging, out of an
        // ordinary user's way until asked for (ui-design.md, screen 6).
        bool advancedTools = false;

        // The board's share of the height under the strip, as the splitter under it was left, and
        // the section column's width.
        WorkspaceLayout workspace;

        // The keycode picker selects the next key after a pick -- off by default (ui-design.md,
        // "Hover, and after a pick").
        bool moveToNextKey = false;

        // Whether the user has said which layout the computer uses -- in Settings, or when a
        // macro first held text, which asks until then (ui-design.md, "The Macros section"). A
        // layout other than US saved by an earlier build counts as said.
        bool hostLayoutChosen = false;

        // Where the window was left: its place when not maximized, in the platform's window
        // coordinates, and whether it was maximized. A width of 0: never saved.
        struct WindowPlace
        {
            int  x         = 0;
            int  y         = 0;
            int  width     = 0;
            int  height    = 0;
            bool maximized = false;

            bool operator==(const WindowPlace&) const = default;
        } window;

        // Paths of VIA definition files that builds before the library remembered here.
        // Read, imported into the library once, then dropped -- written back only while
        // there is no library to import them into.
        std::vector<std::string> legacyViaDefinitions;
    };

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
