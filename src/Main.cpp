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
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgKeyboardView.h"
#include "ui/NazgKeycodePicker.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        std::string              hostLayout = "us";   // a HostLayout id; see NazgKeycapLegend.h
        std::vector<std::string> viaDefinitions;      // paths of VIA definition files, in load order
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
            static_cast<AppSettings*>(self->UserData)->viaDefinitions.clear();
        };

        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* self, void*, const char* line)
        {
            AppSettings& loaded = *static_cast<AppSettings*>(self->UserData);

            constexpr char c_HostLayout[]    = "HostLayout=";
            constexpr char c_ViaDefinition[] = "ViaDefinition=";

            if (std::strncmp(line, c_HostLayout, sizeof(c_HostLayout) - 1) == 0)
                loaded.hostLayout = line + sizeof(c_HostLayout) - 1;
            else if (std::strncmp(line, c_ViaDefinition, sizeof(c_ViaDefinition) - 1) == 0)
                loaded.viaDefinitions.emplace_back(line + sizeof(c_ViaDefinition) - 1);
        };

        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out)
        {
            const AppSettings& saved = *static_cast<AppSettings*>(self->UserData);
            out->appendf("[%s][Settings]\nHostLayout=%s\n", self->TypeName, saved.hostLayout.c_str());
            for (const std::string& path : saved.viaDefinitions)
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

    // TEMPORARY: VIA definitions come from files the user picks, one list for the session,
    // remembered in AppSettings. A registry snapshot or fetch can replace or join this.
    struct ViaDefinitionFile
    {
        std::string                             path;
        std::optional<nazg::KeyboardDefinition> definition;   // empty if the file did not parse
        std::string                             error;
    };

    ViaDefinitionFile ReadViaDefinition(const std::string& path)
    {
        ViaDefinitionFile file;
        file.path = path;

        try
        {
            std::ifstream stream(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())),
                                 std::ios::binary);
            if (!stream)
                throw std::runtime_error("cannot open the file");

            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            file.definition = nazg::ParseDefinition(bytes);
        }
        catch (const std::exception& failure)
        {
            file.error = failure.what();
        }

        return file;
    }

    // VIA's official definitions, beside the executable: read once at start -- 0.3 MB -- and
    // inflated only when a board needs one (adapters/via/NazgViaBundle.h). Built by
    // tools/update_via_bundle.py, copied there by the build, shipped with releases; a
    // checkout that never ran the tool has none, and VIA boards then need a loaded file.
    constexpr char c_ViaBundleName[] = "via_definitions.tar.xz";

    struct ViaBundle
    {
        std::string          path;
        std::vector<uint8_t> bytes;   // empty when the file is missing or unreadable
    };

    ViaBundle ReadViaBundle()
    {
        ViaBundle bundle;

        const char* base = SDL_GetBasePath();   // owned by SDL, ends with a separator
        bundle.path      = std::string(base != nullptr ? base : "") + c_ViaBundleName;

        std::ifstream stream(std::filesystem::path(reinterpret_cast<const char8_t*>(bundle.path.c_str())),
                             std::ios::binary);
        if (stream)
            bundle.bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

        return bundle;
    }

    // Bring the parsed list in line with the saved paths: parse what is new, drop what was
    // removed. Cheap when nothing changed, so it simply runs every frame -- which also
    // covers the saved list arriving from imgui.ini during the first frame.
    void SyncViaDefinitions(const AppSettings& settings, std::vector<ViaDefinitionFile>& files)
    {
        std::erase_if(files, [&](const ViaDefinitionFile& file)
                      { return std::find(settings.viaDefinitions.begin(), settings.viaDefinitions.end(), file.path) ==
                               settings.viaDefinitions.end(); });

        for (const std::string& path : settings.viaDefinitions)
            if (std::none_of(files.begin(), files.end(), [&](const ViaDefinitionFile& file) { return file.path == path; }))
                files.push_back(ReadViaDefinition(path));
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

    // The board being shown. Same lifetime rule as DeviceListState: owned by main().
    struct BoardState
    {
        std::optional<nazg::Keyboard> keyboard;
        std::string                   path;               // where it was opened, for writes
        std::string                   definitionSource;   // where its definition came from, shown with it
        std::string                   error;
        bool                          isLoading = false;
        int                           layer     = 0;

        nazg::KeySelection       selection;
        nazg::KeycodePickerState picker;
        std::string              editMessage;
        bool                     editWarning = false;
        bool                     isWriting   = false;
    };

    // Raw HID interface VIA and Vial answer on; only these rows get an Open button.
    constexpr uint16_t c_ViaUsagePage = 0xFF60;
    constexpr uint16_t c_ViaUsage     = 0x61;

    // Open, load everything, close. A Vial board describes itself. A VIA board takes the
    // definition file the caller found for its VID:PID -- passed by value, so the load does
    // not depend on the list it came from staying put -- or else VIA's own, out of the
    // bundle, which main() owns and so outlives this coroutine.
    nazg::Task<void> LoadBoard(nazg::HidTransport&                     transport,
                               std::string                             path,
                               uint16_t                                vendorId,
                               uint16_t                                productId,
                               std::optional<nazg::KeyboardDefinition> viaDefinition,
                               std::string                             viaDefinitionPath,
                               const ViaBundle&                        bundle,
                               BoardState&                             state)
    {
        state.isLoading = true;
        state.error.clear();

        nazg::DeviceId device = nazg::c_InvalidDevice;

        try
        {
            device = co_await transport.Open(path);

            nazg::HidDeviceChannel channel(transport, device);
            nazg::VialProtocol     protocol(channel);

            std::string source;

            // Vial first: its probe is harmless on a VIA board, which answers 0xFF.
            if (co_await protocol.Detect())
            {
                state.keyboard = co_await nazg::LoadVialKeyboard(protocol);
                source         = "the board (Vial)";
            }
            else
            {
                source = viaDefinitionPath;

                // A file the user loaded wins over VIA's own, as VIA's side-loading does.
                // The bundle needs the protocol to choose V2 or V3. It is inflated here, on
                // the main thread: ~40 ms once per open on a fast desktop, while "loading"
                // is showing anyway.
                if (!viaDefinition && !bundle.bytes.empty())
                {
                    const uint16_t viaProtocol = co_await protocol.GetProtocolVersion();
                    if (const auto bytes = nazg::FindViaDefinition(bundle.bytes, vendorId, productId, viaProtocol))
                    {
                        viaDefinition = nazg::ParseDefinition(*bytes);
                        source        = "VIA's official definitions";
                    }
                }

                if (!viaDefinition)
                {
                    char ids[16];
                    std::snprintf(ids, sizeof(ids), "%04X:%04X", vendorId, productId);
                    throw std::runtime_error(std::string("not a Vial board, VIA has no definition for ") + ids +
                                             ", and none is loaded -- use \"Load VIA definition...\"");
                }

                state.keyboard = co_await nazg::LoadViaKeyboard(protocol, std::move(*viaDefinition));
            }

            state.definitionSource = source;
            state.path             = path;
            state.layer     = 0;
            state.selection = {};
            state.editMessage.clear();
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

    // Write one key and keep the model in step with what the board says it stored.
    // Everything the coroutine needs across its co_awaits is passed by value -- the
    // version too, rather than read back from state.keyboard afterwards.
    nazg::Task<void> EditKey(nazg::HidTransport&     transport,
                             BoardState&             state,
                             uint8_t                 layer,
                             uint8_t                 row,
                             uint8_t                 column,
                             nazg::Keycode           keycode,
                             nazg::QmkKeycodeVersion version)
    {
        state.isWriting = true;
        state.editMessage.clear();
        state.editWarning = false;

        nazg::DeviceId device = nazg::c_InvalidDevice;

        try
        {
            device = co_await transport.Open(state.path);

            nazg::HidDeviceChannel channel(transport, device);
            nazg::ViaProtocol      via(channel);

            const nazg::Keycode stored = co_await nazg::WriteKeycode(via, layer, row, column, keycode, version);

            if (state.keyboard)
                state.keyboard->keymap.Set(layer, row, column, stored);

            // Compared as the values on the wire: two Keycodes can differ as values yet
            // be the same key (a macro by name or by index), and the wire is what counts.
            if (nazg::EncodeQmkKeycode(stored, version) == nazg::EncodeQmkKeycode(keycode, version))
            {
                state.editMessage = "stored " + nazg::FormatKeycode(stored);
            }
            else
            {
                state.editMessage = "asked for " + nazg::FormatKeycode(keycode) + " but the board stored " +
                                    nazg::FormatKeycode(stored) + " -- a locked Vial board filters some keycodes";
                state.editWarning = true;
            }
        }
        catch (const std::exception& failure)
        {
            state.editMessage = std::string("write failed: ") + failure.what();
            state.editWarning = true;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "writing a key failed: %s", failure.what());
        }

        transport.Close(device);
        state.isWriting = false;
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

    // Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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
    nazg::Task<void> editTask;

    std::vector<ViaDefinitionFile> viaFiles;
    PendingDialogPaths             pendingPaths;   // must outlive any open dialog: main() scope

    const ImVec4 clearColor = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    bool showDemoWindow = false;
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

        // Paths picked in the file dialog since last frame, then the parsed list brought in
        // line with the saved one.
        {
            const std::lock_guard<std::mutex> lock(pendingPaths.mutex);
            for (std::string& path : pendingPaths.paths)
            {
                if (std::find(settings.viaDefinitions.begin(), settings.viaDefinitions.end(), path) ==
                    settings.viaDefinitions.end())
                {
                    settings.viaDefinitions.push_back(std::move(path));
                    ImGui::MarkIniSettingsDirty();
                }
            }
            pendingPaths.paths.clear();
        }
        SyncViaDefinitions(settings, viaFiles);

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
                ImGui::Text("%zu device(s)", deviceListState.devices.size());

            if (!deviceListState.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", deviceListState.error.c_str());

            // TEMPORARY definition sourcing: files the user picks, remembered in imgui.ini.
            ImGui::SameLine();
            if (ImGui::Button("Load VIA definition..."))
            {
                // Static: SDL may read the filters until the dialog closes.
                static const SDL_DialogFileFilter c_Filters[] = { { "VIA definition", "json" } };
                SDL_ShowOpenFileDialog(OnViaDefinitionChosen, &pendingPaths, pWindow, c_Filters, 1, nullptr, true);
            }

            if (viaBundle.bytes.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "VIA's official definitions: not found");
            else
                ImGui::Text("VIA's official definitions: %.1f KB", viaBundle.bytes.size() / 1024.0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", viaBundle.path.c_str());

            // Removing only edits the saved list; SyncViaDefinitions drops the file next frame,
            // so viaFiles is never changed while this loop walks it.
            for (const ViaDefinitionFile& file : viaFiles)
            {
                ImGui::PushID(file.path.c_str());
                if (ImGui::SmallButton("Remove"))
                {
                    std::erase(settings.viaDefinitions, file.path);
                    ImGui::MarkIniSettingsDirty();
                }
                ImGui::PopID();

                ImGui::SameLine();
                if (file.definition)
                    ImGui::Text("%04X:%04X  %s", file.definition->vendorId, file.definition->productId,
                                file.definition->name.c_str());
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", file.error.c_str());

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", file.path.c_str());
            }

            ImGui::Separator();

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
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    if (device.usagePage == c_ViaUsagePage && device.usage == c_ViaUsage)
                    {
                        // Same rule as Refresh: a Task still running must not be replaced.
                        // A write in flight blocks it too -- it updates the board a load
                        // would swap out from under it.
                        const bool isLoadBusy = (loadTask.IsValid() && !loadTask.IsDone()) ||
                                                (editTask.IsValid() && !editTask.IsDone());

                        ImGui::PushID(device.path.c_str());
                        ImGui::BeginDisabled(isLoadBusy);
                        if (ImGui::SmallButton("Open"))
                        {
                            // The first loaded definition whose ids match; unused on a Vial board.
                            std::optional<nazg::KeyboardDefinition> viaDefinition;
                            std::string                             viaDefinitionPath;
                            for (const ViaDefinitionFile& file : viaFiles)
                                if (file.definition && file.definition->vendorId == device.vendorId &&
                                    file.definition->productId == device.productId)
                                {
                                    viaDefinition     = file.definition;
                                    viaDefinitionPath = file.path;
                                    break;
                                }

                            loadTask = LoadBoard(transport, device.path, device.vendorId, device.productId,
                                                 std::move(viaDefinition), std::move(viaDefinitionPath), viaBundle,
                                                 boardState);
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

        if (boardState.isLoading || boardState.keyboard || !boardState.error.empty())
        {
            ImGui::Begin("Keyboard");

            if (boardState.isLoading)
                ImGui::TextUnformatted("loading...");
            else if (!boardState.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", boardState.error.c_str());

            if (boardState.keyboard && !boardState.isLoading)
            {
                const nazg::Keyboard& keyboard = *boardState.keyboard;

                // A saved id this build does not know falls back to US rather than failing.
                const nazg::HostLayout* found      = nazg::FindHostLayout(settings.hostLayout);
                const nazg::HostLayout& hostLayout = found != nullptr ? *found : nazg::UsHostLayout();

                ImGui::Text("%s -- QMK keycodes %s", keyboard.Name().c_str(),
                            nazg::QmkKeycodeVersionName(keyboard.keycodeVersion));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Definition from %s", boardState.definitionSource.c_str());

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

                nazg::DrawKeyboardView(keyboard, boardState.layer, hostLayout, boardState.selection);

                ImGui::Separator();

                if (!boardState.selection.active)
                {
                    ImGui::TextUnformatted("Click a key to change it.");
                }
                else
                {
                    const auto layer  = static_cast<uint8_t>(boardState.layer);
                    const auto row    = boardState.selection.row;
                    const auto column = boardState.selection.column;

                    ImGui::Text("Layer %d, row %d, column %d: %s", layer, row, column,
                                nazg::FormatKeycode(keyboard.keymap.At(layer, row, column)).c_str());

                    if (boardState.isWriting)
                        ImGui::TextUnformatted("writing...");
                    else if (!boardState.editMessage.empty())
                        ImGui::TextColored(boardState.editWarning ? ImVec4(1.0f, 0.7f, 0.3f, 1.0f)
                                                                  : ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                                           "%s", boardState.editMessage.c_str());

                    const bool isEditBusy = editTask.IsValid() && !editTask.IsDone();

                    ImGui::BeginDisabled(isEditBusy);
                    const std::optional<nazg::Keycode> picked =
                        nazg::DrawKeycodePicker(boardState.picker, keyboard.keycodeVersion, keyboard.keymap.Layers(),
                                                hostLayout);
                    ImGui::EndDisabled();

                    if (picked && !isEditBusy)
                        editTask = EditKey(transport, boardState, layer, row, column, *picked, keyboard.keycodeVersion);
                }
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
