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

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaBundle.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaLoader.h"
#include "adapters/vial/NazgVialLoader.h"
#include "async/NazgTask.h"
#include "library/NazgDefinitionLibrary.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgDefinitionPicker.h"
#include "ui/NazgKeymapSection.h"
#include "ui/NazgTheme.h"
#include "ui/NazgWorkspace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        std::string                      error;
        bool                             isLoading = false;
    };

    // The payoff: an asynchronous sequence written as straight-line code. It suspends
    // at the co_await, the frame loop keeps rendering, and HidTransport::Pump()
    // resumes it on the main thread once the worker has the result.
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

        state.isLoading = false;
    }

    // Settings that outlive a run. Kept in imgui.ini through ImGui's own settings-handler
    // hook -- one [Nazg][Settings] section -- rather than a settings file of Nazg's own,
    // which can come when there is more than one setting worth a file.
    struct AppSettings
    {
        std::string hostLayout = "us";   // a HostLayout id; see NazgKeycapLegend.h

        // Paths of VIA definition files that builds before the library remembered here.
        // Read, imported into the library once, then dropped -- written back only while
        // there is no library to import them into.
        std::vector<std::string> legacyViaDefinitions;
    };

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
            constexpr char c_ViaDefinition[] = "ViaDefinition=";

            if (std::strncmp(line, c_HostLayout, sizeof(c_HostLayout) - 1) == 0)
                loaded.hostLayout = line + sizeof(c_HostLayout) - 1;
            else if (std::strncmp(line, c_ViaDefinition, sizeof(c_ViaDefinition) - 1) == 0)
                loaded.legacyViaDefinitions.emplace_back(line + sizeof(c_ViaDefinition) - 1);
        };

        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out)
        {
            const AppSettings& saved = *static_cast<AppSettings*>(self->UserData);
            out->appendf("[%s][Settings]\nHostLayout=%s\n", self->TypeName, saved.hostLayout.c_str());
            // Only paths not yet imported -- when the library could not be opened -- so
            // none is lost before it can be.
            for (const std::string& path : saved.legacyViaDefinitions)
                out->appendf("ViaDefinition=%s\n", path.c_str());
            out->append("\n");
        };

        ImGui::AddSettingsHandler(&handler);   // copied by ImGui
    }

    // TEMPORARY, like the board view: a system font with the glyphs host-layout legends
    // need -- é, ß, Cyrillic, Greek, CJK -- since ImGui's built-in font is ASCII only.
    // Which font Nazg ships with is visual design work for later. ImGui 1.92 rasterises
    // glyphs on demand, so no glyph ranges are listed; merged fonts fill in what the first
    // one lacks. Falls back to the built-in font when none is found.
    void LoadFonts(ImGuiIO& io)
    {
        constexpr float c_FontSize = 16.0f;

#if defined(_WIN32)
        const char*       windir = std::getenv("WINDIR");
        const std::string fonts  = std::string(windir != nullptr ? windir : "C:\\Windows") + "\\Fonts\\";

        const std::vector<std::string> primary  = { fonts + "segoeui.ttf", fonts + "arial.ttf" };
        const std::vector<std::string> fallback = { fonts + "seguisym.ttf", fonts + "YuGothM.ttc", fonts + "malgun.ttf" };
#elif defined(__APPLE__)
        const std::vector<std::string> primary  = { "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                                                    "/System/Library/Fonts/Helvetica.ttc" };
        const std::vector<std::string> fallback = {};
#else
        const std::vector<std::string> primary  = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                                    "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                                    "/usr/share/fonts/noto/NotoSans-Regular.ttf" };
        const std::vector<std::string> fallback = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                                    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc" };
#endif

        bool haveBase = false;
        for (const std::string& path : primary)
        {
            if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path.c_str(), c_FontSize) != nullptr)
            {
                haveBase = true;
                break;
            }
        }

        if (!haveBase)
        {
            SDL_Log("no system font found; legends beyond ASCII will not render");
            return;
        }

        ImFontConfig merge;
        merge.MergeMode = true;
        for (const std::string& path : fallback)
            if (std::filesystem::exists(path))
                io.Fonts->AddFontFromFileTTF(path.c_str(), c_FontSize, &merge);
    }

    // A UTF-8 path, as SDL hands them over, as a filesystem path on every platform.
    std::filesystem::path PathFromUtf8(const std::string& path)
    {
        return std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str()));
    }

    std::string Utf8FromPath(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::vector<uint8_t> ReadWholeFile(const std::string& path)
    {
        std::ifstream stream(PathFromUtf8(path), std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot open the file");
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
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

        const std::filesystem::path folder = PathFromUtf8(prefPath);
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

        return Utf8FromPath(settings);
    }

    // User definitions -- the ones the user imported, as opposed to the official ones in
    // VIA's bundle -- all in user_definitions\ of the data folder, index.json included. See
    // library/NazgDefinitionLibrary.h. A library that cannot be opened -- a damaged index,
    // which it leaves untouched -- is reported and the app runs without one.
    // A file picked for import that is for a board the library already has a definition of
    // -- same VID:PID and name -- waiting for the user to say Replace or Keep both.
    struct PendingReplacement
    {
        std::string          path;
        std::vector<uint8_t> bytes;
        uint32_t             entryId = 0;
    };

    struct Library
    {
        std::optional<nazg::DefinitionLibrary> library;
        std::string                            error;      // why it could not be opened
        std::vector<std::string>               messages;   // what the last imports did
        std::vector<PendingReplacement>        replacements;   // asked about one at a time
    };

    Library OpenLibrary(const std::optional<std::filesystem::path>& dataFolder, const std::string& dataFolderError)
    {
        Library result;
        if (!dataFolder)
        {
            result.error = dataFolderError;
            return result;
        }

        try
        {
            result.library.emplace(*dataFolder);
        }
        catch (const std::exception& failure)
        {
            result.error = failure.what();
        }
        return result;
    }

    std::string FileName(const std::string& path)
    {
        return Utf8FromPath(PathFromUtf8(path).filename());
    }

    // Copy files into the library. Each outcome is kept for the device list to show. With
    // `askOnSameBoard`, a file for a board the library already has is set aside for the
    // user's answer -- Replace or Keep both -- rather than imported.
    void ImportDefinitions(Library& library, const std::vector<std::string>& paths, bool askOnSameBoard)
    {
        library.messages.clear();

        for (const std::string& path : paths)
        {
            try
            {
                std::vector<uint8_t> bytes = ReadWholeFile(path);

                if (askOnSameBoard)
                    if (const nazg::LibraryEntry* same = library.library->FindSameBoard(nazg::ParseDefinition(bytes)))
                    {
                        library.replacements.push_back({ path, std::move(bytes), same->id });
                        continue;
                    }

                const nazg::LibraryEntry& entry = library.library->Import(bytes, path, NowUtc());
                library.messages.push_back("imported " + FileName(path) + ": " + entry.name);
            }
            catch (const std::exception& failure)
            {
                library.messages.push_back("not imported " + FileName(path) + ": " + failure.what());
            }
        }
    }

    // A new version of an entry, in its place. A changed set of layout options is worth a
    // warning: the board keeps its layout choice as bits whose meaning the definition gives,
    // so groups added or reordered make the saved value pick different keys. True when the
    // entry has a new version -- not when the file was unchanged, or refused.
    bool ReplaceDefinition(Library& library, uint32_t id, const std::vector<uint8_t>& bytes, const std::string& path)
    {
        try
        {
            std::optional<std::vector<std::string>> before;
            uint32_t                                revision = 0;
            if (const nazg::LibraryEntry* entry = library.library->Find(id))
            {
                revision = entry->revision;
                try
                {
                    before = nazg::ParseDefinition(library.library->Read(*entry)).layoutLabels;
                }
                catch (const std::exception&)
                {
                    // Unreadable or unparseable: nothing to compare with, which is no reason
                    // to refuse the new version.
                }
            }

            const nazg::LibraryEntry& replaced = library.library->Replace(id, bytes, path);
            if (replaced.revision == revision)
            {
                library.messages.push_back(FileName(path) + " is unchanged -- " + replaced.name + " stays as it was");
                return false;
            }

            library.messages.push_back("replaced " + replaced.name + " with " + FileName(path) + " -- the version " +
                                       "before is kept, see \"Restore previous\"");

            if (before && *before != nazg::ParseDefinition(bytes).layoutLabels)
                library.messages.push_back("its layout options changed: a board's saved layout may now select "
                                           "different keys -- check it");
            return true;
        }
        catch (const std::exception& failure)
        {
            library.messages.push_back("not replaced with " + FileName(path) + ": " + failure.what());
            return false;
        }
    }

    // Official definitions, VIA's, beside the executable: 0.3 MB read and inflated once at
    // start -- ~40 ms on a fast desktop -- and kept, 28 MB, so opening a board is a lookup
    // (adapters/via/NazgViaBundle.h). Built by tools/update_via_bundle.py, copied there by the
    // build, shipped with releases; a checkout that never ran the tool has none, and VIA
    // boards then need a user definition.
    constexpr char c_ViaBundleName[] = "via_definitions.tar.xz";

    struct ViaBundle
    {
        std::string                              path;
        std::optional<nazg::ViaDefinitionBundle> definitions;   // empty when missing or corrupt
    };

    ViaBundle ReadViaBundle()
    {
        ViaBundle bundle;

        const char* base = SDL_GetBasePath();   // owned by SDL, ends with a separator
        bundle.path      = std::string(base != nullptr ? base : "") + c_ViaBundleName;

        std::ifstream stream(std::filesystem::path(reinterpret_cast<const char8_t*>(bundle.path.c_str())),
                             std::ios::binary);
        if (!stream)
            return bundle;

        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        // A bundle that does not inflate is as good as none.
        try
        {
            bundle.definitions.emplace(bytes);
        }
        catch (const std::exception& failure)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", bundle.path.c_str(), failure.what());
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

    // A definition written out byte for byte, as its source has it -- so it imports again
    // anywhere, into Nazg or VIA. The outcome, for the window that asked.
    std::string WriteExport(const std::vector<uint8_t>& bytes, const std::string& name, const std::string& path)
    {
        std::ofstream stream(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream)
            return "not exported to " + path + ": cannot write the file";
        return "exported " + name + " to " + path;
    }

    // A file name from a board's name: "Phoenix Project No 1" -> "Phoenix_Project_No_1.json".
    // Only what every file system accepts; a name with nothing left gives "definition.json".
    std::string ExportFileName(const std::string& name)
    {
        std::string file;
        for (const char c : name)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                file.push_back(c);
            else if (c == ' ' && !file.empty() && file.back() != '_')
                file.push_back('_');
        }
        while (!file.empty() && file.back() == '_')
            file.pop_back();
        return (file.empty() ? "definition" : file) + ".json";
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
        std::optional<nazg::DefinitionCandidate> official;     // VIA's, kept to rebuild the list
        std::optional<nazg::DefinitionRef>       inUse;        // the one drawing the board
        bool                                     isChoosing = false;   // the picker is showing

        // The definition drawing the board as its source has it, when that is VIA's bundle or
        // the board itself -- for "Export definition...". Empty for a user definition, whose
        // stored copy is read from the library when exported.
        std::vector<uint8_t> exportable;
        std::string          exportMessage;

        // What the board screen shows, Keymap first (ui/NazgSection.h), and the one chosen.
        // Made for the keyboard above once it has loaded, and dropped when a load starts.
        std::vector<std::unique_ptr<nazg::Section>> sections;
        size_t                                      activeSection = 0;

        // A section still working refers to the keyboard: nothing reloads it meanwhile.
        bool IsSectionBusy() const
        {
            return std::any_of(sections.begin(), sections.end(), [](const auto& section) { return section->IsBusy(); });
        }
    };

    // Raw HID interface VIA and Vial answer on; only these rows get an Open button.
    constexpr uint16_t c_ViaUsagePage = 0xFF60;
    constexpr uint16_t c_ViaUsage     = 0x61;

    // A keyboard Nazg can talk to, as far as enumeration tells: it exposes VIA's raw HID
    // interface, one per board. Necessary, not sufficient -- a QMK build with raw HID and
    // no VIA has it too, and then fails on Open; knowing for sure means asking each board.
    bool IsViaInterface(const nazg::HidDeviceInfo& device)
    {
        return device.usagePage == c_ViaUsagePage && device.usage == c_ViaUsage;
    }

    std::string DescribeInUse(const nazg::DefinitionCandidate& candidate)
    {
        return (candidate.definition.name.empty() ? "(unnamed)" : candidate.definition.name) + " -- " +
               nazg::DescribeDefinitionSource(candidate);
    }

    // The shown VIA board's candidates again, after the library changed: an import appears
    // in the picker at once, a removal leaves it.
    void RefreshCandidates(const Library& library, BoardState& state)
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

        if (state.official)
            state.candidates.push_back(*state.official);
        nazg::RankCandidates(state.candidates, state.identity.product);
    }

    // Open, load everything, close. A Vial board describes itself. A VIA board is drawn by
    // one of its candidates: the user definitions the caller read for its VID:PID -- passed
    // by value, so the load does not depend on the library staying put -- and VIA's own, out
    // of the bundle, which main() owns and so outlives this coroutine. `chosen` is the
    // remembered choice, if any; when it does not settle which candidate, the load stops
    // there and the picker asks.
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

        // They refer to the keyboard about to be replaced; the caller made sure none is busy.
        state.sections.clear();

        nazg::DeviceId device = nazg::c_InvalidDevice;

        try
        {
            device = co_await transport.Open(path);

            nazg::HidDeviceChannel channel(transport, device);
            nazg::VialProtocol     protocol(channel);

            // Vial first: its probe is harmless on a VIA board, which answers 0xFF.
            if (co_await protocol.Detect())
            {
                std::vector<uint8_t> json;
                state.keyboard         = co_await nazg::LoadVialKeyboard(protocol, &json);
                state.exportable       = std::move(json);
                state.definitionSource = "from the board (Vial)";
                state.isVia            = false;
                state.candidates.clear();
                state.official.reset();
                state.inUse.reset();
            }
            else
            {
                // The official definition needs the protocol, which chooses V2 or V3; then it
                // is a lookup in the bundle inflated at start.
                std::optional<nazg::DefinitionCandidate> official;
                std::optional<std::vector<uint8_t>>      officialBytes;
                if (bundle.definitions)
                {
                    const uint16_t viaProtocol = co_await protocol.GetProtocolVersion();
                    officialBytes = bundle.definitions->Find(identity.vendorId, identity.productId, viaProtocol);
                    if (const auto& bytes = officialBytes)
                        official = nazg::DefinitionCandidate{
                            nazg::DefinitionRef::Official(
                                nazg::ViaDefinitionPath(identity.vendorId, identity.productId, viaProtocol)),
                            nazg::ParseDefinition(*bytes), {} };
                }

                if (official)
                    candidates.push_back(*official);
                nazg::RankCandidates(candidates, identity.product);

                const std::optional<size_t> resolved = nazg::ResolveCandidate(candidates, chosen);

                state.isVia      = true;
                state.identity   = identity;
                state.official   = std::move(official);
                state.candidates = candidates;

                if (!resolved)
                {
                    // Nothing to draw with until the user picks: the picker takes the window.
                    state.keyboard.reset();
                    state.inUse.reset();
                    state.isChoosing = true;
                }
                else
                {
                    const nazg::DefinitionCandidate& use = candidates[*resolved];
                    state.keyboard         = co_await nazg::LoadViaKeyboard(protocol, use.definition);
                    state.definitionSource = DescribeInUse(use);
                    state.inUse            = use.ref;
                    state.exportable.clear();
                    if (use.ref.kind == nazg::DefinitionRef::Kind::Official && officialBytes)
                        state.exportable = std::move(*officialBytes);
                }
            }

            state.path          = path;
            state.activeSection = 0;
            state.exportMessage.clear();
        }
        catch (const std::exception& failure)
        {
            state.error = failure.what();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "loading the board failed: %s", failure.what());
        }

        // Outside the catch: closing an id that never opened does nothing, so this is
        // right on both paths.
        transport.Close(device);
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
    SDL_SetWindowPosition(pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(pWindow);

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
    RegisterSettings(settings);

    LoadFonts(io);

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = mainScale;

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

    Library            library = OpenLibrary(dataFolder, dataFolderError);
    PendingDialogPaths pendingPaths;    // must outlive any open dialog: main() scope
    PendingExport      pendingExport;   // likewise

    // Never while loadTask still runs -- see the Refresh button -- nor while a section works on
    // the board a load would replace. The user definitions for the board's VID:PID are read
    // here, a few KB each, and the remembered choice looked up unless the caller has just
    // made one.
    const auto isLoadRunning = [&] { return loadTask.IsValid() && !loadTask.IsDone(); };
    const auto isBoardBusy   = [&] { return isLoadRunning() || boardState.IsSectionBusy(); };

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

    const ImVec4 clearColor = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    bool showDemoWindow    = false;
    bool showAllHidDevices = false;   // the device list shows only keyboards unless asked
    bool done = false;

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(pWindow))
                done = true;
        }

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
                boardState.exportMessage = WriteExport(bytes, name, *path);
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
                ImportDefinitions(library, picked, true);
                afterLibraryChange();
            }
        }

        // Paths an earlier build remembered in imgui.ini, which ImGui reads during the first
        // frame: imported once, then dropped from the settings. Kept if there is no library.
        if (!settings.legacyViaDefinitions.empty() && library.library)
        {
            ImportDefinitions(library, settings.legacyViaDefinitions, false);
            settings.legacyViaDefinitions.clear();
            ImGui::MarkIniSettingsDirty();
        }

        if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED) != 0)
        {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Placeholder UI. Everything here is scaffolding to be replaced by the real
        // device list and capability-driven views.
        {
            ImGui::Begin("Nazg");
            ImGui::TextUnformatted("One keyboard configurator to rule them all.");
            ImGui::Separator();
            ImGui::Text("SDL version    : %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
            ImGui::Text("Dear ImGui     : %s", IMGUI_VERSION);
            ImGui::Text("GPU backend    : %s", pGpuDriver != nullptr ? pGpuDriver : "unknown");
            ImGui::Text("Display scale  : %.2f", mainScale);
            ImGui::Text("Frame          : %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Separator();
            ImGui::Checkbox("Dear ImGui demo window", &showDemoWindow);
            ImGui::End();
        }

        {
            ImGui::Begin("HID Devices");

            const bool isBusy = deviceListState.isLoading;

            ImGui::BeginDisabled(isBusy);
            if (ImGui::Button("Refresh"))
            {
                // Never replace a Task that still has work outstanding: destroying it
                // would free a coroutine frame the transport still holds a handle to.
                if (!refreshTask.IsValid() || refreshTask.IsDone())
                    refreshTask = RefreshDeviceList(transport, deviceListState);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (isBusy)
                ImGui::TextUnformatted("enumerating...");
            else
            {
                const auto keyboards = std::count_if(deviceListState.devices.begin(), deviceListState.devices.end(),
                                                     IsViaInterface);
                if (showAllHidDevices)
                    ImGui::Text("%zu keyboard(s), %zu HID interface(s)", static_cast<size_t>(keyboards),
                                deviceListState.devices.size());
                else
                    ImGui::Text("%zu keyboard(s)", static_cast<size_t>(keyboards));
            }

            if (!deviceListState.error.empty())
                nazg::ColouredText(nazg::PanelColour::Error, "%s", deviceListState.error.c_str());

            // First draft of the definitions' UI, like the rest of this window.
            ImGui::SameLine();
            ImGui::BeginDisabled(!library.library);
            if (ImGui::Button("Import VIA definition..."))
                showImportDialog();
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Add a user definition: the .json file that describes your keyboard,\n"
                                  "often named via.json, from its vendor or designer.");

            // Null when there is no bundle, or it has no manifest to count from.
            const nazg::ViaBundleManifest* manifest =
                viaBundle.definitions && viaBundle.definitions->Manifest() ? &*viaBundle.definitions->Manifest() : nullptr;

            if (!viaBundle.definitions)
                nazg::ColouredText(nazg::PanelColour::Warning, "Official definitions: not found");
            else if (manifest != nullptr)
                ImGui::Text("Official definitions: %d, from VIA", manifest->v2 + manifest->v3);
            else
                ImGui::TextUnformatted("Official definitions: from VIA");
            if (ImGui::IsItemHovered())
            {
                if (manifest != nullptr)
                    ImGui::SetTooltip("%s\nthe-via/keyboards at %s", viaBundle.path.c_str(), manifest->commit.c_str());
                else
                    ImGui::SetTooltip("%s", viaBundle.path.c_str());
            }

            if (!library.library)
            {
                nazg::ColouredText(nazg::PanelColour::Error, "User definitions are unavailable: %s",
                                   library.error.c_str());
            }
            else
            {
                ImGui::Text("User definitions: %zu", library.library->Entries().size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Utf8FromPath(library.library->Folder()).c_str());

                // Acted on after the loop, so the list is never changed while it is walked.
                enum class EntryAction
                {
                    None,
                    Reimport,
                    Restore,
                    Remove,
                };
                EntryAction action   = EntryAction::None;
                uint32_t    actionId = 0;

                for (const nazg::LibraryEntry& entry : library.library->Entries())
                {
                    ImGui::PushID(static_cast<int>(entry.id));

                    if (ImGui::SmallButton("Re-import"))
                        action = EntryAction::Reimport, actionId = entry.id;
                    ImGui::SetItemTooltip("Read %s again, after editing it.\nThe version it replaces is kept.",
                                          entry.origin.c_str());

                    if (entry.previousRevision != 0)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Restore previous"))
                            action = EntryAction::Restore, actionId = entry.id;
                        ImGui::SetItemTooltip("Go back to the version before the last re-import.\n"
                                              "The current one becomes the previous, so this can be undone.");
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove"))
                        action = EntryAction::Remove, actionId = entry.id;

                    ImGui::PopID();

                    ImGui::SameLine();
                    ImGui::Text("%04X:%04X  %s", entry.vendorId, entry.productId, entry.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("imported from %s\nfirst imported on %s", entry.origin.c_str(),
                                          entry.added.c_str());
                }

                if (action != EntryAction::None)
                    library.messages.clear();

                try
                {
                    if (action == EntryAction::Reimport)
                    {
                        const std::string    origin = library.library->Find(actionId)->origin;
                        std::vector<uint8_t> bytes;
                        try
                        {
                            bytes = ReadWholeFile(origin);
                        }
                        catch (const std::exception&)
                        {
                            throw std::runtime_error("cannot read " + origin + " -- if it moved, import it from where "
                                                     "it is now and answer Replace");
                        }
                        if (ReplaceDefinition(library, actionId, bytes, origin))
                            afterLibraryChange(actionId);
                    }
                    else if (action == EntryAction::Restore)
                    {
                        const nazg::LibraryEntry& restored = library.library->RestorePrevious(actionId);
                        library.messages.push_back("restored the previous version of " + restored.name);
                        afterLibraryChange(actionId);
                    }
                    else if (action == EntryAction::Remove)
                    {
                        library.library->Remove(actionId);
                        afterLibraryChange();
                    }
                }
                catch (const std::exception& failure)
                {
                    library.messages.push_back(std::string("failed: ") + failure.what());
                }

                for (const std::string& message : library.messages)
                    ImGui::TextDisabled("%s", message.c_str());
            }

            ImGui::Separator();

            ImGui::Checkbox("Show all HID devices", &showAllHidDevices);
            ImGui::SetItemTooltip("Only keyboards with VIA's raw HID interface are listed otherwise --\n"
                                  "the ones VIA and Vial boards answer on.");

            const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable("devices", 6, tableFlags))
            {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Product");
                ImGui::TableSetupColumn("Manufacturer");
                ImGui::TableSetupColumn("VID:PID");
                ImGui::TableSetupColumn("Usage");
                ImGui::TableSetupColumn("Interface");
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const nazg::HidDeviceInfo& device : deviceListState.devices)
                {
                    const bool isKeyboard = IsViaInterface(device);
                    if (!isKeyboard && !showAllHidDevices)
                        continue;

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    if (isKeyboard)
                    {
                        // Same rule as Refresh: a Task still running must not be replaced.
                        // A write in flight blocks it too -- it updates the board a load
                        // would swap out from under it.
                        ImGui::PushID(device.path.c_str());
                        ImGui::BeginDisabled(isBoardBusy());
                        if (ImGui::SmallButton("Open"))
                        {
                            const nazg::DeviceIdentity identity{ device.vendorId, device.productId,
                                                                 device.manufacturer, device.product,
                                                                 device.releaseNumber, device.serialNumber };
                            startLoad(device.path, identity, std::nullopt);
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(device.product.empty() ? "(unnamed)" : device.product.c_str());
                    if (ImGui::IsItemHovered() && !device.path.empty())
                        ImGui::SetTooltip("%s", device.path.c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(device.manufacturer.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%04X:%04X", device.vendorId, device.productId);

                    ImGui::TableNextColumn();
                    ImGui::Text("%04X:%04X", device.usagePage, device.usage);

                    ImGui::TableNextColumn();
                    ImGui::Text("%d", device.interfaceNumber);
                }

                ImGui::EndTable();
            }

            ImGui::End();
        }

        // An imported file for a board the library already has: Replace or Keep both, one
        // file at a time, the next asked on the next frame.
        if (library.library && !library.replacements.empty())
        {
            constexpr char c_Title[] = "Replace a user definition?";
            if (!ImGui::IsPopupOpen(c_Title))
                ImGui::OpenPopup(c_Title);

            if (ImGui::BeginPopupModal(c_Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                const PendingReplacement  pending = library.replacements.front();
                const nazg::LibraryEntry* entry   = library.library->Find(pending.entryId);

                ImGui::Text("%s is for a board you already have a user definition for:", FileName(pending.path).c_str());
                if (entry != nullptr)
                    ImGui::BulletText("%04X:%04X  %s, imported from %s", entry->vendorId, entry->productId,
                                      entry->name.c_str(), entry->origin.c_str());

                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextUnformatted("Replace puts the new file in its place and keeps the old one as its previous "
                                       "version; boards drawn with it switch to the new file. Keep both adds it as "
                                       "another definition, and Nazg asks which one draws the board.");
                ImGui::PopTextWrapPos();

                const bool replace  = ImGui::Button("Replace");
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                const bool keepBoth = ImGui::Button("Keep both");
                ImGui::SameLine();
                const bool cancel   = ImGui::Button("Don't import");

                if (replace || keepBoth || cancel)
                {
                    library.replacements.erase(library.replacements.begin());
                    ImGui::CloseCurrentPopup();
                    library.messages.clear();
                }

                if (replace)
                {
                    if (ReplaceDefinition(library, pending.entryId, pending.bytes, pending.path))
                        afterLibraryChange(pending.entryId);
                }
                else if (keepBoth)
                {
                    try
                    {
                        const nazg::LibraryEntry& added = library.library->Import(pending.bytes, pending.path, NowUtc());
                        library.messages.push_back("imported " + FileName(pending.path) + ": " + added.name);
                        afterLibraryChange();
                    }
                    catch (const std::exception& failure)
                    {
                        library.messages.push_back("not imported " + FileName(pending.path) + ": " + failure.what());
                    }
                }

                ImGui::EndPopup();
            }
        }

        if (boardState.isLoading || boardState.keyboard || boardState.isChoosing || !boardState.error.empty())
        {
            ImGui::Begin("Keyboard");

            if (boardState.isLoading)
                ImGui::TextUnformatted("loading...");
            else if (!boardState.error.empty())
                nazg::ColouredText(nazg::PanelColour::Error, "%s", boardState.error.c_str());

            if (boardState.isChoosing && !boardState.isLoading)
            {
                ImGui::Text("%s -- which definition?",
                            boardState.identity.product.empty() ? "(unnamed)" : boardState.identity.product.c_str());

                const bool isBusy = isBoardBusy();

                ImGui::BeginDisabled(isBusy);
                const nazg::DefinitionPickerAction action =
                    nazg::DrawDefinitionPicker(boardState.candidates, boardState.inUse, boardState.identity,
                                               boardState.keyboard.has_value());
                ImGui::EndDisabled();

                if (action.import && library.library)
                    showImportDialog();

                if (action.cancel)
                    boardState.isChoosing = false;

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
                    if (!boardState.keyboard || boardState.inUse != picked)
                        startLoad(boardState.path, boardState.identity, picked);
                }
            }
            else if (boardState.keyboard && !boardState.isLoading)
            {
                // The sections of a board just loaded. Keymap is on every board, so there is no
                // match rule to ask yet -- it comes with the first section that is not.
                if (boardState.sections.empty())
                    boardState.sections.push_back(std::make_unique<nazg::KeymapSection>(
                        transport, boardState.path, *boardState.keyboard, settings.hostLayout));

                const nazg::Keyboard& keyboard = *boardState.keyboard;

                // A saved id this build does not know falls back to US rather than failing.
                const nazg::HostLayout* found      = nazg::FindHostLayout(settings.hostLayout);
                const nazg::HostLayout& hostLayout = found != nullptr ? *found : nazg::UsHostLayout();

                ImGui::Text("%s -- QMK keycodes %s", keyboard.Name().c_str(),
                            nazg::QmkKeycodeVersionName(keyboard.keycodeVersion));

                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
                if (ImGui::BeginCombo("Host layout", std::string(hostLayout.name).c_str()))
                {
                    for (const nazg::HostLayout& choice : nazg::HostLayouts())
                    {
                        const bool isCurrent = &choice == &hostLayout;
                        if (ImGui::Selectable(std::string(choice.name).c_str(), isCurrent))
                        {
                            settings.hostLayout = choice.id;
                            ImGui::MarkIniSettingsDirty();
                        }
                        if (isCurrent)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Always shown, so a wrong definition is plain to see -- and cheap to undo:
                // the keymap lives in the board, the definition only draws it.
                ImGui::TextDisabled("Definition: %s", boardState.definitionSource.c_str());

                // For investigation and debugging: the definition drawing the board, byte for byte
                // as Nazg has it -- VIA's bundle, the board itself, or the library's stored copy.
                // The one export there is, next to the line saying which definition it is.
                const nazg::LibraryEntry* userEntry =
                    boardState.inUse && boardState.inUse->kind == nazg::DefinitionRef::Kind::User && library.library
                        ? library.library->Find(boardState.inUse->userId)
                        : nullptr;

                if (!boardState.exportable.empty() || userEntry != nullptr)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Export definition..."))
                    {
                        try
                        {
                            // A user definition offers the name of the file it came from.
                            if (userEntry != nullptr)
                                startExport(library.library->Read(*userEntry), userEntry->name,
                                            FileName(userEntry->origin));
                            else
                                startExport(boardState.exportable, keyboard.Name(), ExportFileName(keyboard.Name()));
                        }
                        catch (const std::exception& failure)
                        {
                            boardState.exportMessage = std::string("not exported: ") + failure.what();
                        }
                    }
                    ImGui::SetItemTooltip("For investigation and debugging: save the definition drawing this board\n"
                                          "exactly as Nazg has it.");
                }

                if (boardState.isVia)
                {
                    ImGui::SameLine();
                    ImGui::BeginDisabled(isBoardBusy());
                    if (ImGui::SmallButton("Change definition..."))
                        boardState.isChoosing = true;
                    ImGui::EndDisabled();

                    const bool hasChoice = library.library && library.library->FindChoice(boardState.identity) != nullptr;
                    if (hasChoice)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Forget choice"))
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
                        ImGui::SetItemTooltip("Nazg asks again the next time this board is opened,\n"
                                              "if more than one definition matches it.");
                    }
                }

                if (!boardState.exportMessage.empty())
                    ImGui::TextDisabled("%s", boardState.exportMessage.c_str());

                nazg::DrawSections(boardState.sections, boardState.activeSection, keyboard);
            }

            ImGui::End();
        }

        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        ImGui::Render();
        ImDrawData* pDrawData = ImGui::GetDrawData();
        const bool isMinimized = (pDrawData->DisplaySize.x <= 0.0f || pDrawData->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* pCommandBuffer = SDL_AcquireGPUCommandBuffer(pGpuDevice);

        SDL_GPUTexture* pSwapchainTexture = nullptr;
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
    }

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
