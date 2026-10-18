// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <utility>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "app/NazgFonts.h"
#include "board/NazgBoardLoad.h"
#include "board/NazgDeviceList.h"
#include "board/NazgSavedLayoutTransfer.h"
#include "files/NazgFiles.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgDefinitionPicker.h"
#include "ui/NazgKeyboardList.h"
#include "ui/NazgSectionFactory.h"
#include "ui/NazgSectionPlan.h"
#include "ui/NazgSettingsScreen.h"
#include "ui/NazgWorkspace.h"

namespace nazg
{
    namespace
    {
        // The bundles' files, beside the executable: VIA's official definitions, 0.3 MB, ~40 ms
        // on a fast desktop, 28 MB kept (adapters/via/NazgViaBundle.h), built by
        // tools/update_via_bundle.py; the community's, vendors' files for boards VIA lacks or has
        // older, 0.1 MB, 9 MB kept (adapters/via/NazgCommunityBundle.h), fetched by
        // tools/update_community_bundle.py. Copied there by the build, shipped with releases; a
        // checkout that never ran the tools has none, and VIA boards then need a user definition.
        constexpr char c_ViaBundleName[]       = "via_definitions.tar.xz";
        constexpr char c_CommunityBundleName[] = "community_definitions.tar.xz";

        // How long settings.json waits after the last change.
        constexpr double c_SettingsDelay = 1.0;

        // How long the device list waits after a device came or went: one board brings several
        // notifications, and its HID interfaces appear one by one.
        constexpr double c_DevicesSettleDelay = 0.5;

        // How long the header says a saved layout was saved.
        constexpr double c_SavedNoteSeconds = 4.0;

        // Where an earlier build's imgui.ini may be: the data folder, then where Nazg was started,
        // which is where builds before the data folder kept it.
        std::vector<std::filesystem::path> LegacyIniFiles(const std::optional<std::filesystem::path>& dataFolder)
        {
            std::vector<std::filesystem::path> files;
            if (dataFolder)
                files.push_back(*dataFolder / "imgui.ini");
            files.emplace_back("imgui.ini");
            return files;
        }

        // The look the settings ask for -- the defaults for ids this build does not know.
        BoardStyle StyleOf(const AppSettings& settings)
        {
            BoardStyle style;
            style.theme   = ThemeFromId(settings.theme).value_or(style.theme);
            style.keycaps = KeycapStyleFromId(settings.keycapStyle).value_or(style.keycaps);
            style.legends = LegendFamilyFromId(settings.legendFamily).value_or(style.legends);
            return style;
        }

        DeviceIdentity IdentityOf(const HidDeviceInfo& device)
        {
            return { device.vendorId, device.productId, device.manufacturer, device.product, device.releaseNumber,
                     device.serialNumber };
        }

        std::string DescribeInUse(const DefinitionCandidate& candidate)
        {
            return (candidate.definition.name.empty() ? "(unnamed)" : candidate.definition.name) + " -- " +
                   DescribeDefinitionSource(candidate);
        }
    }

    bool App::BoardState::IsWorking() const
    {
        return unlock != nullptr || (review && review->IsBusy()) ||
               std::any_of(sections.begin(), sections.end(), [](const auto& section) { return section->IsBusy(); });
    }

    App::App(PlatformServices platform)
        : m_Platform(std::move(platform)),
          m_SettingsFile(m_Platform.dataFolder.value_or(std::filesystem::path()), LegacyIniFiles(m_Platform.dataFolder)),
          m_Library(OpenUserLibrary(m_Platform.dataFolder, m_Platform.dataFolderError))
    {
        // The settings; the modifier names start from the computer's OS.
        AppSettings defaults;
        defaults.legends.modifierNames = m_Platform.modifierNames;
        m_Settings = m_SettingsFile.Load(defaults);
        if (!m_SettingsFile.Problem().empty())
        {
            m_SettingsProblem = m_SettingsFile.Problem();
            m_Platform.logError(m_SettingsProblem);
        }
        m_SavedSettings = ToJson(m_Settings);
        m_SeenSettings  = m_SavedSettings;

        LoadFonts(ImGui::GetIO(), m_Platform.executableFolder + "fonts/", m_Platform.logError);
        ImGui::StyleColorsDark();
        ApplyWindowSizes(m_Platform.displayScale);

        // A bundle that does not inflate is as good as none.
        m_Bundles.path          = m_Platform.executableFolder + c_ViaBundleName;
        m_Bundles.communityPath = m_Platform.executableFolder + c_CommunityBundleName;
        if (const auto bytes = ReadWholeFile(PathFromUtf8(m_Bundles.path)))
            try
            {
                m_Bundles.definitions.emplace(*bytes);
            }
            catch (const std::exception& failure)
            {
                m_Platform.logError(m_Bundles.path + ": " + failure.what());
            }
        if (const auto bytes = ReadWholeFile(PathFromUtf8(m_Bundles.communityPath)))
            try
            {
                m_Bundles.community.emplace(*bytes);
            }
            catch (const std::exception& failure)
            {
                m_Platform.logError(m_Bundles.communityPath + ": " + failure.what());
            }

        m_RefreshTask = RefreshDeviceList();
    }

    App::~App()
    {
        // Explicitly, while the state the coroutines refer to is still alive: members are
        // destroyed in reverse order, so leaving this to ~HidTransport() would resume coroutines
        // whose state had already died.
        m_Transport.Shutdown();

        KeepSettings(true);
    }

    void App::KeepSettings(bool now)
    {
        const nlohmann::ordered_json current = ToJson(m_Settings);
        if (current != m_SeenSettings)
        {
            m_SeenSettings      = current;
            m_SettingsChangedAt = ImGui::GetTime();
        }
        if (current == m_SavedSettings)
            return;
        if (!now && (!m_SettingsChangedAt || ImGui::GetTime() - *m_SettingsChangedAt < c_SettingsDelay))
            return;

        // Tried once per change: a file that cannot be written is said once, not every second.
        m_SavedSettings = current;
        m_SettingsChangedAt.reset();
        if (!m_SettingsFile.Save(m_Settings) && m_SettingsFile.Problem() != m_SettingsProblem)
        {
            m_SettingsProblem = m_SettingsFile.Problem();
            m_Platform.logError(m_SettingsProblem);
        }
    }

    // ------------------------------------------------------------------------------------------
    // The board, through the transport.

    // The payoff: an asynchronous sequence written as straight-line code. It suspends at the
    // co_await, the frame loop keeps rendering, and HidTransport::Pump() resumes it on the main
    // thread once the worker has the result.
    //
    // The list shows as soon as it is enumerated; each keyboard not listed before is then asked
    // which protocol it speaks, one Vial probe each -- harmless on a VIA board, which answers
    // 0xFF (board/NazgDeviceList.h). A second handle on a device gets a copy of every reply meant
    // for the first -- HID delivers input reports to each open handle -- so nothing opens a board
    // while it is asked, and the open board, listed before, never is. Read again whenever a
    // device comes or goes: the open board's path gone, it is closed.
    Task<void> App::RefreshDeviceList()
    {
        m_DeviceList.isLoading = m_DeviceList.devices.empty() && m_DeviceList.protocols.empty();

        std::vector<HidDeviceInfo> devices;
        std::string                error;
        try
        {
            devices = co_await m_Transport.Enumerate();
        }
        catch (const HidTransportError& failure)
        {
            error = failure.what();
            m_Platform.logError(std::string("enumeration failed: ") + failure.what());
        }

        m_DeviceList.protocols = KeptProtocols(m_DeviceList.devices, m_DeviceList.protocols, devices);
        m_DeviceList.devices   = std::move(devices);
        m_DeviceList.error     = std::move(error);
        m_DeviceList.isLoading = false;
        m_DeviceList.isProbing = true;

        const bool hasBoard = m_Board.isLoading || m_Board.keyboard.has_value() || m_Board.isChoosing;
        if (hasBoard && !IsListed(m_DeviceList.devices, m_Board.path))
            m_BoardUnplugged = true;
        if (IsListed(m_DeviceList.devices, m_DeviceList.unpluggedPath))
        {
            m_DeviceList.unplugged.clear();
            m_DeviceList.unpluggedPath.clear();
        }

        for (size_t index = 0; index < m_DeviceList.devices.size(); ++index)
        {
            const std::string path = m_DeviceList.devices[index].path;
            if (!IsViaInterface(m_DeviceList.devices[index]) || !m_DeviceList.protocols[index].empty() ||
                path == m_Board.path)
                continue;

            try
            {
                HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, path);
                vial::Protocol protocol(channel);
                m_DeviceList.protocols[index] = (co_await protocol.Detect()) ? "Vial" : "VIA";
            }
            catch (const std::exception&)
            {
                // Raw HID without VIA behind it, or a board that went: it does not answer.
                m_DeviceList.protocols[index] = "no answer";
            }
        }

        m_DeviceList.isProbing = false;
    }

    // Open, load everything (board/NazgBoardLoad.h), close. The user definitions the caller read
    // for the board's VID:PID are passed by value, so the load does not depend on the library
    // staying put. `chosen` is the remembered choice, if any; when it does not settle which
    // candidate, the picker asks.
    Task<void> App::LoadBoard(std::string path, DeviceIdentity identity, std::vector<DefinitionCandidate> candidates,
                              std::optional<DefinitionRef> chosen)
    {
        BoardState& state = m_Board;
        state.isLoading   = true;
        state.isChoosing  = false;
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
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, path);
            vial::Protocol protocol(channel);

            BoardLoad load = co_await nazg::LoadBoard(
                protocol, identity, std::move(candidates), std::move(chosen),
                { m_Bundles.definitions ? &*m_Bundles.definitions : nullptr,
                  m_Bundles.community ? &*m_Bundles.community : nullptr });
            for (const std::string& skipped : load.skipped)
                m_Platform.logError(skipped);

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
            m_Platform.logError(std::string("loading the board failed: ") + failure.what());
        }

        state.isLoading = false;
    }

    // Lock a Vial board again, and read its lock back -- what the header shows.
    Task<void> App::LockBoard(std::string path)
    {
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, path);
            vial::Protocol vial(channel);
            co_await vial.Lock();
            m_Board.lock = co_await vial.GetUnlockStatus();
        }
        catch (const std::exception& failure)
        {
            m_Board.error = std::string("the board could not be locked: ") + failure.what();
        }
    }

    // Save to file...: what the board holds, read, then written as a Vial saved layout -- said where
    // the definition's export is.
    Task<void> App::SaveLayout(std::string path)
    {
        m_Board.layoutNote          = "Saving...";
        m_Board.isLayoutNoteWarning = false;
        m_Board.layoutNoteAt        = 0.0;
        try
        {
            HidDeviceChannel    channel = co_await HidDeviceChannel::Open(m_Transport, m_Board.path);
            vial::Protocol      protocol(channel);
            const BoardContents board   = co_await ReadBoardContents(protocol, *m_Board.keyboard, m_Board.identity.vendorId,
                                                                     m_Board.identity.productId);
            const std::string   text    = vial::WriteSavedLayout(board.layout);

            std::ofstream stream(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.close();
            m_Board.layoutNote          = stream ? "Saved to " + FileName(path) : "Not saved: cannot write " + FileName(path);
            m_Board.isLayoutNoteWarning = !stream;
            m_Board.layoutNoteAt        = ImGui::GetTime();
        }
        catch (const std::exception& failure)
        {
            m_Board.layoutNote          = std::string("Not saved: ") + failure.what();
            m_Board.isLayoutNoteWarning = true;
        }
    }

    // A Task still running must not be replaced: destroying it would free a coroutine frame the
    // transport still holds a handle to.
    void App::StartRefresh()
    {
        if (!m_RefreshTask.IsValid() || m_RefreshTask.IsDone())
            m_RefreshTask = RefreshDeviceList();
    }

    // Never while a load still runs -- see the Refresh button -- nor while a section works on the
    // board a load would replace. The user definitions for the board's VID:PID are read here, a
    // few KB each, and the remembered choice looked up unless the caller has just made one.
    void App::StartLoad(const std::string& path, const DeviceIdentity& identity, std::optional<DefinitionRef> chosen)
    {
        std::vector<DefinitionCandidate> candidates;
        try
        {
            if (m_Library.library)
            {
                candidates = m_Library.library->Candidates(identity.vendorId, identity.productId);
                if (!chosen)
                    if (const DefinitionChoice* choice = m_Library.library->FindChoice(identity))
                        chosen = choice->definition;
            }
        }
        catch (const std::exception& failure)
        {
            m_Board.error = std::string("a user definition for this board is unreadable: ") + failure.what();
            return;
        }

        m_DeviceList.unplugged.clear();
        m_DeviceList.unpluggedPath.clear();
        m_LoadTask = LoadBoard(path, identity, std::move(candidates), std::move(chosen));
    }

    bool App::IsLoadRunning() const
    {
        return m_LoadTask.IsValid() && !m_LoadTask.IsDone();
    }

    bool App::IsBoardBusy() const
    {
        return IsLoadRunning() || (m_LockTask.IsValid() && !m_LockTask.IsDone()) ||
               (m_LayoutTask.IsValid() && !m_LayoutTask.IsDone()) || m_Board.IsWorking();
    }

    bool App::IsBoardHeld() const
    {
        return IsLoadRunning() || (m_LockTask.IsValid() && !m_LockTask.IsDone()) ||
               (m_LayoutTask.IsValid() && !m_LayoutTask.IsDone()) || m_Board.unlock != nullptr ||
               (m_Board.review && m_Board.review->IsBusy()) ||
               std::any_of(m_Board.sections.begin(), m_Board.sections.end(),
                           [](const auto& section) { return section->IsBusy() && !section->IsPolling(); });
    }

    void App::WhenBoardIdle(std::function<void()> act)
    {
        m_WhenIdle = std::move(act);
    }

    // Narrower than IsBoardBusy(): a request in flight, which the board's going ends with an error
    // soon enough. An unlock waiting to poll again, or a live test between two readings, awaits
    // nothing.
    bool App::IsAwaitingBoard() const
    {
        return IsLoadRunning() || (m_LockTask.IsValid() && !m_LockTask.IsDone()) ||
               (m_LayoutTask.IsValid() && !m_LayoutTask.IsDone()) ||
               (m_Board.unlock && m_Board.unlock->IsRequesting()) || (m_Board.review && m_Board.review->IsBusy()) ||
               std::any_of(m_Board.sections.begin(), m_Board.sections.end(),
                           [](const auto& section) { return section->IsBusy(); });
    }

    // The board is gone, so the list comes back saying which one went -- whatever was being asked,
    // Save or Discard included: nothing can be written to it any more.
    void App::CloseUnpluggedBoard()
    {
        if (!m_BoardUnplugged || IsAwaitingBoard())
            return;
        m_BoardUnplugged = false;

        std::string name = m_Board.keyboard ? m_Board.keyboard->Name() : m_Board.identity.product;
        if (name.empty())
            name = "The keyboard";
        CloseBoard(name + " was unplugged." + (HasUnsaved() ? " The changes not written to it are lost." : ""));
    }

    // A section sent the board away -- its bootloader, an erase -- and nothing waits on it any more.
    void App::CloseLeftBoard()
    {
        if (IsAwaitingBoard())
            return;
        for (const auto& section : m_Board.sections)
            if (std::string said = section->BoardLeft(); !said.empty())
            {
                CloseBoard(std::move(said));
                return;
            }
    }

    // The list comes back saying `said` until the board is listed again.
    void App::CloseBoard(std::string said)
    {
        m_DeviceList.unplugged     = std::move(said);
        m_DeviceList.unpluggedPath = m_Board.path;

        m_BoardUnplugged        = false;
        m_PendingLeave          = nullptr;
        m_SavingBeforeLeave     = false;
        m_DiscardingBeforeLeave = false;
        m_WhenIdle              = nullptr;
        m_Board                 = BoardState{};

        // A quit waiting for this board -- an unlock unplugged to give up -- has nothing left to wait for.
        if (m_QuitWaiting)
            m_QuitConfirmed = true;
    }

    void App::OnDevicesChanged()
    {
        m_DeviceList.changedAt = ImGui::GetTime();
    }

    bool App::HasUnsaved() const
    {
        return std::any_of(m_Board.sections.begin(), m_Board.sections.end(),
                           [](const auto& section) { return section->HasUnsavedChanges(); });
    }

    void App::LeaveBoard(std::function<void()> leave)
    {
        if (!HasUnsaved())
        {
            WhenBoardIdle(std::move(leave));
            return;
        }
        m_PendingLeave          = std::move(leave);
        m_SavingBeforeLeave     = false;
        m_DiscardingBeforeLeave = false;
    }

    // ------------------------------------------------------------------------------------------
    // The library.

    // After the library changed: the picker's list follows, and a board waiting for a definition
    // loads by itself once exactly one candidate is left -- the connect rule. A board drawn by an
    // entry given a new version (`replaced`) is loaded again with it, keymap included, since the
    // matrix may have changed with it.
    void App::AfterLibraryChange(std::optional<uint32_t> replaced)
    {
        if (IsBoardBusy())
            return;

        RefreshCandidates();

        if (replaced && m_Board.keyboard && m_Board.inUse == DefinitionRef::User(*replaced))
            StartLoad(m_Board.path, m_Board.identity, m_Board.inUse);
        else if (m_Board.isChoosing && !m_Board.keyboard && ResolveCandidate(m_Board.candidates, std::nullopt))
            StartLoad(m_Board.path, m_Board.identity, std::nullopt);
    }

    // The shown VIA board's candidates again, after the library changed: an import appears in the
    // picker at once, a removal leaves it.
    void App::RefreshCandidates()
    {
        if (!m_Board.isVia || !m_Library.library)
            return;

        try
        {
            m_Board.candidates = m_Library.library->Candidates(m_Board.identity.vendorId, m_Board.identity.productId);
        }
        catch (const std::exception& failure)
        {
            m_Board.error = std::string("a user definition for this board is unreadable: ") + failure.what();
            return;
        }

        m_Board.candidates.insert(m_Board.candidates.end(), m_Board.bundled.begin(), m_Board.bundled.end());
        RankCandidates(m_Board.candidates, m_Board.identity.product);
    }

    // An import while the picker asks about a VIA board, for another VID:PID: the picker says why
    // it is not among the candidates.
    void App::NoteForeignImport(const std::string& path, const LibraryEntry& entry)
    {
        if (m_Board.isChoosing && m_Board.isVia &&
            (entry.vendorId != m_Board.identity.vendorId || entry.productId != m_Board.identity.productId))
            m_Board.foreignImports.push_back(ForeignImportNote(path, entry, m_Board.identity));
    }

    // The library entry drawing the open board, if a user definition does.
    const LibraryEntry* App::UserEntryInUse() const
    {
        if (!m_Board.inUse || m_Board.inUse->kind != DefinitionRef::Kind::User || !m_Library.library)
            return nullptr;
        return m_Library.library->Find(m_Board.inUse->userId);
    }

    // Opens the save dialog for these bytes, unless one is open already.
    void App::StartExport(std::vector<uint8_t> bytes, const std::string& name, const std::string& suggested)
    {
        if (m_IsExportOpen)
            return;
        m_IsExportOpen = true;
        m_ExportBytes  = std::move(bytes);
        m_ExportName   = name;
        m_Platform.showExportDialog(suggested);
    }

    // For investigation and debugging: the definition drawing the board, byte for byte as Nazg
    // has it -- VIA's bundle, the board itself, or the library's stored copy, which offers the
    // name of the file it came from.
    void App::ExportBoardDefinition()
    {
        try
        {
            if (const LibraryEntry* entry = UserEntryInUse())
                StartExport(m_Library.library->Read(*entry), entry->name, FileName(entry->origin));
            else if (m_Board.keyboard && !m_Board.exportable.empty())
                StartExport(m_Board.exportable, m_Board.keyboard->Name(), ExportFileName(m_Board.keyboard->Name()));
        }
        catch (const std::exception& failure)
        {
            m_Board.exportMessage = std::string("not exported: ") + failure.what();
        }
    }

    void App::OnImportChosen(std::vector<std::string> paths)
    {
        if (paths.empty() || !m_Library.library)
            return;

        m_Board.foreignImports.clear();
        for (const auto& [path, entry] : ImportDefinitions(m_Library, paths, true, m_Platform.nowUtc()))
            NoteForeignImport(path, entry);
        AfterLibraryChange();
    }

    void App::OnExportChosen(std::optional<std::string> path)
    {
        m_IsExportOpen = false;
        if (path)
            m_Board.exportMessage = WriteExport(m_ExportBytes, m_ExportName, *path);
        m_ExportBytes.clear();
    }

    void App::OnLayoutSaveChosen(std::optional<std::string> path)
    {
        m_IsLayoutDialogOpen = false;
        if (!path || !m_Board.keyboard)
            return;
        WhenBoardIdle([this, chosen = *path]
        {
            if (m_Board.keyboard)
                m_LayoutTask = SaveLayout(chosen);
        });
    }

    // The file is read here, and refused if it is no saved layout; the review reads the board.
    void App::OnLayoutOpenChosen(std::optional<std::string> path)
    {
        m_IsLayoutDialogOpen = false;
        if (!path || !m_Board.keyboard)
            return;

        vial::SavedLayout file;
        try
        {
            const std::optional<std::vector<uint8_t>> bytes = ReadWholeFile(PathFromUtf8(*path));
            if (!bytes)
                throw std::runtime_error("cannot read " + *path);
            file = vial::ReadSavedLayout(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                         m_Board.keyboard->keycodeVersion);
        }
        catch (const std::exception& failure)
        {
            m_Board.error = std::string("Not loaded: ") + failure.what();
            return;
        }

        WhenBoardIdle([this, file, name = FileName(*path)]
        {
            if (!m_Board.keyboard)
                return;
            m_ShowSettings = false;
            m_Board.error.clear();
            m_Board.layoutNote.clear();
            m_Board.review = std::make_unique<SavedLayoutReview>(m_Transport, m_Board.path, *m_Board.keyboard,
                                                                 m_Board.identity.vendorId, m_Board.identity.productId,
                                                                 m_Settings.legends, m_Board.lock, name, file);
        });
    }

    // ------------------------------------------------------------------------------------------
    // The frame.

    void App::BeforeFrame()
    {
        // Even while minimized: transport work would stall otherwise.
        m_Transport.Pump();

        // Paths an earlier build remembered, read with the settings: imported once, then dropped
        // from them. Kept if there is no library.
        if (!m_Settings.legacyViaDefinitions.empty() && m_Library.library)
        {
            ImportDefinitions(m_Library, m_Settings.legacyViaDefinitions, false, m_Platform.nowUtc());
            m_Settings.legacyViaDefinitions.clear();
        }

        // Whatever changed them last frame -- Settings, a splitter, the window, a section.
        KeepSettings(false);

        // Devices came or went: listed again once they have stopped for a moment -- or, a listing
        // still running, after it, since it may have missed the latest.
        if (m_DeviceList.changedAt && ImGui::GetTime() - *m_DeviceList.changedAt >= c_DevicesSettleDelay &&
            (!m_RefreshTask.IsValid() || m_RefreshTask.IsDone()))
        {
            m_DeviceList.changedAt.reset();
            StartRefresh();
        }
        CloseUnpluggedBoard();
        CloseLeftBoard();

        // The matrix live test asks for every key of the board to be pressed, and they all type
        // into Nazg: with keyboard navigation on, an arrow then Space would click whatever has
        // focus, and Alt would open the menu bar. Off while the test runs.
        ImGuiIO& io = ImGui::GetIO();
        if (m_Board.matrix && m_Board.matrix->IsLiveTestRunning())
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    std::optional<App::MinimumSize> App::Frame()
    {
        // The look, as the settings say, changed in Settings.
        const BoardStyle look = StyleOf(m_Settings);
        if (m_AppliedTheme != look.theme)
        {
            ApplyWindowTheme(look.theme);
            m_AppliedTheme = look.theme;
        }
        SetBoardStyle(look);

        // With exactly one keyboard plugged in at start, Nazg opens it: most people never see the
        // list. Only the first time -- afterwards the list is shown because it was asked for.
        if (m_OpenLoneBoard && !m_DeviceList.isLoading && !m_DeviceList.isProbing)
        {
            m_OpenLoneBoard = false;

            const auto keyboards = std::count_if(m_DeviceList.devices.begin(), m_DeviceList.devices.end(), IsViaInterface);
            if (keyboards == 1 && !IsBoardBusy())
            {
                const auto lone = std::find_if(m_DeviceList.devices.begin(), m_DeviceList.devices.end(), IsViaInterface);
                StartLoad(lone->path, IdentityOf(*lone), std::nullopt);
            }
        }

        // Waiting for the board to be idle: every section stops, and the waiting is over once the
        // last reading is back -- before the board is drawn, so nothing starts again this frame.
        if (m_WhenIdle)
        {
            for (const auto& section : m_Board.sections)
                section->WhileHidden();
            if (!IsBoardBusy())
            {
                std::function<void()> act = std::move(m_WhenIdle);
                m_WhenIdle                = nullptr;
                act();
            }
        }

        // One window filling the platform's: the header, then Settings, the open board or the
        // keyboard list under it (ui-design.md, "The regions").
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

            const bool        hasBoard = m_Board.isLoading || m_Board.keyboard.has_value() || m_Board.isChoosing;
            const bool        isBusy   = IsBoardHeld() || m_WhenIdle != nullptr;
            const std::string name     = DrawHeader(hasBoard, isBusy);

            DrawLeaveQuestion();
            DrawReplaceQuestion();

            if (m_ShowSettings)
                DrawSettingsScreen(look, hasBoard);
            else if (hasBoard)
                isBoardShown = DrawBoardScreen(name, isBusy);
            else
                DrawKeyboardListScreen(isBusy);

            ImGui::End();
        }

        // Settings, the picker or the unlock in the board's place: what runs only while a section
        // shows stops -- the diagnostics turn key output on again, the live test lets the board go.
        if (!isBoardShown)
            for (const auto& section : m_Board.sections)
                section->WhileHidden();

        // The window stops shrinking where the board would. ImGui's coordinates are the window's;
        // a minimized window has none, and keeps the minimum it had.
        const ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return std::nullopt;

        MinimumSize minimum;
        if (isBoardShown)
        {
            minimum.width  = static_cast<int>(std::ceil(io.DisplaySize.x - m_Settings.workspace.spareWidth));
            minimum.height = static_cast<int>(std::ceil(io.DisplaySize.y - m_Settings.workspace.spareHeight));
        }
        return minimum;
    }

    // --- The header -----------------------------------------------------------------------------

    std::string App::DrawHeader(bool hasBoard, bool isBusy)
    {
        HeaderView          header;
        std::vector<size_t> others;   // device index of each "Switch to" entry
        if (hasBoard)
        {
            header.name = m_Board.keyboard ? m_Board.keyboard->Name() : m_Board.identity.product;
            if (header.name.empty())
                header.name = "(unnamed)";

            if (m_Board.keyboard)
            {
                // Always at hand, so a wrong definition is plain to see -- and cheap to undo: the
                // keymap lives in the board, the definition only draws it.
                header.protocol  = m_Board.isVia ? "VIA" : "Vial";
                header.details   = "Definition: " + m_Board.definitionSource + "\nQMK keycodes " +
                                 qmk::KeycodeVersionName(m_Board.keyboard->keycodeVersion);
                header.isVia     = m_Board.isVia;
                header.hasChoice = m_Board.isVia && m_Library.library &&
                                   m_Library.library->FindChoice(m_Board.identity) != nullptr;
                header.canExport = !m_Board.exportable.empty() || UserEntryInUse() != nullptr;
                header.hasKeyboard = !m_Board.isLoading;
                // "Saved to B104.vil" goes after a few seconds (Rico, 2026-10-18); "Not saved" stays.
                if (!m_Board.isLayoutNoteWarning && m_Board.layoutNoteAt > 0.0 &&
                    ImGui::GetTime() - m_Board.layoutNoteAt > c_SavedNoteSeconds)
                    m_Board.layoutNote.clear();
                header.note          = m_Board.layoutNote;
                header.isNoteWarning = m_Board.isLayoutNoteWarning;

                header.hasAdvanced = m_Settings.advancedTools;

                // A VIAL_INSECURE build reports itself unlocked and has no combo to unlock with:
                // it has no lock, and the header says nothing.
                if (m_Board.lock && !(m_Board.lock->unlocked && m_Board.lock->unlockKeys.empty()))
                    header.isLocked = !m_Board.lock->unlocked;
            }

            for (size_t index = 0; index < m_DeviceList.devices.size(); ++index)
            {
                const HidDeviceInfo& device = m_DeviceList.devices[index];
                // Offered once it has said which protocol it speaks: never opened while it is asked.
                const bool isAsked = index < m_DeviceList.protocols.size() && !m_DeviceList.protocols[index].empty();
                if (!IsViaInterface(device) || device.path == m_Board.path || !isAsked)
                    continue;
                others.push_back(index);
                header.others.push_back({ device.product.empty() ? "(unnamed)" : device.product,
                                          m_DeviceList.protocols[index] });
            }

            header.isBusy = isBusy;
        }
        header.isSettingsShown = m_ShowSettings;

        const HeaderAction action = nazg::DrawHeader(header);

        if (action.settings)
            m_ShowSettings = !m_ShowSettings;

        if (action.switchTo && !isBusy)
        {
            const HidDeviceInfo& device   = m_DeviceList.devices[others[*action.switchTo]];
            const std::string    path     = device.path;
            const DeviceIdentity identity = IdentityOf(device);
            LeaveBoard([this, path, identity]
            {
                m_ShowSettings = false;
                StartLoad(path, identity, std::nullopt);
            });
        }

        if (action.changeDefinition && !isBusy)
            LeaveBoard([this]
            {
                m_ShowSettings     = false;
                m_Board.isChoosing = true;
            });

        if (action.forgetChoice && m_Library.library)
        {
            try
            {
                m_Library.library->Forget(m_Board.identity);
            }
            catch (const std::exception& failure)
            {
                m_Board.error = std::string("the choice could not be forgotten: ") + failure.what();
            }
        }

        // Locked: unlock, on its own screen. Unlocked: lock again at once. Either talks to the
        // board, so the sections let it go first.
        if (action.toggleLock && !isBusy && m_Board.keyboard && m_Board.lock)
            WhenBoardIdle([this]
            {
                if (!m_Board.keyboard || !m_Board.lock)
                    return;
                m_ShowSettings = false;
                m_Board.error.clear();
                if (!m_Board.lock->unlocked)
                    m_Board.unlock = std::make_unique<VialUnlock>(m_Transport, m_Board.path, *m_Board.keyboard,
                                                                  m_Board.lock->unlockKeys, m_Settings.legends);
                else
                    m_LockTask = LockBoard(m_Board.path);
            });

        if (action.exportDefinition)
            ExportBoardDefinition();

        // A saved layout: the board's name offered for the file, what a file system refuses left out.
        if ((action.saveLayout || action.loadLayout) && !isBusy && m_Board.keyboard && !m_IsLayoutDialogOpen)
        {
            m_IsLayoutDialogOpen = true;
            if (action.loadLayout)
                m_Platform.showLayoutOpenDialog();
            else
            {
                constexpr std::string_view c_Refused = "<>:\"/\\|?*";
                std::string                suggested;
                for (const char c : m_Board.keyboard->Name())
                    if (c_Refused.find(c) == std::string_view::npos && static_cast<unsigned char>(c) >= 0x20)
                        suggested.push_back(c);
                m_Platform.showLayoutSaveDialog((suggested.empty() ? std::string("layout") : suggested) + ".vil");
            }
        }

        // Closes the board; the list is kept current as devices come and go.
        if (action.allKeyboards && !isBusy)
            LeaveBoard([this]
            {
                m_Board        = BoardState{};
                m_ShowSettings = false;
            });

        // Quitting, once the board may be left.
        if (m_QuitRequested)
        {
            m_QuitRequested = false;
            m_QuitWaiting   = true;
            LeaveBoard([this] { m_QuitConfirmed = true; });
        }

        return header.name;
    }

    // --- Leaving with changes not written -------------------------------------------------------

    void App::DrawLeaveQuestion()
    {
        if (m_PendingLeave)
        {
            // Saving first: once the writes are done, leave -- unless one failed and left
            // something unsaved, which is then asked about again. Discarding: at once. Either way
            // once nothing works on the board -- a section polling stops first (WhenBoardIdle()).
            if ((m_SavingBeforeLeave || m_DiscardingBeforeLeave) && !IsBoardHeld())
            {
                const bool discarding   = m_DiscardingBeforeLeave;
                m_SavingBeforeLeave     = false;
                m_DiscardingBeforeLeave = false;
                if (discarding || !HasUnsaved())
                {
                    WhenBoardIdle(std::move(m_PendingLeave));
                    m_PendingLeave = nullptr;
                }
            }
        }
        if (!m_PendingLeave)
            return;

        ImGui::BeginChild("leave", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

        std::string what;
        for (const auto& section : m_Board.sections)
            if (section->HasUnsavedChanges())
                what += (what.empty() ? "" : "; ") + std::string(section->Name()) + ": " + section->UnsavedSummary();
        ImGui::TextWrapped("Changes not written to the board yet -- %s. Write them before leaving?", what.c_str());

        ImGui::BeginDisabled(m_SavingBeforeLeave || m_DiscardingBeforeLeave);
        if (ImGui::Button("Save"))
        {
            m_SavingBeforeLeave = true;
            for (const auto& section : m_Board.sections)
                if (section->HasUnsavedChanges())
                    section->SaveChanges();
        }
        ImGui::SameLine();
        const bool discard = ImGui::Button("Discard");
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");
        ImGui::EndDisabled();
        if (m_SavingBeforeLeave)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Writing...");
        }
        ImGui::EndChild();

        if (discard)
        {
            for (const auto& section : m_Board.sections)
                section->DiscardChanges();
            m_DiscardingBeforeLeave = true;   // left above, once the board is idle
        }
        else if (cancel)
        {
            m_PendingLeave = nullptr;
            m_QuitWaiting  = false;
        }
    }

    // --- A question about an import -----------------------------------------------------------

    // A file imported for a board the library already has: Replace or Keep both, one file at a
    // time, the next asked on the next frame. Under the header, whatever the screen, wrapping to
    // the window's width -- not a window of its own.
    void App::DrawReplaceQuestion()
    {
        if (!m_Library.library || m_Library.replacements.empty())
            return;

        ImGui::BeginChild("replace", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

        const PendingReplacement& asked = m_Library.replacements.front();
        const LibraryEntry*       entry = m_Library.library->Find(asked.entryId);

        ImGui::TextWrapped("%s is for a board you already have a user definition for:", FileName(asked.path).c_str());
        if (entry != nullptr)
            ImGui::BulletText("%04X:%04X  %s, imported from %s", entry->vendorId, entry->productId, entry->name.c_str(),
                              entry->origin.c_str());

        ImGui::TextWrapped("Replace puts the new file in its place and keeps the old one as its previous "
                           "version; boards drawn with it switch to the new file. Keep both adds it as "
                           "another definition, and Nazg asks which one draws the board.");

        const bool replace  = ImGui::Button("Replace");
        ImGui::SameLine();
        const bool keepBoth = ImGui::Button("Keep both");
        ImGui::SameLine();
        const bool cancel   = ImGui::Button("Don't import");

        ImGui::EndChild();

        if (!replace && !keepBoth && !cancel)
            return;

        const PendingReplacement pending = std::move(m_Library.replacements.front());
        m_Library.replacements.erase(m_Library.replacements.begin());
        m_Library.messages.clear();

        if (replace)
        {
            if (ReplaceDefinition(m_Library, pending.entryId, pending.bytes, pending.path))
                AfterLibraryChange(pending.entryId);
        }
        else if (keepBoth)
        {
            if (const LibraryEntry* added = ImportKeepingBoth(m_Library, pending, m_Platform.nowUtc()))
            {
                NoteForeignImport(pending.path, *added);
                AfterLibraryChange();
            }
        }
    }

    // --- Settings -------------------------------------------------------------------------------

    void App::DrawSettingsScreen(const BoardStyle& look, bool hasBoard)
    {
        SettingsView view;
        view.library         = m_Library.library ? &*m_Library.library : nullptr;
        view.libraryError    = m_Library.error;
        view.libraryFolder   = m_Library.library ? Utf8FromPath(m_Library.library->Folder()) : std::string();
        view.libraryMessages = &m_Library.messages;
        view.backLabel       = hasBoard ? "Back to the board" : "Back to the keyboards";

        view.official.found = m_Bundles.definitions.has_value();
        view.official.path  = m_Bundles.path;
        if (m_Bundles.definitions && m_Bundles.definitions->Manifest())
        {
            const via::BundleManifest& manifest = *m_Bundles.definitions->Manifest();
            view.official.count  = manifest.v2 + manifest.v3;
            view.official.commit = manifest.commit;
        }

        view.community.found = m_Bundles.community.has_value();
        view.community.path  = m_Bundles.communityPath;
        if (m_Bundles.community)
        {
            view.community.count = static_cast<int>(m_Bundles.community->Definitions().size());
            if (const auto& manifest = m_Bundles.community->Manifest())
                view.community.release = manifest->release;
        }

        const ImGuiIO& io = ImGui::GetIO();
        char           line[128];
        std::snprintf(line, sizeof(line), "Nazg %s", NAZG_VERSION);
        view.about.emplace_back(line);
        view.about.insert(view.about.end(), m_Platform.about.begin(), m_Platform.about.end());
        std::snprintf(line, sizeof(line), "Frame: %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        view.about.emplace_back(line);

        BoardStyle           chosen     = look;
        const std::string    hostBefore = m_Settings.legends.hostLayout;
        const SettingsAction action =
            DrawSettings(view, m_Settings.legends, chosen, m_Settings.moveToNextKey, m_Settings.advancedTools);

        if (action.appearanceChanged)
        {
            m_Settings.theme        = std::string(IdOf(chosen.theme));
            m_Settings.keycapStyle  = std::string(IdOf(chosen.keycaps));
            m_Settings.legendFamily = std::string(IdOf(chosen.legends));
        }
        if (action.back)
            m_ShowSettings = false;
        if (m_Settings.legends.hostLayout != hostBefore)
            m_Settings.hostLayoutChosen = true;
        if (action.import && m_Library.library)
            m_Platform.showImportDialog();

        if (m_Library.library)
        {
            if (action.reimport && ReimportDefinition(m_Library, *action.reimport))
                AfterLibraryChange(*action.reimport);
            else if (action.restore && RestorePreviousDefinition(m_Library, *action.restore))
                AfterLibraryChange(*action.restore);
            else if (action.remove && RemoveDefinition(m_Library, *action.remove))
                AfterLibraryChange();
        }
    }

    // --- The open board -------------------------------------------------------------------------

    bool App::DrawBoardScreen(const std::string& name, bool isBusy)
    {
        if (m_Board.isLoading)
            ImGui::Text("Loading %s...", name.c_str());
        if (!m_Board.error.empty())
            ColouredText(PanelColour::Error, "%s", m_Board.error.c_str());

        if (m_Board.isChoosing && !m_Board.isLoading)
        {
            ImGui::BeginDisabled(isBusy);
            const DefinitionPickerAction action = DrawDefinitionPicker(m_Board.candidates, m_Board.inUse, m_Board.identity,
                                                                       m_Board.keyboard.has_value(), m_Board.foreignImports);
            ImGui::EndDisabled();

            if (action.import && m_Library.library)
                m_Platform.showImportDialog();

            if (action.cancel)
            {
                m_Board.isChoosing = false;
                m_Board.foreignImports.clear();
            }

            if (action.picked && !isBusy)
            {
                const DefinitionRef picked = m_Board.candidates[*action.picked].ref;

                // Remembered for this device -- exactly it, release and serial included. Without a
                // library the board still loads; the choice just is not kept.
                m_Board.error.clear();
                if (m_Library.library)
                {
                    try
                    {
                        m_Library.library->Choose(m_Board.identity, picked);
                    }
                    catch (const std::exception& failure)
                    {
                        m_Board.error = std::string("the choice could not be saved: ") + failure.what();
                    }
                }

                m_Board.isChoosing = false;
                m_Board.foreignImports.clear();
                if (!m_Board.keyboard || m_Board.inUse != picked)
                    WhenBoardIdle([this, picked] { StartLoad(m_Board.path, m_Board.identity, picked); });
            }
            return false;
        }

        if (m_Board.unlock)
        {
            m_Board.unlock->Draw(m_QuitWaiting);

            if (m_Board.unlock->IsDone())
            {
                if (m_Board.unlock->IsUnlocked())
                {
                    m_Board.lock->unlocked = true;
                    // Unlocked for a saved layout's Apply: written now.
                    if (m_ApplyAfterUnlock && m_Board.review)
                        m_Board.review->StartWriting();
                }
                else if (m_Board.unlock->IsCancelled())
                {
                    // Left mid-unlock, it answers nothing else: closed, a saved layout's review with
                    // it, until it is unplugged. Opening it before then is refused (NazgBoardLoad).
                    m_ApplyAfterUnlock = false;
                    CloseBoard(m_Board.keyboard->Name() + " is still waiting for its unlock keys -- unplug it, and "
                                                          "it answers again.");
                    return false;
                }
                else
                    m_Board.error = m_Board.unlock->Failure();
                m_ApplyAfterUnlock = false;
                m_Board.unlock.reset();
            }
            return false;
        }

        if (!m_Board.keyboard || m_Board.isLoading)
            return false;

        // A saved layout being loaded, in the sections' place, as Settings is: no column.
        if (m_Board.review)
        {
            DrawView(*m_Board.review, m_Board.review->Preview(), m_Settings.workspace);

            const SavedLayoutReview::Action action = m_Board.review->TakeAction();
            if (action.cancel && !m_Board.review->IsBusy())
                m_Board.review.reset();
            else if (action.apply)
            {
                // A locked board refuses macros and stores Boot as nothing: unlocked first, on the
                // unlock's own screen, then written.
                if (m_Board.review->NeedsUnlock() && m_Board.lock && !m_Board.lock->unlocked)
                {
                    m_ApplyAfterUnlock = true;
                    m_Board.unlock     = std::make_unique<VialUnlock>(m_Transport, m_Board.path, *m_Board.keyboard,
                                                                      m_Board.lock->unlockKeys, m_Settings.legends);
                }
                else
                    m_Board.review->StartWriting();
            }
            else if (action.back)
                // Loaded again, so every section reads what the board now holds.
                WhenBoardIdle([this]
                {
                    m_Board.review.reset();
                    StartLoad(m_Board.path, m_Board.identity, m_Board.inUse);
                });
            return false;
        }

        // The sections of a board just loaded: every one it should have, from what the load read
        // (ui/NazgSectionPlan.h), each made by ui/NazgSectionFactory.h.
        if (m_Board.sections.empty())
        {
            const SectionContext context{ m_Transport,
                                          m_Board.path,
                                          m_Board.identity,
                                          *m_Board.keyboard,
                                          m_Settings.legends,
                                          m_Board.lock,
                                          m_Settings.advancedTools,
                                          m_SafeMargins,
                                          m_Settings.moveToNextKey,
                                          m_Settings.hostLayoutChosen };
            for (PlannedSection& planned : PlanSections(*m_Board.keyboard))
                m_Board.sections.push_back(MakeSection(std::move(planned), context));
        }

        if (!m_Board.exportMessage.empty())
            ColouredText(PanelColour::Muted, "%s", m_Board.exportMessage.c_str());

        // The matrix view joins the Tools group, last, while Advanced tools is on (Rico,
        // 2026-10-09) -- and leaves it, once idle, when the setting is turned off.
        if (m_Settings.advancedTools && m_Board.matrix == nullptr)
        {
            auto view = std::make_unique<MatrixView>(m_Transport, m_Board.path, !m_Board.isVia, *m_Board.keyboard,
                                                     m_Settings.legends);
            m_Board.matrix = view.get();
            m_Board.sections.push_back(std::move(view));
        }
        else if (!m_Settings.advancedTools && m_Board.matrix != nullptr && !m_Board.matrix->IsBusy())
        {
            m_Board.sections.pop_back();
            m_Board.matrix = nullptr;
        }

        DrawSections(m_Board.sections, m_Board.activeSection, *m_Board.keyboard,
                     m_Board.keyboard->report.isVial ? "Vial" : "VIA", m_Settings.workspace);
        return true;
    }

    // --- The keyboard list ----------------------------------------------------------------------

    void App::DrawKeyboardListScreen(bool isBusy)
    {
        if (!m_DeviceList.error.empty())
            ColouredText(PanelColour::Error, "%s", m_DeviceList.error.c_str());
        if (!m_Board.error.empty())
            ColouredText(PanelColour::Error, "%s", m_Board.error.c_str());
        if (!m_DeviceList.unplugged.empty())
            ColouredText(PanelColour::Muted, "%s", m_DeviceList.unplugged.c_str());

        const KeyboardListAction action = DrawKeyboardList(m_DeviceList.devices, m_DeviceList.protocols,
                                                           m_ShowAllHidDevices, m_DeviceList.isLoading,
                                                           !isBusy && m_RefreshTask.IsDone());

        if (action.open && !isBusy)
        {
            const HidDeviceInfo& device = m_DeviceList.devices[*action.open];
            StartLoad(device.path, IdentityOf(device), std::nullopt);
        }
    }
}
