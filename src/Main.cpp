// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Nazg - one keyboard configurator to rule them all.
//
// Application entry point. This file owns every SDL and renderer call in the project.
// Keep it that way: it is what makes switching the renderer backend (SDL_GPU -> OpenGL3)
// or adding an Emscripten path a single-file change. See CLAUDE.md.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiSettingsHandler, to keep app settings in imgui.ini
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgCommunityBundle.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaBundle.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "app/NazgFonts.h"
#include "async/NazgTask.h"
#include "board/NazgBoardLoad.h"
#include "files/NazgFiles.h"
#include "library/NazgDefinitionLibrary.h"
#include "library/NazgLibraryActions.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgDefinitionPicker.h"
#include "ui/NazgKeyboardList.h"
#include "ui/NazgMatrixView.h"
#include "ui/NazgSectionFactory.h"
#include "ui/NazgSectionPlan.h"
#include "ui/NazgSettingsScreen.h"
#include "ui/NazgVialUnlock.h"
#include "ui/NazgTheme.h"
#include "ui/NazgWorkspace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr int   c_DefaultWindowWidth  = 1280;
    constexpr int   c_DefaultWindowHeight = 800;
    constexpr char  c_WindowTitle[]       = "Nazg";

    // What the device-list view renders. Owned by main(), so it outlives every
    // coroutine that writes to it -- coroutine frames outlive the calling scope, so
    // anything captured by reference across a co_await must be long-lived.
    struct DeviceListState
    {
        std::vector<nazg::HidDeviceInfo> devices;
        std::vector<std::string>         protocols;   // parallel to devices, see DrawKeyboardList()
        std::string                      error;
        bool                             isLoading = false;   // enumerating
        bool                             isProbing = false;   // asking each keyboard its protocol
    };

    // The payoff: an asynchronous sequence written as straight-line code. It suspends
    // at the co_await, the frame loop keeps rendering, and HidTransport::Pump()
    // resumes it on the main thread once the worker has the result.
    //
    // The list shows as soon as it is enumerated; each keyboard is then asked which protocol
    // it speaks, one Vial probe each -- harmless on a VIA board, which answers 0xFF. Nothing
    // opens a board until they have all answered: a second handle on the same device gets a
    // copy of every reply meant for the first -- HID delivers input reports to each open
    // handle -- and the transport discards such leftovers only when it opens a device.
    nazg::Task<void> RefreshDeviceList(nazg::HidTransport& transport, DeviceListState& state)
    {
        state.isLoading = true;
        state.error.clear();

        try
        {
            state.devices = co_await transport.Enumerate();
        }
        catch (const nazg::HidTransportError& failure)
        {
            state.devices.clear();
            state.error = failure.what();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "enumeration failed: %s", failure.what());
        }

        state.protocols.assign(state.devices.size(), std::string());
        state.isLoading = false;
        state.isProbing = true;

        for (size_t index = 0; index < state.devices.size(); ++index)
        {
            if (!nazg::IsViaInterface(state.devices[index]))
                continue;

            try
            {
                nazg::HidDeviceChannel channel = co_await nazg::HidDeviceChannel::Open(transport, state.devices[index].path);
                nazg::vial::Protocol protocol(channel);
                state.protocols[index] = (co_await protocol.Detect()) ? "Vial" : "VIA";
            }
            catch (const std::exception&)
            {
                // Raw HID without VIA behind it, or a board that went: it does not answer.
                state.protocols[index] = "no answer";
            }
        }

        state.isProbing = false;
    }

    // Settings that outlive a run. Kept in imgui.ini through ImGui's own settings-handler
    // hook -- one [Nazg][Settings] section -- rather than a settings file of Nazg's own,
    // which can come when there is more than one setting worth a file.
    struct AppSettings
    {
        // The host layout and the modifier names (ui/NazgKeycapLegend.h). The names start from
        // the computer's OS, PlatformModifierNames(), and are the user's choice once saved.
        nazg::LegendSettings legends;

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
        nazg::WorkspaceLayout workspace;

        // The keycode picker selects the next key after a pick -- off by default (ui-design.md,
        // "Hover, and after a pick").
        bool moveToNextKey = false;

        // Whether the user has said which layout the computer uses -- in Settings, or when a
        // macro first held text, which asks until then (ui-design.md, "The Macros section"). A
        // layout other than US saved by an earlier build counts as said.
        bool hostLayoutChosen = false;

        // Where the window was left: its place when not maximized, in SDL's window
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
    nazg::BoardStyle StyleOf(const AppSettings& settings)
    {
        nazg::BoardStyle style;
        style.theme   = nazg::ThemeFromId(settings.theme).value_or(style.theme);
        style.keycaps = nazg::KeycapStyleFromId(settings.keycapStyle).value_or(style.keycaps);
        style.legends = nazg::LegendFamilyFromId(settings.legendFamily).value_or(style.legends);
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
                loaded.legends.modifierNames = nazg::ModifierNamesFromId(line + sizeof(c_ModifierNames) - 1)
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
                         std::string(nazg::IdOf(saved.legends.modifierNames)).c_str(), saved.theme.c_str(),
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

    // The window where it was left, if that is still on a display -- its top-left corner, where
    // the title bar is, in some display's usable area -- and no bigger than that area. Otherwise,
    // and on the first run, centred on the main display and as large as most of it: a full-size
    // board needs more than 1280 px (ui-design.md, "The floor is on the header").
    void PlaceWindow(SDL_Window* window, const AppSettings::WindowPlace& saved, float scale)
    {
        SDL_Rect usable{};
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);

        bool onDisplay = false;
        if (saved.width > 0)
        {
            int                  count    = 0;
            SDL_DisplayID* const displays = SDL_GetDisplays(&count);
            for (int i = 0; i < count && !onDisplay; ++i)
            {
                SDL_Rect bounds{};
                if (SDL_GetDisplayUsableBounds(displays[i], &bounds) && saved.x >= bounds.x &&
                    saved.y >= bounds.y && saved.x < bounds.x + bounds.w && saved.y < bounds.y + bounds.h)
                {
                    usable    = bounds;
                    onDisplay = true;
                }
            }
            SDL_free(displays);
        }

        int width  = std::max(static_cast<int>(c_DefaultWindowWidth * scale), usable.w * 85 / 100);
        int height = std::max(static_cast<int>(c_DefaultWindowHeight * scale), usable.h * 85 / 100);
        if (saved.width > 0)
        {
            width  = saved.width;
            height = saved.height;
        }
        SDL_SetWindowSize(window, std::min(width, usable.w), std::min(height, usable.h));

        if (onDisplay)
            SDL_SetWindowPosition(window, saved.x, saved.y);
        else
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        if (saved.maximized)
            SDL_MaximizeWindow(window);
    }

    // Keep the settings in step with the window: its place while it is neither maximized nor
    // minimized -- so a maximized window comes back to it when restored -- and whether it is
    // maximized. Saved by ImGui with the rest, a few seconds after a change and on exit.
    void TrackWindowPlace(SDL_Window* window, AppSettings::WindowPlace& saved)
    {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
        if ((flags & SDL_WINDOW_MINIMIZED) != 0)
            return;

        AppSettings::WindowPlace place = saved;
        place.maximized                = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (!place.maximized)
        {
            SDL_GetWindowPosition(window, &place.x, &place.y);
            SDL_GetWindowSize(window, &place.width, &place.height);
        }

        if (!(place == saved))
        {
            saved = place;
            ImGui::MarkIniSettingsDirty();
        }
    }

    // The modifier names of the OS Nazg runs on, for a first launch (ui-design.md, "Modifier
    // names follow a setting"): a keyboard is configured on the computer it is used with.
    nazg::ModifierNames PlatformModifierNames()
    {
        const std::string platform = SDL_GetPlatform();
        if (platform == "Windows")
            return nazg::ModifierNames::Windows;
        if (platform == "macOS")
            return nazg::ModifierNames::Mac;
        return nazg::ModifierNames::Linux;
    }

    // Now, as the library records it: ISO 8601, UTC.
    std::string NowUtc()
    {
        SDL_Time     now = 0;
        SDL_DateTime date{};
        if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &date, false))
            return "unknown";

        char text[32];
        std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", date.year, date.month, date.day, date.hour,
                      date.minute, date.second);
        return text;
    }

    // Nazg's data folder, the per-user one SDL picks and creates: %APPDATA%\mymakercorner\Nazg
    // on Windows. It holds the settings (imgui.ini) and the user definitions, so every build
    // and every working directory shares them. Nullopt, with SDL's reason, when there is none.
    std::optional<std::filesystem::path> DataFolder(std::string& error)
    {
        char* prefPath = SDL_GetPrefPath("mymakercorner", "Nazg");
        if (prefPath == nullptr)
        {
            error = std::string("no folder for Nazg's data: ") + SDL_GetError();
            return std::nullopt;
        }

        const std::filesystem::path folder = nazg::PathFromUtf8(prefPath);
        SDL_free(prefPath);
        return folder;
    }

    // Where ImGui keeps its settings -- window layout and Nazg's own, see RegisterSettings().
    // In the data folder; the first time, an imgui.ini where Nazg was started is copied there,
    // which is where earlier builds kept it, so nothing is lost. The working directory's
    // imgui.ini, the default, when there is no data folder.
    std::string SettingsFile(const std::optional<std::filesystem::path>& dataFolder)
    {
        if (!dataFolder)
            return "imgui.ini";

        const std::filesystem::path settings = *dataFolder / "imgui.ini";

        std::error_code ignored;
        if (!std::filesystem::exists(settings, ignored) && std::filesystem::exists("imgui.ini", ignored))
            std::filesystem::copy_file("imgui.ini", settings, ignored);

        return nazg::Utf8FromPath(settings);
    }

    // The VIA definitions bundled with Nazg, beside the executable, each read and inflated once
    // at start and kept, so opening a board is a lookup:
    //  - the official ones, VIA's: 0.3 MB, ~40 ms on a fast desktop, 28 MB kept
    //    (adapters/via/NazgViaBundle.h), built by tools/update_via_bundle.py;
    //  - the community ones, vendors' files for boards VIA lacks or has older: 0.1 MB, 9 MB kept
    //    (adapters/via/NazgCommunityBundle.h), fetched by tools/update_community_bundle.py.
    // Copied there by the build, shipped with releases; a checkout that never ran the tools has
    // none, and VIA boards then need a user definition.
    constexpr char c_ViaBundleName[]       = "via_definitions.tar.xz";
    constexpr char c_CommunityBundleName[] = "community_definitions.tar.xz";

    struct ViaBundle
    {
        std::string                                         path;
        std::optional<nazg::via::DefinitionBundle>          definitions;   // empty when missing or corrupt
        std::string                                         communityPath;
        std::optional<nazg::via::CommunityDefinitionBundle> community;     // likewise
    };

    ViaBundle ReadViaBundle()
    {
        ViaBundle bundle;

        const char* base     = SDL_GetBasePath();   // owned by SDL, ends with a separator
        bundle.path          = std::string(base != nullptr ? base : "") + c_ViaBundleName;
        bundle.communityPath = std::string(base != nullptr ? base : "") + c_CommunityBundleName;

        // A bundle that does not inflate is as good as none.
        if (const auto bytes = nazg::ReadWholeFile(nazg::PathFromUtf8(bundle.path)))
            try
            {
                bundle.definitions.emplace(*bytes);
            }
            catch (const std::exception& failure)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", bundle.path.c_str(), failure.what());
            }

        if (const auto bytes = nazg::ReadWholeFile(nazg::PathFromUtf8(bundle.communityPath)))
            try
            {
                bundle.community.emplace(*bytes);
            }
            catch (const std::exception& failure)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", bundle.communityPath.c_str(), failure.what());
            }

        return bundle;
    }

    // SDL may run a file dialog's callback on another thread, so the chosen paths wait
    // here until the frame loop collects them.
    struct PendingDialogPaths
    {
        std::mutex               mutex;
        std::vector<std::string> paths;
    };

    void SDLCALL OnViaDefinitionChosen(void* userdata, const char* const* filelist, int)
    {
        if (filelist == nullptr)   // an error; SDL_GetError() says which
            return;

        auto&                       pending = *static_cast<PendingDialogPaths*>(userdata);
        const std::lock_guard<std::mutex> lock(pending.mutex);
        for (const char* const* path = filelist; *path != nullptr; ++path)
            pending.paths.emplace_back(*path);
    }

    // Windows runs a loop of its own while the window is dragged -- moved or resized -- and the
    // frame loop in main() stops until the mouse is released: a resized window would stretch its
    // last frame meanwhile. SDL keeps sending SDL_EVENT_WINDOW_EXPOSED from inside that loop --
    // a 10 ms timer, which Windows' tick stretches to about 16 -- with data1 set to 1, on the
    // main thread; an event watch sees it at once and
    // may draw right there. `userdata` is main()'s frame function, told the frame is a live one.
    bool SDLCALL DrawDuringLiveResize(void* userdata, SDL_Event* event)
    {
        if (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == 1)
            (*static_cast<std::function<void(bool)>*>(userdata))(true);
        return true;   // ignored for event watches
    }

    // "Export definition...": what the save dialog is for, and where the user chose to write
    // it. The bytes are taken when Export is clicked -- a user definition's stored file, the
    // official one out of the bundle, or what a Vial board served -- so nothing has to be looked
    // up again once a place is chosen. One dialog at a time; another Export waits for it to close.
    struct PendingExport
    {
        std::mutex                 mutex;
        bool                       isOpen = false;
        std::vector<uint8_t>       bytes;
        std::string                name;        // the definition's, for the message
        std::string                suggested;   // the file name offered; SDL may read it until it closes
        std::optional<std::string> path;        // chosen, not yet written
    };

    void SDLCALL OnExportChosen(void* userdata, const char* const* filelist, int)
    {
        auto&                             pending = *static_cast<PendingExport*>(userdata);
        const std::lock_guard<std::mutex> lock(pending.mutex);

        pending.isOpen = false;
        if (filelist != nullptr && *filelist != nullptr)   // null: an error; empty: cancelled
            pending.path = *filelist;
    }

    // The board being shown. Same lifetime rule as DeviceListState: owned by main().
    struct BoardState
    {
        std::optional<nazg::Keyboard> keyboard;
        std::string                   path;               // where it was opened, for writes
        std::string                   definitionSource;   // where its definition came from, shown with it
        std::string                   error;
        bool                          isLoading = false;

        // A VIA board's definition is chosen among candidates; a Vial board carries its own.
        // Set for the board shown, or the one the picker is asking about.
        bool                                     isVia = false;
        nazg::DeviceIdentity                     identity;
        std::vector<nazg::DefinitionCandidate>   candidates;   // ranked, as the picker shows them
        std::vector<nazg::DefinitionCandidate>   bundled;      // VIA's and the community's, kept to rebuild the list
        std::optional<nazg::DefinitionRef>       inUse;        // the one drawing the board

        // A Vial board's lock, as it was when the board loaded -- Nazg cannot unlock yet, so
        // only another app or a restart changes it meanwhile. Unset on a VIA board.
        std::optional<nazg::vial::UnlockStatus> lock;
        bool                                     isChoosing = false;   // the picker is showing
        std::vector<std::string>                 foreignImports;       // the picker's notes: files just imported for another keyboard

        // The definition drawing the board as its source has it, when that is VIA's bundle or
        // the board itself -- for "Export definition...". Empty for a user definition, whose
        // stored copy is read from the library when exported.
        std::vector<uint8_t> exportable;
        std::string          exportMessage;

        // What the board screen shows, Keymap first (ui/NazgSection.h), and the one chosen.
        // Made for the keyboard above once it has loaded, and dropped when a load starts.
        std::vector<std::unique_ptr<nazg::Section>> sections;
        size_t                                      activeSection = 0;

        // The matrix view among the sections, last, while Advanced tools is on -- or null. Its
        // live test keeps the board open: anything else that talks to the board stops the test
        // first.
        nazg::MatrixView* matrix = nullptr;

        // An unlock under way, while the board screen shows it instead of the sections.
        // Behind a pointer: it holds a Task that refers to it, so it must never move.
        std::unique_ptr<nazg::VialUnlock> unlock;

        // A section still working, or an unlock, refers to the keyboard: nothing reloads it
        // meanwhile.
        bool IsWorking() const
        {
            return unlock != nullptr ||
                   std::any_of(sections.begin(), sections.end(), [](const auto& section) { return section->IsBusy(); });
        }
    };

    nazg::DeviceIdentity IdentityOf(const nazg::HidDeviceInfo& device)
    {
        return { device.vendorId, device.productId, device.manufacturer, device.product, device.releaseNumber,
                 device.serialNumber };
    }

    std::string DescribeInUse(const nazg::DefinitionCandidate& candidate)
    {
        return (candidate.definition.name.empty() ? "(unnamed)" : candidate.definition.name) + " -- " +
               nazg::DescribeDefinitionSource(candidate);
    }

    // The shown VIA board's candidates again, after the library changed: an import appears
    // in the picker at once, a removal leaves it.
    void RefreshCandidates(const nazg::UserLibrary& library, BoardState& state)
    {
        if (!state.isVia || !library.library)
            return;

        try
        {
            state.candidates = library.library->Candidates(state.identity.vendorId, state.identity.productId);
        }
        catch (const std::exception& failure)
        {
            state.error = std::string("a user definition for this board is unreadable: ") + failure.what();
            return;
        }

        state.candidates.insert(state.candidates.end(), state.bundled.begin(), state.bundled.end());
        nazg::RankCandidates(state.candidates, state.identity.product);
    }

    // Lock a Vial board again, and read its lock back -- what the header shows.
    nazg::Task<void> LockBoard(nazg::HidTransport& transport, std::string path, BoardState& state)
    {
        try
        {
            nazg::HidDeviceChannel channel = co_await nazg::HidDeviceChannel::Open(transport, path);
            nazg::vial::Protocol vial(channel);
            co_await vial.Lock();
            state.lock = co_await vial.GetUnlockStatus();
        }
        catch (const std::exception& failure)
        {
            state.error = std::string("the board could not be locked: ") + failure.what();
        }
    }

    // Open, load everything (board/NazgBoardLoad.h), close. The user definitions the caller read
    // for the board's VID:PID are passed by value, so the load does not depend on the library
    // staying put; the bundles are main()'s, and outlive this coroutine. `chosen` is the
    // remembered choice, if any; when it does not settle which candidate, the picker asks.
    nazg::Task<void> LoadBoard(nazg::HidTransport&                    transport,
                               std::string                            path,
                               nazg::DeviceIdentity                   identity,
                               std::vector<nazg::DefinitionCandidate> candidates,
                               std::optional<nazg::DefinitionRef>     chosen,
                               const ViaBundle&                       bundle,
                               BoardState&                            state)
    {
        state.isLoading  = true;
        state.isChoosing = false;
        state.error.clear();

        // The board shown until now goes, sections first: they refer to it. The caller made
        // sure none is busy. A load that fails leaves no board, and the list says why.
        state.sections.clear();
        state.matrix = nullptr;
        state.keyboard.reset();
        state.lock.reset();
        if (state.path != path)
            state.foreignImports.clear();   // they were about another board
        state.path     = path;
        state.identity = identity;

        try
        {
            nazg::HidDeviceChannel channel = co_await nazg::HidDeviceChannel::Open(transport, path);
            nazg::vial::Protocol protocol(channel);

            nazg::BoardLoad load = co_await nazg::LoadBoard(
                protocol, identity, std::move(candidates), std::move(chosen),
                { bundle.definitions ? &*bundle.definitions : nullptr, bundle.community ? &*bundle.community : nullptr });
            for (const std::string& skipped : load.skipped)
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", skipped.c_str());

            state.keyboard   = std::move(load.keyboard);
            state.lock       = load.lock;
            state.isVia      = !load.isVial;
            state.isChoosing = !load.isVial && !state.keyboard;
            state.candidates = std::move(load.candidates);
            state.bundled    = std::move(load.bundled);
            state.exportable = std::move(load.exportable);
            state.inUse.reset();
            state.definitionSource = load.isVial ? "from the board (Vial)" : std::string();
            if (load.used)
            {
                state.inUse            = state.candidates[*load.used].ref;
                state.definitionSource = DescribeInUse(state.candidates[*load.used]);
            }

            state.activeSection = 0;
            state.exportMessage.clear();
        }
        catch (const std::exception& failure)
        {
            state.error = failure.what();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "loading the board failed: %s", failure.what());
        }

        state.isLoading = false;
    }
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init() failed: %s", SDL_GetError());
        return 1;
    }

    // Create the window, sized in logical units scaled by the display's content scale.
    const float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* pWindow = SDL_CreateWindow(c_WindowTitle,
                                           static_cast<int>(c_DefaultWindowWidth  * mainScale),
                                           static_cast<int>(c_DefaultWindowHeight * mainScale),
                                           windowFlags);
    if (pWindow == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow() failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create the GPU device. SDL picks the backend per platform: Metal on macOS,
    // D3D12 on Windows, Vulkan on Linux. We accept every shader format so SDL is free
    // to choose; ImGui ships its own precompiled shaders, so we supply none ourselves.
    SDL_GPUDevice* pGpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                                    SDL_GPU_SHADERFORMAT_DXIL |
                                                    SDL_GPU_SHADERFORMAT_MSL |
                                                    SDL_GPU_SHADERFORMAT_METALLIB,
                                                    true, nullptr);
    if (pGpuDevice == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice() failed: %s", SDL_GetError());
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(pGpuDevice, pWindow))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ClaimWindowForGPUDevice() failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(pGpuDevice);
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return 1;
    }
    SDL_SetGPUSwapchainParameters(pGpuDevice, pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    const char* pGpuDriver = SDL_GetGPUDeviceDriver(pGpuDevice);
    SDL_Log("GPU backend: %s", pGpuDriver != nullptr ? pGpuDriver : "unknown");

    // Settings and user definitions both live here. The settings path outlives the ImGui
    // context -- declared before it -- since ImGui only keeps a pointer to it and writes the
    // file one last time in DestroyContext().
    std::string                                dataFolderError;
    const std::optional<std::filesystem::path> dataFolder   = DataFolder(dataFolderError);
    const std::string                          settingsFile = SettingsFile(dataFolder);

    // Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = settingsFile.c_str();

    // Before the first NewFrame(), which is when ImGui reads imgui.ini.
    AppSettings settings;
    settings.legends.modifierNames = PlatformModifierNames();
    RegisterSettings(settings);

    // Now rather than at the first NewFrame(), so the window shows where it was left.
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    PlaceWindow(pWindow, settings.window, mainScale);
    SDL_ShowWindow(pWindow);

    // From fonts/ beside the executable (app/NazgFonts.h).
    const char* basePath = SDL_GetBasePath();   // owned by SDL, ends with a separator
    nazg::LoadFonts(io, std::string(basePath != nullptr ? basePath : "") + "fonts/",
                    [](const std::string& text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); });

    ImGui::StyleColorsDark();
    nazg::ApplyWindowSizes(mainScale);

    ImGui_ImplSDL3_InitForSDLGPU(pWindow);

    ImGui_ImplSDLGPU3_InitInfo initInfo = {};
    initInfo.Device             = pGpuDevice;
    initInfo.ColorTargetFormat  = SDL_GetGPUSwapchainTextureFormat(pGpuDevice, pWindow);
    initInfo.MSAASamples        = SDL_GPU_SAMPLECOUNT_1;
    initInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    initInfo.PresentMode        = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&initInfo);

    // Owns a worker thread; every hidapi call happens there, never on this thread.
    nazg::HidTransport transport;

    DeviceListState  deviceListState;
    nazg::Task<void> refreshTask = RefreshDeviceList(transport, deviceListState);

    // Before loadTask, which holds a reference to it, so it is destroyed after.
    const ViaBundle viaBundle = ReadViaBundle();

    BoardState       boardState;
    nazg::Task<void> loadTask;
    nazg::Task<void> lockTask;

    // Leaving the board -- for another, the list, another definition, or quitting -- while a section
    // holds changes not written yet: the user is asked, Save, Discard or Cancel, and the leaving
    // waits in `pendingLeave` (ui-design.md, "The Macros section"). Saving first runs it once every
    // write is done and nothing is left unsaved; discarding, once no section is working -- leaving
    // drops the sections, and a running Task must never be destroyed.
    std::function<void()> pendingLeave;
    bool                  savingBeforeLeave     = false;
    bool                  discardingBeforeLeave = false;
    bool                  quitRequested     = false;   // the window's close, heard by the event loop
    bool                  quitConfirmed     = false;   // and allowed

    const auto hasUnsaved = [&]
    {
        return std::any_of(boardState.sections.begin(), boardState.sections.end(),
                           [](const auto& section) { return section->HasUnsavedChanges(); });
    };
    const auto leaveBoard = [&](std::function<void()> leave)
    {
        if (!hasUnsaved())
        {
            leave();
            return;
        }
        pendingLeave          = std::move(leave);
        savingBeforeLeave     = false;
        discardingBeforeLeave = false;
    };

    nazg::UserLibrary  library = nazg::OpenUserLibrary(dataFolder, dataFolderError);
    PendingDialogPaths pendingPaths;    // must outlive any open dialog: main() scope
    PendingExport      pendingExport;   // likewise

    // Never while loadTask still runs -- see the Refresh button -- nor while a section works on
    // the board a load would replace. The user definitions for the board's VID:PID are read
    // here, a few KB each, and the remembered choice looked up unless the caller has just
    // made one.
    const auto isLoadRunning = [&] { return loadTask.IsValid() && !loadTask.IsDone(); };
    const auto isBoardBusy   = [&]
    {
        return isLoadRunning() || (lockTask.IsValid() && !lockTask.IsDone()) || boardState.IsWorking();
    };

    const auto startLoad = [&](const std::string& path, const nazg::DeviceIdentity& identity,
                               std::optional<nazg::DefinitionRef> chosen)
    {
        std::vector<nazg::DefinitionCandidate> candidates;
        try
        {
            if (library.library)
            {
                candidates = library.library->Candidates(identity.vendorId, identity.productId);
                if (!chosen)
                    if (const nazg::DefinitionChoice* choice = library.library->FindChoice(identity))
                        chosen = choice->definition;
            }
        }
        catch (const std::exception& failure)
        {
            boardState.error = std::string("a user definition for this board is unreadable: ") + failure.what();
            return;
        }

        loadTask = LoadBoard(transport, path, identity, std::move(candidates), std::move(chosen), viaBundle, boardState);
    };

    // After the library changed: the picker's list follows, and a board waiting for a
    // definition loads by itself once exactly one candidate is left -- the connect rule. A
    // board drawn by an entry given a new version (`replaced`) is loaded again with it,
    // keymap included, since the matrix may have changed with it.
    const auto afterLibraryChange = [&](std::optional<uint32_t> replaced = std::nullopt)
    {
        if (isBoardBusy())
            return;

        RefreshCandidates(library, boardState);

        if (replaced && boardState.keyboard && boardState.inUse == nazg::DefinitionRef::User(*replaced))
            startLoad(boardState.path, boardState.identity, boardState.inUse);
        else if (boardState.isChoosing && !boardState.keyboard &&
                 nazg::ResolveCandidate(boardState.candidates, std::nullopt))
            startLoad(boardState.path, boardState.identity, std::nullopt);
    };

    // An import while the picker asks about a VIA board, for another VID:PID: the picker says
    // why it is not among the candidates.
    const auto noteForeignImport = [&](const std::string& path, const nazg::LibraryEntry& entry)
    {
        if (boardState.isChoosing && boardState.isVia &&
            (entry.vendorId != boardState.identity.vendorId || entry.productId != boardState.identity.productId))
            boardState.foreignImports.push_back(nazg::ForeignImportNote(path, entry, boardState.identity));
    };

    const auto showImportDialog = [&]
    {
        // Static: SDL may read the filters until the dialog closes.
        static const SDL_DialogFileFilter c_Filters[] = { { "VIA definition", "json" } };
        SDL_ShowOpenFileDialog(OnViaDefinitionChosen, &pendingPaths, pWindow, c_Filters, 1, nullptr, true);
    };

    // Opens the save dialog for these bytes, unless one is open already.
    const auto startExport = [&](std::vector<uint8_t> bytes, const std::string& name, std::string suggested)
    {
        {
            const std::lock_guard<std::mutex> lock(pendingExport.mutex);
            if (pendingExport.isOpen)
                return;
            pendingExport.isOpen    = true;
            pendingExport.bytes     = std::move(bytes);
            pendingExport.name      = name;
            pendingExport.suggested = std::move(suggested);
        }

        // Outside the lock: SDL may call back before returning.
        static const SDL_DialogFileFilter c_Filters[] = { { "VIA definition", "json" } };
        const char* suggestedName = pendingExport.suggested.empty() ? nullptr : pendingExport.suggested.c_str();
        SDL_ShowSaveFileDialog(OnExportChosen, &pendingExport, pWindow, c_Filters, 1, suggestedName);
    };

    // Same rule as for loadTask: a Task still running must not be replaced -- destroying it
    // would free a coroutine frame the transport still holds a handle to.
    const auto startRefresh = [&]
    {
        if (!refreshTask.IsValid() || refreshTask.IsDone())
            refreshTask = RefreshDeviceList(transport, deviceListState);
    };

    // The library entry drawing the open board, if a user definition does.
    const auto userEntryInUse = [&]() -> const nazg::LibraryEntry*
    {
        if (!boardState.inUse || boardState.inUse->kind != nazg::DefinitionRef::Kind::User || !library.library)
            return nullptr;
        return library.library->Find(boardState.inUse->userId);
    };

    // For investigation and debugging: the definition drawing the board, byte for byte as
    // Nazg has it -- VIA's bundle, the board itself, or the library's stored copy, which
    // offers the name of the file it came from.
    const auto exportBoardDefinition = [&]
    {
        try
        {
            if (const nazg::LibraryEntry* entry = userEntryInUse())
                startExport(library.library->Read(*entry), entry->name, nazg::FileName(entry->origin));
            else if (boardState.keyboard && !boardState.exportable.empty())
                startExport(boardState.exportable, boardState.keyboard->Name(),
                            nazg::ExportFileName(boardState.keyboard->Name()));
        }
        catch (const std::exception& failure)
        {
            boardState.exportMessage = std::string("not exported: ") + failure.what();
        }
    };

    const ImVec4 clearColor = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    bool showAllHidDevices = false;   // the keyboard list shows only keyboards unless asked
    bool showSettings      = false;   // settings in place of the main area

    // The window's colours are set when the theme changes -- first once imgui.ini is read.
    std::optional<nazg::ThemeId> appliedTheme;
    // The window's minimum size, as last given to SDL: the open board's, so resizing never
    // shrinks it (NazgWorkspace.h, WorkspaceLayout); none on the other screens.
    int appliedMinWidth  = 0;
    int appliedMinHeight = 0;
    bool openLoneBoard     = true;    // until the first list is in
    // One frame: finished transport work resumed, dialog results collected, then the UI drawn
    // and presented. Run by the loop below, and from inside SDL's event pumping while the
    // window is being moved or resized -- `isLive`, see DrawDuringLiveResize(). Never inside
    // itself.
    //
    // A live frame never waits for the display: Windows handles the mouse in the same loop, which
    // a wait for VSYNC -- up to 16.7 ms at 60 Hz -- would hold up on every tick. It is skipped
    // instead when the swapchain has no texture ready, and the next tick tries again. (Dragging
    // the window still jitters at 60 Hz, for a reason outside Nazg: ui-design.md, "Open points".)
    bool                      isDrawing = false;
    std::function<void(bool)> drawFrame = [&](bool isLive)
    {
        if (isDrawing)
            return;
        isDrawing = true;

        // Pump before the minimized early-out below, otherwise transport work stalls
        // for as long as the window stays minimized.
        transport.Pump();

        // A place picked in the export dialog since last frame gets the definition.
        {
            std::optional<std::string> path;
            std::vector<uint8_t>       bytes;
            std::string                name;
            {
                const std::lock_guard<std::mutex> lock(pendingExport.mutex);
                path.swap(pendingExport.path);
                if (path)
                {
                    bytes.swap(pendingExport.bytes);
                    name = pendingExport.name;
                }
            }
            if (path)
                boardState.exportMessage = nazg::WriteExport(bytes, name, *path);
        }

        // Files picked in the import dialog since last frame go into the library.
        {
            std::vector<std::string> picked;
            {
                const std::lock_guard<std::mutex> lock(pendingPaths.mutex);
                picked.swap(pendingPaths.paths);
            }
            if (!picked.empty() && library.library)
            {
                boardState.foreignImports.clear();
                for (const auto& [path, entry] : nazg::ImportDefinitions(library, picked, true, NowUtc()))
                    noteForeignImport(path, entry);
                afterLibraryChange();
            }
        }

        // Paths an earlier build remembered in imgui.ini, read before the window shows:
        // imported once, then dropped from the settings. Kept if there is no library.
        if (!settings.legacyViaDefinitions.empty() && library.library)
        {
            nazg::ImportDefinitions(library, settings.legacyViaDefinitions, false, NowUtc());
            settings.legacyViaDefinitions.clear();
            ImGui::MarkIniSettingsDirty();
        }

        if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED) != 0)
        {
            SDL_Delay(10);
            isDrawing = false;
            return;
        }

        // The matrix live test asks for every key of the board to be pressed, and they all type
        // into Nazg: with keyboard navigation on, an arrow then Space would click whatever has
        // focus, and Alt would open the menu bar. Off while the test runs.
        if (boardState.matrix && boardState.matrix->IsLiveTestRunning())
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // The look, as the settings say: read from imgui.ini by the first NewFrame(), changed in
        // Settings.
        const nazg::BoardStyle look = StyleOf(settings);
        if (appliedTheme != look.theme)
        {
            nazg::ApplyWindowTheme(look.theme);
            appliedTheme = look.theme;
        }
        nazg::SetBoardStyle(look);

        // With exactly one keyboard plugged in at start, Nazg opens it: most people never see
        // the list. Only the first time -- afterwards the list is shown because it was asked for.
        if (openLoneBoard && !deviceListState.isLoading && !deviceListState.isProbing)
        {
            openLoneBoard = false;

            const auto keyboards = std::count_if(deviceListState.devices.begin(), deviceListState.devices.end(),
                                                 nazg::IsViaInterface);
            if (keyboards == 1 && !isBoardBusy())
            {
                const auto lone = std::find_if(deviceListState.devices.begin(), deviceListState.devices.end(),
                                               nazg::IsViaInterface);
                startLoad(lone->path, IdentityOf(*lone), std::nullopt);
            }
        }

        // One window filling SDL's: the header, then settings, the open board or the keyboard
        // list under it (ui-design.md, "The regions").
        bool isBoardShown = false;
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            constexpr ImGuiWindowFlags c_Workspace = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                                     ImGuiWindowFlags_NoSavedSettings |
                                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;
            ImGui::Begin("##workspace", nullptr, c_Workspace);
            ImGui::PopStyleVar(2);

            const bool hasBoard = boardState.isLoading || boardState.keyboard.has_value() || boardState.isChoosing;
            const bool isBusy   = isBoardBusy();

            // --- The header -------------------------------------------------------------

            nazg::HeaderView    header;
            std::vector<size_t> others;   // device index of each "Switch to" entry
            if (hasBoard)
            {
                header.name = boardState.keyboard ? boardState.keyboard->Name() : boardState.identity.product;
                if (header.name.empty())
                    header.name = "(unnamed)";

                if (boardState.keyboard)
                {
                    // Always at hand, so a wrong definition is plain to see -- and cheap to undo:
                    // the keymap lives in the board, the definition only draws it.
                    header.protocol  = boardState.isVia ? "VIA" : "Vial";
                    header.details   = "Definition: " + boardState.definitionSource + "\nQMK keycodes " +
                                     nazg::qmk::KeycodeVersionName(boardState.keyboard->keycodeVersion);
                    header.isVia     = boardState.isVia;
                    header.hasChoice = boardState.isVia && library.library &&
                                       library.library->FindChoice(boardState.identity) != nullptr;
                    header.canExport = !boardState.exportable.empty() || userEntryInUse() != nullptr;

                    header.hasAdvanced = settings.advancedTools;

                    // A VIAL_INSECURE build reports itself unlocked and has no combo to unlock
                    // with: it has no lock, and the header says nothing.
                    if (boardState.lock && !(boardState.lock->unlocked && boardState.lock->unlockKeys.empty()))
                        header.isLocked = !boardState.lock->unlocked;
                }

                for (size_t index = 0; index < deviceListState.devices.size(); ++index)
                {
                    const nazg::HidDeviceInfo& device = deviceListState.devices[index];
                    if (!nazg::IsViaInterface(device) || device.path == boardState.path)
                        continue;
                    others.push_back(index);
                    header.others.push_back({ device.product.empty() ? "(unnamed)" : device.product,
                                              index < deviceListState.protocols.size()
                                                  ? deviceListState.protocols[index]
                                                  : std::string() });
                }

                header.isBusy = isBusy;
            }
            header.isSettingsShown = showSettings;

            const nazg::HeaderAction headerAction = nazg::DrawHeader(header);

            if (headerAction.settings)
                showSettings = !showSettings;

            if (headerAction.switchTo && !isBusy)
            {
                const nazg::HidDeviceInfo& device   = deviceListState.devices[others[*headerAction.switchTo]];
                const std::string          path     = device.path;
                const nazg::DeviceIdentity identity = IdentityOf(device);
                leaveBoard([&, path, identity]
                {
                    showSettings = false;
                    startLoad(path, identity, std::nullopt);
                });
            }

            if (headerAction.changeDefinition && !isBusy)
                leaveBoard([&]
                {
                    showSettings          = false;
                    boardState.isChoosing = true;
                });

            if (headerAction.forgetChoice && library.library)
            {
                try
                {
                    library.library->Forget(boardState.identity);
                }
                catch (const std::exception& failure)
                {
                    boardState.error = std::string("the choice could not be forgotten: ") + failure.what();
                }
            }

            // Locked: unlock, on its own screen. Unlocked: lock again at once.
            if (headerAction.toggleLock && !isBusy && boardState.keyboard && boardState.lock)
            {
                showSettings = false;
                boardState.error.clear();
                if (boardState.matrix)
                    boardState.matrix->StopLiveTest();
                if (!boardState.lock->unlocked)
                    boardState.unlock = std::make_unique<nazg::VialUnlock>(
                        transport, boardState.path, *boardState.keyboard, boardState.lock->unlockKeys, settings.legends);
                else
                    lockTask = LockBoard(transport, boardState.path, boardState);
            }

            if (headerAction.exportDefinition)
                exportBoardDefinition();

            // Closes the board, and lists what is plugged in now.
            if (headerAction.allKeyboards && !isBusy)
                leaveBoard([&]
                {
                    boardState   = BoardState{};
                    showSettings = false;
                    startRefresh();
                });

            // Quitting, once the board may be left.
            if (quitRequested)
            {
                quitRequested = false;
                leaveBoard([&] { quitConfirmed = true; });
            }

            // --- Leaving with changes not written ---------------------------------------------

            if (pendingLeave)
            {
                // Saving first: once the writes are done, leave -- unless one failed and left
                // something unsaved, which is then asked about again. Discarding: once nothing
                // works on the board -- the matrix view's live test may have a reading in flight.
                if ((savingBeforeLeave || discardingBeforeLeave) && !isBoardBusy())
                {
                    const bool discarding = discardingBeforeLeave;
                    savingBeforeLeave     = false;
                    discardingBeforeLeave = false;
                    if (discarding || !hasUnsaved())
                    {
                        std::function<void()> leave = std::move(pendingLeave);
                        pendingLeave                = nullptr;
                        leave();
                    }
                }
            }
            if (pendingLeave)
            {
                ImGui::BeginChild("leave", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

                std::string what;
                for (const auto& section : boardState.sections)
                    if (section->HasUnsavedChanges())
                        what += (what.empty() ? "" : "; ") + std::string(section->Name()) + ": " + section->UnsavedSummary();
                ImGui::TextWrapped("Changes not written to the board yet -- %s. Write them before leaving?", what.c_str());

                ImGui::BeginDisabled(savingBeforeLeave || discardingBeforeLeave);
                if (ImGui::Button("Save"))
                {
                    savingBeforeLeave = true;
                    for (const auto& section : boardState.sections)
                        if (section->HasUnsavedChanges())
                            section->SaveChanges();
                }
                ImGui::SameLine();
                const bool discard = ImGui::Button("Discard");
                ImGui::SameLine();
                const bool cancel = ImGui::Button("Cancel");
                ImGui::EndDisabled();
                if (savingBeforeLeave)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Writing...");
                }
                ImGui::EndChild();

                if (discard)
                {
                    for (const auto& section : boardState.sections)
                        section->DiscardChanges();
                    discardingBeforeLeave = true;   // left above, once the board is idle
                }
                else if (cancel)
                {
                    pendingLeave = nullptr;
                }
            }

            // --- A question about an import -----------------------------------------------

            // A file imported for a board the library already has: Replace or Keep both, one
            // file at a time, the next asked on the next frame. Under the header, whatever the
            // screen, wrapping to the window's width -- not a window of its own.
            if (library.library && !library.replacements.empty())
            {
                ImGui::BeginChild("replace", ImVec2(0.0f, 0.0f),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

                const nazg::PendingReplacement& asked = library.replacements.front();
                const nazg::LibraryEntry*       entry = library.library->Find(asked.entryId);

                ImGui::TextWrapped("%s is for a board you already have a user definition for:",
                                   nazg::FileName(asked.path).c_str());
                if (entry != nullptr)
                    ImGui::BulletText("%04X:%04X  %s, imported from %s", entry->vendorId, entry->productId,
                                      entry->name.c_str(), entry->origin.c_str());

                ImGui::TextWrapped("Replace puts the new file in its place and keeps the old one as its previous "
                                   "version; boards drawn with it switch to the new file. Keep both adds it as "
                                   "another definition, and Nazg asks which one draws the board.");

                const bool replace  = ImGui::Button("Replace");
                ImGui::SameLine();
                const bool keepBoth = ImGui::Button("Keep both");
                ImGui::SameLine();
                const bool cancel   = ImGui::Button("Don't import");

                ImGui::EndChild();

                if (replace || keepBoth || cancel)
                {
                    const nazg::PendingReplacement pending = std::move(library.replacements.front());
                    library.replacements.erase(library.replacements.begin());
                    library.messages.clear();

                    if (replace)
                    {
                        if (nazg::ReplaceDefinition(library, pending.entryId, pending.bytes, pending.path))
                            afterLibraryChange(pending.entryId);
                    }
                    else if (keepBoth)
                    {
                        if (const nazg::LibraryEntry* added = nazg::ImportKeepingBoth(library, pending, NowUtc()))
                        {
                            noteForeignImport(pending.path, *added);
                            afterLibraryChange();
                        }
                    }
                }
            }

            // --- Settings -----------------------------------------------------------------

            if (showSettings)
            {
                nazg::SettingsView view;
                view.library         = library.library ? &*library.library : nullptr;
                view.libraryError    = library.error;
                view.libraryFolder   = library.library ? nazg::Utf8FromPath(library.library->Folder()) : std::string();
                view.libraryMessages = &library.messages;
                view.backLabel       = hasBoard ? "Back to the board" : "Back to the keyboards";

                view.official.found = viaBundle.definitions.has_value();
                view.official.path  = viaBundle.path;
                if (viaBundle.definitions && viaBundle.definitions->Manifest())
                {
                    const nazg::via::BundleManifest& manifest = *viaBundle.definitions->Manifest();
                    view.official.count  = manifest.v2 + manifest.v3;
                    view.official.commit = manifest.commit;
                }

                view.community.found = viaBundle.community.has_value();
                view.community.path  = viaBundle.communityPath;
                if (viaBundle.community)
                {
                    view.community.count = static_cast<int>(viaBundle.community->Definitions().size());
                    if (const auto& manifest = viaBundle.community->Manifest())
                        view.community.release = manifest->release;
                }

                char line[128];
                std::snprintf(line, sizeof(line), "Nazg %s", NAZG_VERSION);
                view.about.emplace_back(line);
                std::snprintf(line, sizeof(line), "SDL %d.%d.%d, Dear ImGui %s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                              SDL_MICRO_VERSION, IMGUI_VERSION);
                view.about.emplace_back(line);
                std::snprintf(line, sizeof(line), "GPU backend: %s, display scale %.2f",
                              pGpuDriver != nullptr ? pGpuDriver : "unknown", mainScale);
                view.about.emplace_back(line);
                std::snprintf(line, sizeof(line), "Frame: %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                view.about.emplace_back(line);

                nazg::BoardStyle           chosen     = look;
                const std::string          hostBefore = settings.legends.hostLayout;
                const nazg::SettingsAction action =
                    nazg::DrawSettings(view, settings.legends, chosen, settings.moveToNextKey, settings.advancedTools);

                if (action.appearanceChanged)
                {
                    settings.theme        = std::string(nazg::IdOf(chosen.theme));
                    settings.keycapStyle  = std::string(nazg::IdOf(chosen.keycaps));
                    settings.legendFamily = std::string(nazg::IdOf(chosen.legends));
                }
                if (action.back)
                    showSettings = false;
                if (settings.legends.hostLayout != hostBefore)
                    settings.hostLayoutChosen = true;
                if (action.appearanceChanged || action.legendsChanged || action.advancedToolsChanged || action.keymapChanged)
                    ImGui::MarkIniSettingsDirty();
                if (action.import && library.library)
                    showImportDialog();

                if (library.library)
                {
                    if (action.reimport && nazg::ReimportDefinition(library, *action.reimport))
                        afterLibraryChange(*action.reimport);
                    else if (action.restore && nazg::RestorePreviousDefinition(library, *action.restore))
                        afterLibraryChange(*action.restore);
                    else if (action.remove && nazg::RemoveDefinition(library, *action.remove))
                        afterLibraryChange();
                }
            }

            // --- The open board -----------------------------------------------------------

            else if (hasBoard)
            {
                if (boardState.isLoading)
                    ImGui::Text("Loading %s...", header.name.c_str());
                if (!boardState.error.empty())
                    nazg::ColouredText(nazg::PanelColour::Error, "%s", boardState.error.c_str());

                if (boardState.isChoosing && !boardState.isLoading)
                {
                    ImGui::BeginDisabled(isBusy);
                    const nazg::DefinitionPickerAction action =
                        nazg::DrawDefinitionPicker(boardState.candidates, boardState.inUse, boardState.identity,
                                                   boardState.keyboard.has_value(), boardState.foreignImports);
                    ImGui::EndDisabled();

                    if (action.import && library.library)
                        showImportDialog();

                    if (action.cancel)
                    {
                        boardState.isChoosing = false;
                        boardState.foreignImports.clear();
                    }

                    if (action.picked && !isBusy)
                    {
                        const nazg::DefinitionRef picked = boardState.candidates[*action.picked].ref;

                        // Remembered for this device -- exactly it, release and serial included.
                        // Without a library the board still loads; the choice just is not kept.
                        boardState.error.clear();
                        if (library.library)
                        {
                            try
                            {
                                library.library->Choose(boardState.identity, picked);
                            }
                            catch (const std::exception& failure)
                            {
                                boardState.error = std::string("the choice could not be saved: ") + failure.what();
                            }
                        }

                        boardState.isChoosing = false;
                        boardState.foreignImports.clear();
                        if (!boardState.keyboard || boardState.inUse != picked)
                            startLoad(boardState.path, boardState.identity, picked);
                    }
                }
                else if (boardState.unlock)
                {
                    boardState.unlock->Draw();

                    if (boardState.unlock->IsDone())
                    {
                        if (boardState.unlock->IsUnlocked())
                            boardState.lock->unlocked = true;
                        else
                            boardState.error = boardState.unlock->Failure();
                        boardState.unlock.reset();
                    }
                }
                else if (boardState.keyboard && !boardState.isLoading)
                {
                    // The sections of a board just loaded: every one it should have, from what the
                    // load read (ui/NazgSectionPlan.h), each made by ui/NazgSectionFactory.h.
                    if (boardState.sections.empty())
                    {
                        const nazg::SectionContext context{ transport,
                                                            boardState.path,
                                                            *boardState.keyboard,
                                                            settings.legends,
                                                            boardState.lock,
                                                            settings.advancedTools,
                                                            settings.moveToNextKey,
                                                            settings.hostLayoutChosen };
                        for (nazg::PlannedSection& planned : nazg::PlanSections(*boardState.keyboard))
                            boardState.sections.push_back(nazg::MakeSection(std::move(planned), context));
                    }

                    if (!boardState.exportMessage.empty())
                        nazg::ColouredText(nazg::PanelColour::Muted, "%s", boardState.exportMessage.c_str());

                    // The matrix view joins the Tools group, last, while Advanced tools is on (Rico,
                    // 2026-10-09) -- and leaves it, once idle, when the setting is turned off.
                    if (settings.advancedTools && boardState.matrix == nullptr)
                    {
                        auto view = std::make_unique<nazg::MatrixView>(transport, boardState.path, !boardState.isVia,
                                                                       *boardState.keyboard, settings.legends);
                        boardState.matrix = view.get();
                        boardState.sections.push_back(std::move(view));
                    }
                    else if (!settings.advancedTools && boardState.matrix != nullptr && !boardState.matrix->IsBusy())
                    {
                        boardState.sections.pop_back();
                        boardState.matrix = nullptr;
                    }

                    nazg::DrawSections(boardState.sections, boardState.activeSection, *boardState.keyboard,
                                       boardState.keyboard->report.isVial ? "Vial" : "VIA", settings.workspace);

                    isBoardShown = true;

                    // A splitter or the column's edge dragged: kept for the next run.
                    if (settings.workspace.changed)
                    {
                        settings.workspace.changed = false;
                        ImGui::MarkIniSettingsDirty();
                    }
                }
            }

            // --- The keyboard list --------------------------------------------------------

            else
            {
                if (!deviceListState.error.empty())
                    nazg::ColouredText(nazg::PanelColour::Error, "%s", deviceListState.error.c_str());
                if (!boardState.error.empty())
                    nazg::ColouredText(nazg::PanelColour::Error, "%s", boardState.error.c_str());

                const nazg::KeyboardListAction action =
                    nazg::DrawKeyboardList(deviceListState.devices, deviceListState.protocols, showAllHidDevices,
                                           deviceListState.isLoading, refreshTask.IsDone(),
                                           !isBusy && refreshTask.IsDone());

                if (action.refresh)
                    startRefresh();

                if (action.open && !isBusy)
                {
                    const nazg::HidDeviceInfo& device = deviceListState.devices[*action.open];
                    startLoad(device.path, IdentityOf(device), std::nullopt);
                }
            }

            ImGui::End();
        }

        // The window stops shrinking where the board would. ImGui's coordinates are the window's;
        // a minimized window has none, and keeps the minimum it had.
        if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
        {
            int minWidth  = 0;
            int minHeight = 0;
            if (isBoardShown)
            {
                minWidth  = static_cast<int>(std::ceil(io.DisplaySize.x - settings.workspace.spareWidth));
                minHeight = static_cast<int>(std::ceil(io.DisplaySize.y - settings.workspace.spareHeight));
            }
            if (minWidth != appliedMinWidth || minHeight != appliedMinHeight)
            {
                SDL_SetWindowMinimumSize(pWindow, minWidth, minHeight);
                appliedMinWidth  = minWidth;
                appliedMinHeight = minHeight;
            }
        }

        ImGui::Render();
        ImDrawData* pDrawData = ImGui::GetDrawData();
        const bool isMinimized = (pDrawData->DisplaySize.x <= 0.0f || pDrawData->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* pCommandBuffer = SDL_AcquireGPUCommandBuffer(pGpuDevice);

        SDL_GPUTexture* pSwapchainTexture = nullptr;
        if (isLive)
            SDL_AcquireGPUSwapchainTexture(pCommandBuffer, pWindow, &pSwapchainTexture, nullptr, nullptr);
        else
            SDL_WaitAndAcquireGPUSwapchainTexture(pCommandBuffer, pWindow, &pSwapchainTexture, nullptr, nullptr);

        if (pSwapchainTexture != nullptr && !isMinimized)
        {
            // MANDATORY for this backend, and unlike every other ImGui renderer backend:
            // the vertex and index buffers are uploaded here, before the render pass.
            ImGui_ImplSDLGPU3_PrepareDrawData(pDrawData, pCommandBuffer);

            SDL_GPUColorTargetInfo targetInfo = {};
            targetInfo.texture     = pSwapchainTexture;
            targetInfo.clear_color = SDL_FColor{ clearColor.x, clearColor.y, clearColor.z, clearColor.w };
            targetInfo.load_op     = SDL_GPU_LOADOP_CLEAR;
            targetInfo.store_op    = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* pRenderPass = SDL_BeginGPURenderPass(pCommandBuffer, &targetInfo, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(pDrawData, pCommandBuffer, pRenderPass);
            SDL_EndGPURenderPass(pRenderPass);
        }

        SDL_SubmitGPUCommandBuffer(pCommandBuffer);
        isDrawing = false;
    };

    SDL_AddEventWatch(DrawDuringLiveResize, &drawFrame);

    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            // Asked, not done: macros not written yet are asked about first.
            if (event.type == SDL_EVENT_QUIT)
                quitRequested = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(pWindow))
                quitRequested = true;
        }
        TrackWindowPlace(pWindow, settings.window);

        drawFrame(false);
        done = quitConfirmed;
    }

    SDL_RemoveEventWatch(DrawDuringLiveResize, &drawFrame);

    // Shut the transport down explicitly, while deviceListState, boardState and the tasks
    // are still alive. Destructors run in reverse declaration order, so leaving this to
    // ~HidTransport() would resume coroutines whose captured state had already died.
    transport.Shutdown();

    SDL_WaitForGPUIdle(pGpuDevice);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(pGpuDevice, pWindow);
    SDL_DestroyGPUDevice(pGpuDevice);
    SDL_DestroyWindow(pWindow);
    SDL_Quit();

    return 0;
}
