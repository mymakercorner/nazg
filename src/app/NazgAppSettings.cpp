// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgAppSettings.h"

#include <cstdio>
#include <cstring>
#include <system_error>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiSettingsHandler, to keep app settings in imgui.ini

#include "files/NazgFiles.h"

namespace nazg
{
    BoardStyle StyleOf(const AppSettings& settings)
    {
        BoardStyle style;
        style.theme   = ThemeFromId(settings.theme).value_or(style.theme);
        style.keycaps = KeycapStyleFromId(settings.keycapStyle).value_or(style.keycaps);
        style.legends = LegendFamilyFromId(settings.legendFamily).value_or(style.legends);
        return style;
    }

    void RegisterSettings(AppSettings& settings)
    {
        ImGuiSettingsHandler handler;
        handler.TypeName = "Nazg";
        handler.TypeHash = ImHashStr("Nazg");
        handler.UserData = &settings;

        handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void*
        {
            return std::strcmp(name, "Settings") == 0 ? reinterpret_cast<void*>(1) : nullptr;
        };

        handler.ReadInitFn = [](ImGuiContext*, ImGuiSettingsHandler* self)
        {
            static_cast<AppSettings*>(self->UserData)->legacyViaDefinitions.clear();
        };

        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* self, void*, const char* line)
        {
            AppSettings& loaded = *static_cast<AppSettings*>(self->UserData);

            constexpr char c_HostLayout[]    = "HostLayout=";
            constexpr char c_ModifierNames[] = "ModifierNames=";
            constexpr char c_Theme[]         = "Theme=";
            constexpr char c_KeycapStyle[]   = "Keycaps=";
            constexpr char c_LegendFamily[]  = "Legends=";
            constexpr char c_AdvancedTools[] = "AdvancedTools=";
            constexpr char c_ViaDefinition[] = "ViaDefinition=";
            constexpr char c_Window[]        = "Window=";
            constexpr char c_Maximized[]     = "Maximized=";
            constexpr char c_BoardHeight[]   = "BoardHeight=";
            constexpr char c_ColumnWidth[]   = "ColumnWidth=";
            constexpr char c_ColumnFolded[]  = "ColumnFolded=";
            constexpr char c_MoveToNextKey[] = "MoveToNextKey=";

            constexpr char c_HostLayoutChosen[] = "HostLayoutChosen=";

            if (std::strncmp(line, c_HostLayoutChosen, sizeof(c_HostLayoutChosen) - 1) == 0)
                loaded.hostLayoutChosen = loaded.hostLayoutChosen ||
                                          std::strcmp(line + sizeof(c_HostLayoutChosen) - 1, "1") == 0;
            else if (std::strncmp(line, c_HostLayout, sizeof(c_HostLayout) - 1) == 0)
            {
                loaded.legends.hostLayout = line + sizeof(c_HostLayout) - 1;
                loaded.hostLayoutChosen   = loaded.hostLayoutChosen || loaded.legends.hostLayout != "us";
            }
            else if (std::strncmp(line, c_ModifierNames, sizeof(c_ModifierNames) - 1) == 0)
                loaded.legends.modifierNames = ModifierNamesFromId(line + sizeof(c_ModifierNames) - 1)
                                                   .value_or(loaded.legends.modifierNames);
            else if (std::strncmp(line, c_Theme, sizeof(c_Theme) - 1) == 0)
                loaded.theme = line + sizeof(c_Theme) - 1;
            else if (std::strncmp(line, c_KeycapStyle, sizeof(c_KeycapStyle) - 1) == 0)
                loaded.keycapStyle = line + sizeof(c_KeycapStyle) - 1;
            else if (std::strncmp(line, c_LegendFamily, sizeof(c_LegendFamily) - 1) == 0)
                loaded.legendFamily = line + sizeof(c_LegendFamily) - 1;
            else if (std::strncmp(line, c_AdvancedTools, sizeof(c_AdvancedTools) - 1) == 0)
                loaded.advancedTools = std::strcmp(line + sizeof(c_AdvancedTools) - 1, "1") == 0;
            else if (std::strncmp(line, c_ViaDefinition, sizeof(c_ViaDefinition) - 1) == 0)
                loaded.legacyViaDefinitions.emplace_back(line + sizeof(c_ViaDefinition) - 1);
            else if (std::strncmp(line, c_Window, sizeof(c_Window) - 1) == 0)
            {
                AppSettings::WindowPlace place;
                if (std::sscanf(line + sizeof(c_Window) - 1, "%d,%d,%d,%d", &place.x, &place.y, &place.width,
                                &place.height) == 4 &&
                    place.width > 0 && place.height > 0)
                {
                    place.maximized = loaded.window.maximized;
                    loaded.window   = place;
                }
            }
            else if (std::strncmp(line, c_Maximized, sizeof(c_Maximized) - 1) == 0)
                loaded.window.maximized = std::strcmp(line + sizeof(c_Maximized) - 1, "1") == 0;
            else if (std::strncmp(line, c_BoardHeight, sizeof(c_BoardHeight) - 1) == 0)
            {
                float height = 0.0f;
                if (std::sscanf(line + sizeof(c_BoardHeight) - 1, "%f", &height) == 1 && height > 0.0f)
                    loaded.workspace.boardHeight = height;
            }
            else if (std::strncmp(line, c_ColumnWidth, sizeof(c_ColumnWidth) - 1) == 0)
            {
                float width = 0.0f;
                if (std::sscanf(line + sizeof(c_ColumnWidth) - 1, "%f", &width) == 1 && width > 0.0f)
                    loaded.workspace.columnWidth = width;
            }
            else if (std::strncmp(line, c_ColumnFolded, sizeof(c_ColumnFolded) - 1) == 0)
                loaded.workspace.isColumnFolded = std::strcmp(line + sizeof(c_ColumnFolded) - 1, "1") == 0;
            else if (std::strncmp(line, c_MoveToNextKey, sizeof(c_MoveToNextKey) - 1) == 0)
                loaded.moveToNextKey = std::strcmp(line + sizeof(c_MoveToNextKey) - 1, "1") == 0;
        };

        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out)
        {
            const AppSettings& saved = *static_cast<AppSettings*>(self->UserData);
            out->appendf("[%s][Settings]\nHostLayout=%s\nModifierNames=%s\nTheme=%s\nKeycaps=%s\nLegends=%s\n"
                         "AdvancedTools=%d\n",
                         self->TypeName, saved.legends.hostLayout.c_str(),
                         std::string(IdOf(saved.legends.modifierNames)).c_str(), saved.theme.c_str(),
                         saved.keycapStyle.c_str(), saved.legendFamily.c_str(), saved.advancedTools ? 1 : 0);
            // Only paths not yet imported -- when the library could not be opened -- so
            // none is lost before it can be.
            for (const std::string& path : saved.legacyViaDefinitions)
                out->appendf("ViaDefinition=%s\n", path.c_str());
            if (saved.window.width > 0)
                out->appendf("Window=%d,%d,%d,%d\nMaximized=%d\n", saved.window.x, saved.window.y,
                             saved.window.width, saved.window.height, saved.window.maximized ? 1 : 0);
            if (saved.workspace.boardHeight > 0.0f)
                out->appendf("BoardHeight=%.0f\n", saved.workspace.boardHeight);
            out->appendf("ColumnWidth=%.0f\nColumnFolded=%d\n", saved.workspace.columnWidth,
                         saved.workspace.isColumnFolded ? 1 : 0);
            out->appendf("MoveToNextKey=%d\n", saved.moveToNextKey ? 1 : 0);
            out->appendf("HostLayoutChosen=%d\n", saved.hostLayoutChosen ? 1 : 0);
            out->append("\n");
        };

        ImGui::AddSettingsHandler(&handler);   // copied by ImGui
    }

    std::string SettingsFile(const std::optional<std::filesystem::path>& dataFolder)
    {
        if (!dataFolder)
            return "imgui.ini";

        const std::filesystem::path settings = *dataFolder / "imgui.ini";

        std::error_code ignored;
        if (!std::filesystem::exists(settings, ignored) && std::filesystem::exists("imgui.ini", ignored))
            std::filesystem::copy_file("imgui.ini", settings, ignored);

        return Utf8FromPath(settings);
    }
}
