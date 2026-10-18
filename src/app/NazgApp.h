// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// App - Nazg itself: the keyboard list, the open board and Settings, the questions between
// them, and everything they work on -- the transport, the definition library, the bundles, the
// settings, the fonts and the look. No SDL and no renderer: Main.cpp, the platform shell,
// creates the window and the ImGui context, hands over what only the platform knows
// (PlatformServices), and calls BeforeFrame() and Frame() around ImGui's frame. Another
// backend -- Emscripten, OpenGL -- is another shell around the same App.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "adapters/via/NazgCommunityBundle.h"
#include "adapters/via/NazgViaBundle.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "library/NazgDefinitionChoice.h"
#include "library/NazgLibraryActions.h"
#include "model/NazgKeyHealth.h"
#include "model/NazgKeyboard.h"
#include "settings/NazgSettings.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgMatrixView.h"
#include "ui/NazgSavedLayoutReview.h"
#include "ui/NazgSection.h"
#include "ui/NazgTheme.h"
#include "ui/NazgVialUnlock.h"

namespace nazg
{
    // What only the platform knows or does, handed to App by the shell.
    struct PlatformServices
    {
        // The executable's folder, ending with a separator: the bundles and fonts/ are there.
        std::string executableFolder;

        // The per-user data folder -- the settings and the user definitions -- or why there is none.
        std::optional<std::filesystem::path> dataFolder;
        std::string                          dataFolderError;

        // The display's content scale, which the interface's sizes follow.
        float displayScale = 1.0f;

        // The OS's modifier names, for a first launch (ui-design.md, "Modifier names follow a
        // setting"): a keyboard is configured on the computer it is used with.
        ModifierNames modifierNames = ModifierNames::Windows;

        // Settings' About lines on what Nazg runs on: the libraries, the GPU backend.
        std::vector<std::string> about;

        // Now, as the library records it: ISO 8601, UTC.
        std::function<std::string()> nowUtc;

        // A problem worth a line in the log.
        std::function<void(const std::string&)> logError;

        // The file dialogs, for VIA definitions. What the user picks reaches App on the frame
        // loop's thread, in OnImportChosen() and OnExportChosen().
        std::function<void()>                             showImportDialog;
        std::function<void(const std::string& suggested)> showExportDialog;
        // Likewise for saved layouts (.vil): OnLayoutOpenChosen() and OnLayoutSaveChosen().
        std::function<void()>                             showLayoutOpenDialog;
        std::function<void(const std::string& suggested)> showLayoutSaveDialog;
    };

    class App
    {
    public:
        // The window's minimum size, in ImGui's coordinates -- the window's: the open board's, so
        // resizing never shrinks it (ui/NazgWorkspace.h, WorkspaceLayout); 0 by 0 elsewhere.
        struct MinimumSize
        {
            int width  = 0;
            int height = 0;

            bool operator==(const MinimumSize&) const = default;
        };

        // Within an ImGui context, before its first frame: the fonts are loaded here. Reads
        // settings.json (settings/NazgSettings.h).
        explicit App(PlatformServices platform);

        // Shuts the transport down first, so no coroutine resumes on state already gone, then
        // writes settings.json if anything changed since it last was.
        ~App();

        // Coroutines refer to it: it never moves.
        App(const App&)            = delete;
        App& operator=(const App&) = delete;

        // What settings.json keeps; the shell keeps `window` in step with the real window.
        [[nodiscard]] AppSettings& Settings() noexcept { return m_Settings; }

        // Before ImGui's frame, and while the window is minimized too: finished board work
        // resumed, definitions an earlier build remembered imported, keyboard navigation set.
        void BeforeFrame();

        // The whole window, between ImGui's NewFrame() and Render(). The window's minimum size,
        // or none to keep the last one -- while the window has no size, minimized.
        [[nodiscard]] std::optional<MinimumSize> Frame();

        // The file dialogs' answers: the files to import; where to export, none when cancelled.
        void OnImportChosen(std::vector<std::string> paths);
        void OnExportChosen(std::optional<std::string> path);
        // A saved layout to load, and where to save one; none when cancelled.
        void OnLayoutOpenChosen(std::optional<std::string> path);
        void OnLayoutSaveChosen(std::optional<std::string> path);

        // A HID device came or went, as the shell heard it: the list is read again once they stop
        // changing (ui-design.md, "Hotplug").
        void OnDevicesChanged();

        // The window asked to close: unsaved changes are asked about first.
        void               RequestQuit() noexcept { m_QuitRequested = true; }
        [[nodiscard]] bool IsQuitConfirmed() const noexcept { return m_QuitConfirmed; }

    private:
        // What the keyboard list shows.
        struct DeviceListState
        {
            std::vector<HidDeviceInfo> devices;
            std::vector<std::string>   protocols;   // parallel to devices, see DrawKeyboardList()
            std::string                error;
            bool                       isLoading = false;   // enumerating for the first time
            bool                       isProbing = false;   // asking new keyboards their protocol

            // When a device last came or went, until the list is read again.
            std::optional<double> changedAt;

            // The open board, unplugged and closed: said on the list until it is back or another
            // board opens.
            std::string unplugged;
            std::string unpluggedPath;
        };

        // The VIA definitions bundled with Nazg, beside the executable, each read and inflated
        // once at start and kept, so opening a board is a lookup.
        struct Bundles
        {
            std::string                                   path;
            std::optional<via::DefinitionBundle>          definitions;   // empty when missing or corrupt
            std::string                                   communityPath;
            std::optional<via::CommunityDefinitionBundle> community;     // likewise
        };

        // The board being shown.
        struct BoardState
        {
            std::optional<Keyboard> keyboard;
            std::string             path;               // where it was opened, for writes
            std::string             definitionSource;   // where its definition came from, shown with it
            std::string             error;
            bool                    isLoading = false;

            // A VIA board's definition is chosen among candidates; a Vial board carries its own.
            // Set for the board shown, or the one the picker is asking about.
            bool                             isVia = false;
            DeviceIdentity                   identity;
            std::vector<DefinitionCandidate> candidates;   // ranked, as the picker shows them
            std::vector<DefinitionCandidate> bundled;      // VIA's and the community's, kept to rebuild the list
            std::optional<DefinitionRef>     inUse;        // the one drawing the board

            // A Vial board's lock, as it was when the board loaded -- Nazg cannot unlock yet, so
            // only another app or a restart changes it meanwhile. Unset on a VIA board.
            std::optional<vial::UnlockStatus> lock;
            bool                              isChoosing = false;   // the picker is showing
            std::vector<std::string>          foreignImports;       // the picker's notes: files just imported for another keyboard

            // The definition drawing the board as its source has it, when that is VIA's bundle or
            // the board itself -- for "Export definition...". Empty for a user definition, whose
            // stored copy is read from the library when exported.
            std::vector<uint8_t> exportable;
            std::string          exportMessage;
            // What Save to file... did, in the header (ui-design.md, "Saved layouts").
            std::string layoutNote;
            bool        isLayoutNoteWarning = false;

            // What the board screen shows, Keymap first (ui/NazgSection.h), and the one chosen.
            // Made for the keyboard above once it has loaded, and dropped when a load starts.
            std::vector<std::unique_ptr<Section>> sections;
            size_t                                activeSection = 0;

            // The matrix view among the sections, last, while Advanced tools is on -- or null. Its
            // live test keeps the board open: anything else that talks to the board stops the test
            // first.
            MatrixView* matrix = nullptr;

            // An unlock under way, while the board screen shows it instead of the sections.
            // Behind a pointer: it holds a Task that refers to it, so it must never move.
            std::unique_ptr<VialUnlock> unlock;
            // A saved layout being loaded: what it changes, shown in the sections' place until
            // written or cancelled (ui-design.md, "Saved layouts"). Behind a pointer, as the unlock.
            std::unique_ptr<SavedLayoutReview> review;

            // A section still working, or an unlock, refers to the keyboard: nothing reloads it
            // meanwhile.
            [[nodiscard]] bool IsWorking() const;
        };

        // The board, through the transport.
        Task<void> RefreshDeviceList();
        Task<void> LoadBoard(std::string path, DeviceIdentity identity, std::vector<DefinitionCandidate> candidates,
                             std::optional<DefinitionRef> chosen);
        Task<void> LockBoard(std::string path);
        Task<void> SaveLayout(std::string path);

        void StartRefresh();
        void StartLoad(const std::string& path, const DeviceIdentity& identity, std::optional<DefinitionRef> chosen);
        [[nodiscard]] bool IsLoadRunning() const;
        [[nodiscard]] bool IsBoardBusy() const;

        // Busy with what holds the header back: a load, a lock, an unlock, a write -- not a
        // section's polling (Section::IsPolling()), which WhenBoardIdle() stops first.
        [[nodiscard]] bool IsBoardHeld() const;

        // `act` once nothing works on the board: at once, or after every section was told to stop
        // (Section::WhileHidden()) and its last reading is back. What replaces or reloads the board
        // goes through here -- a running Task must never be destroyed, and one handle at a time.
        void WhenBoardIdle(std::function<void()> act);

        // The open board unplugged: closed once no reply from it is awaited.
        [[nodiscard]] bool IsAwaitingBoard() const;
        void               CloseUnpluggedBoard();

        // A section sent the board away (Section::BoardLeft()): closed as if unplugged, the list
        // saying where it went -- an erase restarts it too fast for hotplug to see it go.
        void CloseLeftBoard();
        void CloseBoard(std::string said);

        // Leaving the board -- for another, the list, another definition, or quitting.
        [[nodiscard]] bool HasUnsaved() const;
        void               LeaveBoard(std::function<void()> leave);

        // settings.json: written a second after the last change -- dragging the window or a splitter
        // writes once, when it stops -- or at once when `now`.
        void KeepSettings(bool now);

        // The library.
        void AfterLibraryChange(std::optional<uint32_t> replaced = std::nullopt);
        void RefreshCandidates();
        void NoteForeignImport(const std::string& path, const LibraryEntry& entry);
        [[nodiscard]] const LibraryEntry* UserEntryInUse() const;
        void StartExport(std::vector<uint8_t> bytes, const std::string& name, const std::string& suggested);
        void ExportBoardDefinition();

        // The frame's regions, top to bottom: the header -- which returns the board's name --
        // the questions, then Settings, the open board or the keyboard list.
        std::string DrawHeader(bool hasBoard, bool isBusy);
        void        DrawLeaveQuestion();
        void        DrawReplaceQuestion();
        void        DrawSettingsScreen(const BoardStyle& look, bool hasBoard);
        bool        DrawBoardScreen(const std::string& name, bool isBusy);   // true when the sections show
        void        DrawKeyboardListScreen(bool isBusy);

        // Declared in the order they must be made in; destroyed the other way round, the tasks
        // and the board -- which refer to the transport -- before it.
        PlatformServices m_Platform;
        SettingsFile     m_SettingsFile;
        AppSettings      m_Settings;
        HidTransport     m_Transport;   // owns a worker thread; every hidapi call happens there
        UserLibrary      m_Library;
        Bundles          m_Bundles;
        DeviceListState  m_DeviceList;
        BoardState       m_Board;
        Task<void>       m_RefreshTask;
        Task<void>       m_LoadTask;
        Task<void>       m_LockTask;
        Task<void>       m_LayoutTask;   // Save to file...: reading the board, then writing the file

        // Leaving the board while a section holds changes not written yet: the user is asked,
        // Save, Discard or Cancel, and the leaving waits in `m_PendingLeave` (ui-design.md, "The
        // Macros section"). Saving first runs it once every write is done and nothing is left
        // unsaved; discarding, once no section is working -- leaving drops the sections, and a
        // running Task must never be destroyed.
        std::function<void()> m_PendingLeave;
        bool                  m_SavingBeforeLeave     = false;
        bool                  m_DiscardingBeforeLeave = false;
        std::function<void()> m_WhenIdle;   // WhenBoardIdle()'s, waiting for the sections to stop
        bool                  m_QuitRequested         = false;   // the window's close, heard by the shell
        bool                  m_QuitConfirmed         = false;   // and allowed
        bool                  m_QuitWaiting           = false;   // asked, waiting for the board to be left

        // "Export definition...": the bytes are taken when Export is clicked -- a user
        // definition's stored file, the official one out of the bundle, or what a Vial board
        // served -- so nothing has to be looked up again once a place is chosen. One dialog at a
        // time; another Export waits for it to close.
        bool                 m_IsExportOpen = false;
        std::vector<uint8_t> m_ExportBytes;
        std::string          m_ExportName;   // the definition's, for the message

        // A saved layout's dialog is open: one at a time. And Apply waits for the unlock it asked for.
        bool m_IsLayoutDialogOpen = false;
        bool m_ApplyAfterUnlock   = false;

        bool                   m_ShowAllHidDevices = false;   // the keyboard list shows only keyboards unless asked
        bool                   m_ShowSettings      = false;   // Settings in place of the main area
        SafeMargins            m_SafeMargins;                 // the diagnostics', changed by an expert: never saved
        bool                   m_OpenLoneBoard     = true;    // until the first list is in
        bool                   m_BoardUnplugged    = false;   // the open board's path is no longer listed
        std::optional<ThemeId> m_AppliedTheme;                // set when the theme changes

        // The settings as settings.json has them, as last seen, and when they last changed.
        nlohmann::ordered_json m_SavedSettings;
        nlohmann::ordered_json m_SeenSettings;
        std::optional<double>  m_SettingsChangedAt;
        std::string            m_SettingsProblem;   // the last one logged, said once
    };
}
