// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Nazg - one keyboard configurator to rule them all.
//
// The platform shell: every SDL and renderer call in the project, and nothing else. It makes
// the window, the GPU device and the ImGui context, hands App (app/NazgApp.h) what only the
// platform knows, and runs the frame loop around it. Keep it that way: it is what makes
// switching the renderer backend (SDL_GPU -> OpenGL3) or adding an Emscripten path a
// single-file change. See CLAUDE.md.
//
// The renderer is chosen at start: SDL_GPU -- Metal, Direct3D 12 or Vulkan -- where SDL finds a
// device, else OpenGL through imgui_impl_opengl3: a Linux computer with no Vulkan driver, an old
// GPU, a virtual machine (tester-feedback.md, 22). Neither: a message in Nazg's words, saying what
// to install, or the browser.
//
// In a browser (__EMSCRIPTEN__; build-and-release.md, "The web build") the window is the page's
// canvas, drawn with OpenGL ES 3 -- WebGL 2 -- since SDL_GPU has no WebGPU backend, and the
// browser runs the frame loop, a pass per display refresh.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#if defined(__EMSCRIPTEN__)
#include <SDL3/SDL_opengles2.h>   // GLint, GL_APIENTRY: WebGL 2 is OpenGL ES 3's
#include <emscripten.h>
#define NAZG_GL_API GL_APIENTRY
#else
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION   // OpenGL is the fallback there, deprecated but working
#endif
#include <SDL3/SDL_opengl.h>      // GLint, APIENTRY: the shell's few calls are found at run time
#include "imgui_impl_sdlgpu3.h"
#define NAZG_GL_API APIENTRY
#endif

#include "app/NazgApp.h"
#include "files/NazgFiles.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
// A browser's file dialogs. To open: a hidden <input type="file">, clicked while the click on Nazg's
// button still counts; the files picked are copied into memory under /uploads and their paths left in
// Module.nazgChosenFiles for the frame loop -- `which` says for whom -- none when cancelled. To save:
// the file written in memory, then handed to the browser as a download.
EM_JS_DEPS(nazg_shell, "$FS,$stringToNewUTF8,$UTF8ToString");

EM_JS(void, NazgChooseFiles, (const char* accept, int multiple, int which), {
    const input    = document.createElement("input");
    input.type     = "file";
    input.accept   = UTF8ToString(accept);
    input.multiple = multiple !== 0;
    input.addEventListener("change", async () => {
        const paths = [];
        FS.mkdirTree("/uploads");
        for (const file of input.files) {
            const path = "/uploads/" + file.name;
            FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));
            paths.push(path);
        }
        Module.nazgChosenFiles = { which: which, paths: paths };
    });
    input.addEventListener("cancel", () => { Module.nazgChosenFiles = { which: which, paths: [] }; });
    input.click();
});

// What NazgChooseFiles() left, once: "<which>\n<path>\n<path>...", for free(); null while nothing is.
EM_JS(char*, NazgTakeChosenFiles, (), {
    const chosen = Module.nazgChosenFiles;
    if (!chosen)
        return 0;
    Module.nazgChosenFiles = null;
    return stringToNewUTF8([String(chosen.which)].concat(chosen.paths).join("\n"));
});

EM_JS(void, NazgOfferDownload, (const char* path), {
    try {
        const name = UTF8ToString(path);
        const url  = URL.createObjectURL(new Blob([FS.readFile(name)]));
        const link = document.createElement("a");
        link.href     = url;
        link.download = name.split("/").pop();
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
    } catch (error) {
        console.error("Nazg: no download for " + UTF8ToString(path) + ": " + error);
    }
});
#endif

namespace
{
    constexpr int   c_DefaultWindowWidth  = 1280;
    constexpr int   c_DefaultWindowHeight = 800;
    constexpr char  c_WindowTitle[]       = "Nazg";

    // A key as USB numbers it, the page above the usage (model/NazgHostKeys.h): SDL's scancodes up to
    // 0xFF are the keyboard page's own usages; its media keys, past them, are the consumer page's.
    std::optional<uint32_t> HidUsageOf(SDL_Scancode scancode)
    {
        if (scancode > SDL_SCANCODE_UNKNOWN && scancode <= 0xFF)
            return (0x07u << 16) | static_cast<uint32_t>(scancode);
        switch (scancode)
        {
        case SDL_SCANCODE_MEDIA_NEXT_TRACK:     return (0x0Cu << 16) | 0xB5;
        case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK: return (0x0Cu << 16) | 0xB6;
        case SDL_SCANCODE_MEDIA_STOP:           return (0x0Cu << 16) | 0xB7;
        case SDL_SCANCODE_MEDIA_PLAY_PAUSE:     return (0x0Cu << 16) | 0xCD;
        default:                                return std::nullopt;
        }
    }

    // The window where it was left, if that is still on a display -- its top-left corner, where
    // the title bar is, in some display's usable area -- and no bigger than that area. Otherwise,
    // and on the first run, centred on the main display and as large as most of it: a full-size
    // board needs more than 1280 px (ui-design.md, "The floor is on the header").
    void PlaceWindow(SDL_Window* window, const nazg::AppSettings::WindowPlace& saved, float scale)
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
    // maximized. App writes it with the rest, a second after a change and on exit.
    //
    // A maximized window moved to another display -- Win+Shift+Arrow, its title bar dragged --
    // takes its place along: the same size, no bigger than that display, centred on it. Kept where
    // it was, the place would bring Nazg back maximized on the first display, whichever it was left
    // on (tester-feedback.md, 29).
    void TrackWindowPlace(SDL_Window* window, nazg::AppSettings::WindowPlace& saved)
    {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
        if ((flags & SDL_WINDOW_MINIMIZED) != 0)
            return;

        nazg::AppSettings::WindowPlace place = saved;
        place.maximized                = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (!place.maximized)
        {
            SDL_GetWindowPosition(window, &place.x, &place.y);
            SDL_GetWindowSize(window, &place.width, &place.height);
        }
        else
        {
            const SDL_DisplayID on     = SDL_GetDisplayForWindow(window);
            const SDL_Point     corner = { place.x, place.y };
            SDL_Rect            usable{};
            if (on != 0 && (place.width <= 0 || SDL_GetDisplayForPoint(&corner) != on) && SDL_GetDisplayUsableBounds(on, &usable))
            {
                place.width  = place.width > 0 ? std::min(place.width, usable.w) : usable.w * 85 / 100;
                place.height = place.height > 0 ? std::min(place.height, usable.h) : usable.h * 85 / 100;
                place.x      = usable.x + (usable.w - place.width) / 2;
                place.y      = usable.y + (usable.h - place.height) / 2;
            }
        }

        saved = place;
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
    // on Windows. It holds the settings (settings.json) and the user definitions, so every build
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

    // SDL may run a file dialog's callback on another thread, so what the user picked waits here
    // until the frame loop hands it to App.
    struct DialogAnswers
    {
        std::mutex                 mutex;
        std::vector<std::string>   imported;               // files to import
        bool                       isExportClosed = false;   // the save dialog closed...
        std::optional<std::string> exportPath;             // ...with a place chosen, or none
        std::string                suggested;              // the file name offered; SDL may read it until it closes
        std::string                exportKind;             // its filter, which SDL may read until it closes too
        std::string                exportPattern;
        SDL_DialogFileFilter       exportFilter{};

        // The saved layouts' dialogs (.vil; VIA's .layout.json opened too), one at a time: closed, with a file chosen or none.
        bool                       isLayoutOpenClosed = false;
        std::optional<std::string> layoutOpenPath;
        bool                       isLayoutSaveClosed = false;
        std::optional<std::string> layoutSavePath;
        std::string                layoutSuggested;
    };

    void SDLCALL OnImportChosen(void* userdata, const char* const* filelist, int)
    {
        if (filelist == nullptr)   // an error; SDL_GetError() says which
            return;

        auto&                             answers = *static_cast<DialogAnswers*>(userdata);
        const std::lock_guard<std::mutex> lock(answers.mutex);
        for (const char* const* path = filelist; *path != nullptr; ++path)
            answers.imported.emplace_back(*path);
    }

    void SDLCALL OnExportChosen(void* userdata, const char* const* filelist, int)
    {
        auto&                             answers = *static_cast<DialogAnswers*>(userdata);
        const std::lock_guard<std::mutex> lock(answers.mutex);

        answers.isExportClosed = true;
        answers.exportPath.reset();
        if (filelist != nullptr && *filelist != nullptr)   // null: an error; empty: cancelled
            answers.exportPath = *filelist;
    }

    // A dialog's first file, or none -- cancelled, or an error.
    std::optional<std::string> FirstChosen(const char* const* filelist)
    {
        if (filelist == nullptr || *filelist == nullptr)
            return std::nullopt;
        return std::string(*filelist);
    }

    void SDLCALL OnLayoutOpenChosen(void* userdata, const char* const* filelist, int)
    {
        auto&                             answers = *static_cast<DialogAnswers*>(userdata);
        const std::lock_guard<std::mutex> lock(answers.mutex);
        answers.isLayoutOpenClosed = true;
        answers.layoutOpenPath     = FirstChosen(filelist);
    }

    void SDLCALL OnLayoutSaveChosen(void* userdata, const char* const* filelist, int)
    {
        auto&                             answers = *static_cast<DialogAnswers*>(userdata);
        const std::lock_guard<std::mutex> lock(answers.mutex);
        answers.isLayoutSaveClosed = true;
        answers.layoutSavePath     = FirstChosen(filelist);
    }

#if defined(__EMSCRIPTEN__)
    // NazgChooseFiles()'s `which`: the dialog the files were picked in.
    constexpr int c_ChoosingImport = 0;
    constexpr int c_ChoosingLayout = 1;

    // Where a browser's save dialog puts a file: memory, until it is downloaded under its own name.
    std::string DownloadPath(const std::string& suggested)
    {
        std::error_code ignored;
        std::filesystem::create_directories("/downloads", ignored);
        return "/downloads/" + suggested.substr(suggested.find_last_of('/') + 1);
    }

    // The files picked in a browser's open dialog, if any came since the last frame, handed to App
    // as SDL's dialogs hand theirs.
    void TakeBrowserFiles(DialogAnswers& answers)
    {
        char* const chosen = NazgTakeChosenFiles();
        if (chosen == nullptr)
            return;

        std::vector<std::string> lines;
        for (const char* line = chosen; *line != '\0';)
        {
            const char* end = std::strchr(line, '\n');
            lines.emplace_back(line, end != nullptr ? end : line + std::strlen(line));
            line = end != nullptr ? end + 1 : line + std::strlen(line);
        }
        std::free(chosen);

        const std::lock_guard<std::mutex> lock(answers.mutex);
        if (lines.empty())
            return;
        if (lines[0] == std::to_string(c_ChoosingImport))
            answers.imported.insert(answers.imported.end(), lines.begin() + 1, lines.end());
        else
        {
            answers.isLayoutOpenClosed = true;
            answers.layoutOpenPath     = lines.size() > 1 ? std::optional<std::string>(lines[1]) : std::nullopt;
        }
    }
#endif

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

    // The window's icon where the platform takes it from the app (resources/icon/README.md): 32 px
    // for 100% display scale, 64 and 256 for SDL to pick from above it. Not on Windows, where
    // SDL's window class already carries the exe's icon (src/Nazg.rc), drawn for every size; nor
    // on macOS, where SDL would put it in the Dock over the app's nazg.icns, drawn on Apple's grid.
    [[maybe_unused]] void SetWindowIcon(SDL_Window* window, const std::string& executableFolder)
    {
        SDL_Surface* const icon = SDL_LoadPNG((executableFolder + "icon/nazg-32.png").c_str());
        if (icon == nullptr)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No window icon: %s", SDL_GetError());
            return;
        }

        for (const char* size : { "64", "256" })
        {
            SDL_Surface* const larger = SDL_LoadPNG((executableFolder + "icon/nazg-" + size + ".png").c_str());
            if (larger == nullptr)
                continue;
            SDL_AddSurfaceAlternateImage(icon, larger);
            SDL_DestroySurface(larger);   // icon keeps a reference of its own
        }

        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

    // The content scale of the display the window is on -- Windows' 150 %, a desktop's scaling --
    // never the pixel density (macOS Retina, a browser's zoom), which ImGui's backend reads on its
    // own: SDL_GetWindowDisplayScale() multiplies the two. 0 when SDL cannot tell, which App ignores.
    float WindowContentScale(SDL_Window* window)
    {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        return display != 0 ? SDL_GetDisplayContentScale(display) : 0.0f;
    }

    // A failure before the first frame, logged and shown: Nazg has no console, so without the
    // message box a user sees nothing at all (tester-feedback.md, 1). Called before anything is
    // cleaned up, while SDL_GetError() still says why. SDL allows the box before SDL_Init().
    void ReportStartupFailure(const char* call)
    {
        const std::string error = SDL_GetError();
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed: %s", call, error.c_str());

        const std::string text = std::string("Nazg could not start.\n\n") + call + " failed:\n" +
                                 (error.empty() ? "no reason given" : error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Nazg", text.c_str(), nullptr);
    }

    // SDL_GetError(), or words for its silence.
    [[maybe_unused]] std::string SdlReason()
    {
        const char* const error = SDL_GetError();
        return error != nullptr && *error != '\0' ? error : "no reason given";
    }

    // How a frame is drawn: SDL_GPU where it has a device, else OpenGL -- always, in a browser.
    enum class Renderer
    {
        SdlGpu,
        OpenGl,
    };

    // The few OpenGL calls the shell makes itself, around ImGui's: found once the context exists,
    // so nothing links OpenGL -- the desktop only loads it when SDL_GPU has no device.
    struct GlCalls
    {
        void(NAZG_GL_API* viewport)(GLint, GLint, GLsizei, GLsizei)        = nullptr;
        void(NAZG_GL_API* clearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
        void(NAZG_GL_API* clear)(GLbitfield)                              = nullptr;
        const GLubyte*(NAZG_GL_API* getString)(GLenum)                    = nullptr;

        [[nodiscard]] bool IsComplete() const { return viewport && clearColor && clear && getString; }
    };

    GlCalls FindGlCalls()
    {
        GlCalls calls;
        calls.viewport   = reinterpret_cast<decltype(calls.viewport)>(SDL_GL_GetProcAddress("glViewport"));
        calls.clearColor = reinterpret_cast<decltype(calls.clearColor)>(SDL_GL_GetProcAddress("glClearColor"));
        calls.clear      = reinterpret_cast<decltype(calls.clear)>(SDL_GL_GetProcAddress("glClear"));
        calls.getString  = reinterpret_cast<decltype(calls.getString)>(SDL_GL_GetProcAddress("glGetString"));
        return calls;
    }

#if !defined(__EMSCRIPTEN__)
    // What Nazg asks SDL_GPU for. SDL picks the backend per platform: Metal on macOS, D3D12 on
    // Windows, Vulkan on Linux. The shader formats are the ones ImGui's precompiled shaders come in
    // (imgui_impl_sdlgpu3_shaders.h) -- DXBC for D3D12, not DXIL: SDL asks a D3D12 GPU for Shader
    // Model 6 only when DXIL is offered, and refuses it without. ImGui uses no storage resources,
    // so Tier 1 resource binding is enough, which lets Intel's Haswell and Broadwell graphics in
    // (SDL_gpu.h). Debug builds only ask for the validation layers.
    SDL_PropertiesID GpuProperties()
    {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);
#if defined(NDEBUG)
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
#else
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
#endif
        return properties;
    }

    // The OpenGL context asked for before the window is made: 3.2 core on macOS, the only modern
    // one it has; 3.0 elsewhere, which every Mesa driver and Windows' own give. ImGui picks the
    // GLSL to match (imgui_impl_opengl3.cpp).
    void AskForOpenGl()
    {
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    // Neither renderer: what Nazg needs on this system and what to do, in Nazg's words, then what
    // the system said (tester-feedback.md, 22). Logged and shown, like ReportStartupFailure().
    void ReportNoRenderer(const std::string& gpuReason, const std::string& glReason)
    {
        std::string text = "Nazg could not start: it found no way to draw its window on this computer.\n\n";
#if defined(__APPLE__)
        text += "Nazg draws with Metal, or else with OpenGL, and neither could be used.\n";
#elif defined(_WIN32)
        text += "Nazg draws with Direct3D 12 or Vulkan, or else with OpenGL, and none could be used. "
                "Updating the graphics driver usually brings them.\n";
#else
        text += "Nazg draws with Vulkan, or else with OpenGL, and neither could be used. Installing your "
                "distribution's Vulkan driver usually brings it -- mesa-vulkan-drivers on Debian, Ubuntu "
                "and Linux Mint.\n";
#endif
        text += "\nNazg also runs in Chrome or Edge, with nothing to install: https://mymakercorner.com/nazg/\n\n"
                "What the system said:\n"
                "SDL_GPU: " + gpuReason + "\n"
                "OpenGL: " + glReason;

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Nazg", text.c_str(), nullptr);
    }
#endif
}

namespace
{
    // The computer's game controllers, for the Switches section's gamepad test: SDL's joysticks started
    // only while it shows, and the board's own opened by its vendor and product numbers among whatever
    // others there are. An Xbox one is read as SDL's gamepad, whose parts it orders the same on every
    // system; any other as the joystick it is, its axes in HID's order.
    class GameControllers
    {
    public:
        void Watch(bool watch)
        {
            if (watch == m_Watching)
                return;
            m_Watching = watch;
            Close();
            if (!watch)
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            else if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(SDL_INIT_GAMEPAD) failed: %s", SDL_GetError());
                m_Watching = false;
            }
        }

        nazg::GameControllerReading Read(uint16_t vendorId, uint16_t productId)
        {
            nazg::GameControllerReading reading;
            if (!m_Watching)
                return reading;
            SDL_UpdateJoysticks();

            SDL_JoystickID  own   = 0;
            int             count = 0;
            SDL_JoystickID* ids   = SDL_GetJoysticks(&count);
            for (int index = 0; ids && index < count; ++index)
            {
                if (!own && SDL_GetJoystickVendorForID(ids[index]) == vendorId &&
                    SDL_GetJoystickProductForID(ids[index]) == productId)
                    own = ids[index];
                else
                    ++reading.others;
            }
            SDL_free(ids);

            if (own != m_Id)
                Close();
            if (!own)
                return reading;
            if (!m_Id)
                Open(own);

            if (m_Gamepad)
            {
                reading.isSeen = true;
                reading.isXbox = true;
                for (const SDL_GamepadAxis axis :
                     { SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
                       SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER })
                    reading.axes.push_back(Unit(SDL_GetGamepadAxis(m_Gamepad, axis)));
                for (const SDL_GamepadButton button :
                     { SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                       SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                       SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                       SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                       SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_GUIDE })
                    reading.buttons.push_back(SDL_GetGamepadButton(m_Gamepad, button));
            }
            else if (m_Joystick)
            {
                reading.isSeen = true;
                for (int axis = 0; axis < SDL_GetNumJoystickAxes(m_Joystick); ++axis)
                    reading.axes.push_back(Unit(SDL_GetJoystickAxis(m_Joystick, axis)));
                for (int button = 0; button < SDL_GetNumJoystickButtons(m_Joystick); ++button)
                    reading.buttons.push_back(SDL_GetJoystickButton(m_Joystick, button));
            }
            return reading;
        }

    private:
        static float Unit(Sint16 value)
        {
            return value < 0 ? static_cast<float>(value) / 32768.0f : static_cast<float>(value) / 32767.0f;
        }

        void Open(SDL_JoystickID id)
        {
            m_Id                       = id;
            const SDL_GamepadType type = SDL_GetGamepadTypeForID(id);
            if (SDL_IsGamepad(id) && (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE))
                m_Gamepad = SDL_OpenGamepad(id);
            if (!m_Gamepad)
                m_Joystick = SDL_OpenJoystick(id);
        }

        void Close()
        {
            if (m_Gamepad)
                SDL_CloseGamepad(m_Gamepad);
            if (m_Joystick)
                SDL_CloseJoystick(m_Joystick);
            m_Gamepad  = nullptr;
            m_Joystick = nullptr;
            m_Id       = 0;
        }

        bool           m_Watching = false;
        SDL_JoystickID m_Id       = 0;
        SDL_Gamepad*   m_Gamepad  = nullptr;
        SDL_Joystick*  m_Joystick = nullptr;
    };
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        ReportStartupFailure("SDL_Init()");
        return 1;
    }

    // Create the window, sized in logical units scaled by the display's content scale: the primary
    // display's until the window is placed, then the frame loop follows the one it is on.
    const float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    const auto createWindow = [mainScale](SDL_WindowFlags flags)
    {
        return SDL_CreateWindow(c_WindowTitle, static_cast<int>(c_DefaultWindowWidth * mainScale),
                                static_cast<int>(c_DefaultWindowHeight * mainScale), flags);
    };

    SDL_Window*   pWindow   = nullptr;
    SDL_GLContext glContext = nullptr;
    Renderer      renderer  = Renderer::OpenGl;
    std::string   gpuDriver;                                     // for the log and Settings' about lines
#if defined(__EMSCRIPTEN__)
    // The page is the window: the canvas fills it and follows the browser's window, drawn at the
    // display's own pixels. OpenGL ES 3.0 is what WebGL 2 offers.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    pWindow = createWindow(SDL_WINDOW_OPENGL | SDL_WINDOW_FILL_DOCUMENT | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (pWindow == nullptr)
    {
        ReportStartupFailure("SDL_CreateWindow()");
        SDL_Quit();
        return 1;
    }
    glContext = SDL_GL_CreateContext(pWindow);
    if (glContext == nullptr)
    {
        ReportStartupFailure("SDL_GL_CreateContext()");
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(pWindow, glContext);
    gpuDriver = "WebGL 2";
#else
    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    // SDL_GPU first. SDL_GPUSupportsProperties() runs the choice of backend that making a device
    // runs -- on Linux it loads Vulkan and looks for a GPU with what SDL needs -- with no window
    // yet: OpenGL needs one made for it (SDL_WINDOW_OPENGL), so the window waits for the answer.
    SDL_GPUDevice*         pGpuDevice    = nullptr;
    std::string            gpuReason;
    const SDL_PropertiesID gpuProperties = GpuProperties();
    if (SDL_GPUSupportsProperties(gpuProperties))
    {
        pWindow = createWindow(windowFlags);
        if (pWindow == nullptr)
        {
            ReportStartupFailure("SDL_CreateWindow()");
            SDL_DestroyProperties(gpuProperties);
            SDL_Quit();
            return 1;
        }

        // Rarely, a device is promised and still not made, or not given the window: OpenGL then,
        // in a window of its own.
        pGpuDevice = SDL_CreateGPUDeviceWithProperties(gpuProperties);
        if (pGpuDevice != nullptr && SDL_ClaimWindowForGPUDevice(pGpuDevice, pWindow))
            renderer = Renderer::SdlGpu;
        else
        {
            gpuReason = SdlReason();
            if (pGpuDevice != nullptr)
                SDL_DestroyGPUDevice(pGpuDevice);
            pGpuDevice = nullptr;
            SDL_DestroyWindow(pWindow);
            pWindow = nullptr;
        }
    }
    else
        gpuReason = SdlReason();
    SDL_DestroyProperties(gpuProperties);

    if (renderer == Renderer::SdlGpu)
    {
        SDL_SetGPUSwapchainParameters(pGpuDevice, pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        const char* const driver = SDL_GetGPUDeviceDriver(pGpuDevice);
        gpuDriver = driver != nullptr ? driver : "unknown";
    }
    else
    {
        // OpenGL: no Vulkan driver on Linux, an old GPU, a virtual machine -- Mesa's own OpenGL is
        // nearly everywhere. Neither: Nazg says what to do (tester-feedback.md, 22).
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No SDL_GPU device (%s): drawing with OpenGL", gpuReason.c_str());
        AskForOpenGl();
        pWindow = createWindow(windowFlags | SDL_WINDOW_OPENGL);
        if (pWindow != nullptr)
        {
            glContext = SDL_GL_CreateContext(pWindow);
            if (glContext == nullptr)
            {
                const std::string glReason = SdlReason();
                SDL_DestroyWindow(pWindow);
                ReportNoRenderer(gpuReason, glReason);
                SDL_Quit();
                return 1;
            }
        }
        else
        {
            ReportNoRenderer(gpuReason, SdlReason());
            SDL_Quit();
            return 1;
        }
        SDL_GL_MakeCurrent(pWindow, glContext);
        SDL_GL_SetSwapInterval(1);   // the display's pace, as SDL_GPU's VSYNC
    }
#endif

    // Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // nothing of ImGui's is kept; Nazg's settings are App's settings.json

    // OpenGL's calls, and ImGui's renderer for it -- its loader finding OpenGL's functions. Made
    // before App: a context too old for ImGui is one more reason to stop.
    GlCalls gl;
    if (renderer == Renderer::OpenGl)
    {
        gl = FindGlCalls();
#if defined(__EMSCRIPTEN__)
        const char* const glsl = "#version 300 es";
#else
        const char* const glsl = nullptr;   // ImGui's choice for the context made: 130, or 150 on macOS
#endif
        if (!gl.IsComplete() || !ImGui_ImplOpenGL3_Init(glsl))
        {
#if defined(__EMSCRIPTEN__)
            ReportStartupFailure("ImGui_ImplOpenGL3_Init()");
#else
            ReportNoRenderer(gpuReason, "ImGui could not draw with this OpenGL context");
#endif
            ImGui::DestroyContext();
            SDL_GL_DestroyContext(glContext);
            SDL_DestroyWindow(pWindow);
            SDL_Quit();
            return 1;
        }
#if !defined(__EMSCRIPTEN__)
        const GLubyte* const version = gl.getString(GL_VERSION);
        gpuDriver = std::string("OpenGL ") + (version != nullptr ? reinterpret_cast<const char*>(version) : "");
#endif
    }
    SDL_Log("GPU backend: %s", gpuDriver.c_str());

    const auto logError = [](const std::string& text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); };

    // What App needs from the platform. The data folder holds the settings and the user
    // definitions.
    nazg::PlatformServices platform;
    platform.dataFolder = DataFolder(platform.dataFolderError);

    const char* basePath      = SDL_GetBasePath();   // owned by SDL, ends with a separator
    platform.executableFolder = basePath != nullptr ? basePath : "";
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)   // a page's icon is in its HTML
    SetWindowIcon(pWindow, platform.executableFolder);
#endif
    platform.displayScale     = mainScale;
    platform.modifierNames    = PlatformModifierNames();
    platform.nowUtc           = NowUtc;
    platform.logError         = logError;

    char line[128];
    std::snprintf(line, sizeof(line), "SDL %d.%d.%d, Dear ImGui %s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                  SDL_MICRO_VERSION, IMGUI_VERSION);
    platform.about.emplace_back(line);
    platform.about.push_back("GPU backend: " + gpuDriver);
#if !defined(__EMSCRIPTEN__)
    if (renderer == Renderer::OpenGl)
        platform.about.push_back("No SDL_GPU device: " + gpuReason);   // why OpenGL, for a tester's report
#endif

    // Must outlive any open dialog: main() scope.
    DialogAnswers answers;
    platform.showImportDialog = [&answers, pWindow]
    {
        // Static: SDL may read the filters until the dialog closes.
        static const SDL_DialogFileFilter c_Filters[] = { { "VIA definition", "json" } };
        SDL_ShowOpenFileDialog(OnImportChosen, &answers, pWindow, c_Filters, 1, nullptr, true);
    };
    platform.showExportDialog = [&answers, pWindow](const std::string& suggested, const std::string& kind,
                                                    const std::string& extension)
    {
        answers.suggested     = suggested;   // App opens one at a time
        answers.exportKind    = kind;
        answers.exportPattern = extension;
        answers.exportFilter  = { answers.exportKind.c_str(), answers.exportPattern.c_str() };
        SDL_ShowSaveFileDialog(OnExportChosen, &answers, pWindow, &answers.exportFilter, 1,
                               answers.suggested.empty() ? nullptr : answers.suggested.c_str());
    };
    platform.showLayoutOpenDialog = [&answers, pWindow]
    {
        // Vial's .vil and VIA's .layout.json: App tells them apart by their content.
        static const SDL_DialogFileFilter c_Filters[] = { { "Vial or VIA layout", "vil;json" } };
        SDL_ShowOpenFileDialog(OnLayoutOpenChosen, &answers, pWindow, c_Filters, 1, nullptr, false);
    };
    platform.takeKeyboard = [pWindow](bool take)
    {
        if (take)
            SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");
        else
            SDL_ResetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4);
        SDL_SetWindowKeyboardGrab(pWindow, take);
    };
    GameControllers controllers;
    platform.watchGameControllers = [&controllers](bool watch) { controllers.Watch(watch); };
    platform.readGameController   = [&controllers](uint16_t vendorId, uint16_t productId)
    { return controllers.Read(vendorId, productId); };
    platform.openUrl = [](const std::string& url)
    {
        if (!SDL_OpenURL(url.c_str()))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_OpenURL() failed: %s", SDL_GetError());
    };
    platform.showLayoutSaveDialog = [&answers, pWindow](const std::string& suggested)
    {
        answers.layoutSuggested = suggested;   // App opens one at a time
        static const SDL_DialogFileFilter c_Filters[] = { { "Vial layout", "vil" } };
        SDL_ShowSaveFileDialog(OnLayoutSaveChosen, &answers, pWindow, c_Filters, 1, answers.layoutSuggested.c_str());
    };
#if defined(__EMSCRIPTEN__)
    // In a browser SDL has no file dialogs -- it answers "cancelled" -- so the browser's own: an open
    // dialog's files come back through TakeBrowserFiles(); a save names a file in memory at once, and
    // once App has written it, it is downloaded.
    platform.showImportDialog     = [] { NazgChooseFiles(".json", 1, c_ChoosingImport); };
    platform.showLayoutOpenDialog = [] { NazgChooseFiles(".vil,.json", 0, c_ChoosingLayout); };
    platform.showExportDialog     = [&answers](const std::string& suggested, const std::string&, const std::string& extension)
    {
        const std::lock_guard<std::mutex> lock(answers.mutex);
        answers.isExportClosed = true;
        answers.exportPath     = DownloadPath(suggested.empty() ? "nazg." + extension : suggested);
    };
    platform.showLayoutSaveDialog = [&answers](const std::string& suggested)
    {
        const std::lock_guard<std::mutex> lock(answers.mutex);
        answers.isLayoutSaveClosed = true;
        answers.layoutSavePath     = DownloadPath(suggested);
    };
    platform.fileSaved = [](const std::string& path) { NazgOfferDownload(path.c_str()); };
#endif

    {
        // Nazg itself, with its settings read: the window shows where it was left -- in a browser,
        // where the user left it, the canvas filling it.
        nazg::App app(std::move(platform));

#if !defined(__EMSCRIPTEN__)
        PlaceWindow(pWindow, app.Settings().window, mainScale);
        SDL_ShowWindow(pWindow);
#endif

        if (renderer == Renderer::OpenGl)
            ImGui_ImplSDL3_InitForOpenGL(pWindow, glContext);
#if !defined(__EMSCRIPTEN__)
        else
        {
            ImGui_ImplSDL3_InitForSDLGPU(pWindow);

            ImGui_ImplSDLGPU3_InitInfo initInfo = {};
            initInfo.Device               = pGpuDevice;
            initInfo.ColorTargetFormat    = SDL_GetGPUSwapchainTextureFormat(pGpuDevice, pWindow);
            initInfo.MSAASamples          = SDL_GPU_SAMPLECOUNT_1;
            initInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
            initInfo.PresentMode          = SDL_GPU_PRESENTMODE_VSYNC;
            ImGui_ImplSDLGPU3_Init(&initInfo);
        }
#endif

        const ImVec4 clearColor = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);

        // The window's minimum size, as last given to SDL.
        nazg::App::MinimumSize appliedMinimum;

        // One frame: the dialogs' answers handed over, App's work before the frame, then the frame
        // drawn and presented. Run by the loop below, and from inside SDL's event pumping while
        // the window is being moved or resized -- `isLive`, see DrawDuringLiveResize(). Never
        // inside itself.
        //
        // A live frame never waits for the display: Windows handles the mouse in the same loop,
        // which a wait for VSYNC -- up to 16.7 ms at 60 Hz -- would hold up on every tick. It is
        // skipped instead when the swapchain has no texture ready, and the next tick tries again.
        // (Dragging the window still jitters at 60 Hz, for a reason outside Nazg: ui-design.md,
        // "Open points".)
        bool                      isDrawing       = false;
        [[maybe_unused]] bool     isSwapImmediate = false;   // OpenGL's swap interval is 0, for a live frame
        std::function<void(bool)> drawFrame       = [&](bool isLive)
        {
            if (isDrawing)
                return;
            isDrawing = true;

            {
                std::vector<std::string>   imported;
                bool                       isExportClosed = false;
                std::optional<std::string> exportPath;
                bool                       isLayoutOpenClosed = false;
                std::optional<std::string> layoutOpenPath;
                bool                       isLayoutSaveClosed = false;
                std::optional<std::string> layoutSavePath;
                {
                    const std::lock_guard<std::mutex> lock(answers.mutex);
                    imported.swap(answers.imported);
                    isExportClosed = std::exchange(answers.isExportClosed, false);
                    exportPath     = std::move(answers.exportPath);
                    answers.exportPath.reset();
                    isLayoutOpenClosed = std::exchange(answers.isLayoutOpenClosed, false);
                    layoutOpenPath     = std::exchange(answers.layoutOpenPath, std::nullopt);
                    isLayoutSaveClosed = std::exchange(answers.isLayoutSaveClosed, false);
                    layoutSavePath     = std::exchange(answers.layoutSavePath, std::nullopt);
                }
                if (isExportClosed)
                    app.OnExportChosen(std::move(exportPath));
                if (!imported.empty())
                    app.OnImportChosen(std::move(imported));
                if (isLayoutOpenClosed)
                    app.OnLayoutOpenChosen(std::move(layoutOpenPath));
                if (isLayoutSaveClosed)
                    app.OnLayoutSaveChosen(std::move(layoutSavePath));
            }

            // Before the minimized early-out below, otherwise board work stalls for as long as
            // the window stays minimized.
            app.BeforeFrame();

            // The scale of the display the window is on, asked every frame rather than on SDL's
            // scale events: those wait in the queue while Windows runs its own loop during a drag,
            // and the window opening on its saved display is covered too. App does nothing for
            // the same scale again.
            app.OnDisplayScaleChanged(WindowContentScale(pWindow));

            if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED) != 0)
            {
                SDL_Delay(10);
                isDrawing = false;
                return;
            }

            if (renderer == Renderer::OpenGl)
                ImGui_ImplOpenGL3_NewFrame();
#if !defined(__EMSCRIPTEN__)
            else
                ImGui_ImplSDLGPU3_NewFrame();
#endif
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (const std::optional<nazg::App::MinimumSize> minimum = app.Frame(); minimum && !(*minimum == appliedMinimum))
            {
                SDL_SetWindowMinimumSize(pWindow, minimum->width, minimum->height);
                appliedMinimum = *minimum;
            }

            ImGui::Render();
            ImDrawData* pDrawData = ImGui::GetDrawData();
            const bool isMinimized = (pDrawData->DisplaySize.x <= 0.0f || pDrawData->DisplaySize.y <= 0.0f);

            if (renderer == Renderer::OpenGl)
            {
                // At the window's pixels. In a browser the canvas is shown after this frame's turn;
                // on the desktop a live frame does not wait for the display, as SDL_GPU's does not.
#if defined(__EMSCRIPTEN__)
                (void)isLive;
#else
                if (isLive != isSwapImmediate)
                {
                    SDL_GL_SetSwapInterval(isLive ? 0 : 1);
                    isSwapImmediate = isLive;
                }
#endif
                if (!isMinimized)
                {
                    gl.viewport(0, 0, static_cast<GLsizei>(pDrawData->DisplaySize.x * pDrawData->FramebufferScale.x),
                                static_cast<GLsizei>(pDrawData->DisplaySize.y * pDrawData->FramebufferScale.y));
                    gl.clearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
                    gl.clear(GL_COLOR_BUFFER_BIT);
                    ImGui_ImplOpenGL3_RenderDrawData(pDrawData);
                    SDL_GL_SwapWindow(pWindow);
                }
                isDrawing = false;
                return;
            }

#if !defined(__EMSCRIPTEN__)
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
#endif
            isDrawing = false;
        };

#if !defined(__EMSCRIPTEN__)
        SDL_AddEventWatch(DrawDuringLiveResize, &drawFrame);

        // A HID device came or went: SDL counts what the OS notices -- not which device, and it
        // opens none. The first call starts SDL's own copy of hidapi, apart from Nazg's
        // (ui-design.md, "Hotplug").
        Uint32 deviceChanges = SDL_hid_device_change_count();
#else
        // In a browser WebHID counts them: a chosen device plugged in or unplugged.
        Uint32 deviceChanges = nazg::HidTransport::DeviceChangeCount();
#endif

        // One pass of the frame loop: the events, then the frame.
        bool                  done = false;
        std::function<void()> pass = [&]
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);

                // Asked, not done: changes not written yet are asked about first.
                if (event.type == SDL_EVENT_QUIT)
                    app.RequestQuit();
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(pWindow))
                    app.RequestQuit();

                // The computer's keys, for the live test: repeats are not presses.
                if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat)
                    if (const std::optional<uint32_t> usage = HidUsageOf(event.key.scancode))
                        app.OnHostKey(*usage, event.type == SDL_EVENT_KEY_DOWN);
            }
#if defined(__EMSCRIPTEN__)
            TakeBrowserFiles(answers);
            const Uint32 changes = nazg::HidTransport::DeviceChangeCount();
#else
            TrackWindowPlace(pWindow, app.Settings().window);
            const Uint32 changes = SDL_hid_device_change_count();
#endif
            if (changes != deviceChanges)
            {
                deviceChanges = changes;
                app.OnDevicesChanged();
            }

            drawFrame(false);
            done = app.IsQuitConfirmed();
        };

#if defined(__EMSCRIPTEN__)
        // The browser runs the loop, a pass per display refresh, and main() is left by unwinding it.
        // Emscripten's JavaScript-based exceptions let that unwinding through without running a
        // destructor, so main()'s locals -- App among them -- live on with the page; WebAssembly's
        // own would destroy them before the first frame (client-architecture.md, "Exceptions").
        // Closing the tab is the only way out: nothing below runs in a browser.
        emscripten_set_main_loop_arg([](void* loopPass) { (*static_cast<std::function<void()>*>(loopPass))(); }, &pass,
                                     0, true);
#else
        while (!done)
            pass();

        SDL_RemoveEventWatch(DrawDuringLiveResize, &drawFrame);
#endif
    }

#if !defined(__EMSCRIPTEN__)
    if (renderer == Renderer::SdlGpu)
    {
        SDL_WaitForGPUIdle(pGpuDevice);
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui::DestroyContext();

        SDL_ReleaseWindowFromGPUDevice(pGpuDevice, pWindow);
        SDL_DestroyGPUDevice(pGpuDevice);
    }
    else
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();

        SDL_GL_DestroyContext(glContext);
    }
    SDL_DestroyWindow(pWindow);
    SDL_hid_exit();   // started by the change count
#endif
    SDL_Quit();

    return 0;
}
