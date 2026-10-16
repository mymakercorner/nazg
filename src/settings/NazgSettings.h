// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Settings - what Nazg remembers between runs: settings.json in the data folder, one JSON
// object with a format number.
//
//   - Keys this build does not know are kept when it writes the file, so an older build never
//     erases a newer one's settings. A file of a newer format is read as far as it is understood
//     and never written.
//   - Written whole to settings.json.tmp, then renamed over the old one: a crash mid-write loses
//     nothing. A file that does not parse is copied to settings.json.unreadable before anything
//     replaces it.
//   - The first run without one reads the settings an earlier build kept in imgui.ini -- its
//     [Nazg][Settings] section -- writes settings.json, and leaves imgui.ini alone.
//
// When to write is the caller's: App writes a second after the last change, and on exit.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ui/NazgKeycapLegend.h"
#include "ui/NazgWorkspaceLayout.h"

namespace nazg
{
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

        // Paths of VIA definition files that builds before the library remembered. Imported into
        // the library once, then dropped -- kept only while there is no library to import them into.
        std::vector<std::string> legacyViaDefinitions;
    };

    // The settings as settings.json holds them, in the order it lists them -- what is kept between
    // runs, nothing of a frame's.
    [[nodiscard]] nlohmann::ordered_json ToJson(const AppSettings& settings);

    // And back: what the document lacks, or holds as the wrong type, keeps `defaults`'s value.
    [[nodiscard]] AppSettings FromJson(const nlohmann::ordered_json& document, AppSettings defaults);

    // An earlier build's settings: the [Nazg][Settings] section of its imgui.ini.
    [[nodiscard]] AppSettings FromImGuiIni(std::string_view ini, AppSettings defaults);

    class SettingsFile
    {
    public:
        // The format this build writes, and the newest it writes over.
        static constexpr int c_Format = 1;

        // settings.json in `folder` -- the working directory's when empty. `legacyIniFiles` are
        // where an earlier build's imgui.ini may be, in the order they are tried.
        SettingsFile(std::filesystem::path folder, std::vector<std::filesystem::path> legacyIniFiles);

        // The settings, `defaults` where the file says nothing. Without settings.json, an earlier
        // build's imgui.ini is read and settings.json written from it. Never throws: what went
        // wrong is in Problem().
        [[nodiscard]] AppSettings Load(const AppSettings& defaults);

        // Writes `settings`, keeping what the file held that this build does not know. False when
        // it could not, or must not -- a newer format -- with Problem() saying why.
        bool Save(const AppSettings& settings);

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

        // The last thing that went wrong reading or writing; empty when nothing did.
        [[nodiscard]] const std::string& Problem() const noexcept { return m_Problem; }

    private:
        std::filesystem::path              m_Path;
        std::vector<std::filesystem::path> m_LegacyIniFiles;
        nlohmann::ordered_json             m_Document = nlohmann::ordered_json::object();   // as last read or written
        bool                               m_IsWritable = true;
        std::string                        m_Problem;
    };
}
