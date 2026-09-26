// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SettingsScreen - what the settings button shows, in place of the main area; the header
// stays, so the open board is still named (ui-design.md, screen 6). The host layout, the
// definitions -- official and user, with the library's Re-import, Restore previous, Remove
// and Import -- and, last, what Nazg runs on.
//
// It only draws and reports what was clicked; the library is changed by the caller. ImGui
// only, no SDL.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "library/NazgDefinitionLibrary.h"

namespace nazg
{
    // VIA's bundle, as far as the screen tells it.
    struct OfficialDefinitionsInfo
    {
        bool        found = false;
        int         count = -1;   // -1 when the bundle has no manifest to count from
        std::string path;         // where it was looked for
        std::string commit;       // of the-via/keyboards, when known
    };

    // Everything the screen shows, gathered by the caller.
    struct SettingsView
    {
        const DefinitionLibrary*        library = nullptr;   // null when it could not be opened
        std::string                     libraryError;        // why not
        std::string                     libraryFolder;
        const std::vector<std::string>* libraryMessages = nullptr;   // what the last changes did
        OfficialDefinitionsInfo         official;
        std::vector<std::string>        about;               // versions, GPU backend...
        const char*                     backLabel = "Back";
    };

    struct SettingsAction
    {
        bool                    back              = false;
        bool                    hostLayoutChanged = false;   // to be saved
        bool                    import            = false;
        std::optional<uint32_t> reimport;                    // library entry ids
        std::optional<uint32_t> restore;
        std::optional<uint32_t> remove;
    };

    // `hostLayoutId` is the setting, changed here; `showDemoWindow` the Dear ImGui demo's
    // switch, for whoever works on the look.
    [[nodiscard]] SettingsAction DrawSettings(const SettingsView& view, std::string& hostLayoutId,
                                              bool& showDemoWindow);
}
