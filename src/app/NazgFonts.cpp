// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgFonts.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "imgui.h"

#include "files/NazgFiles.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgLegendFont.h"
#include "ui/NazgTheme.h"
#include "ui/NazgWorkspace.h"

namespace nazg
{
    namespace
    {
        using LogFunction = std::function<void(const std::string&)>;

        // ImGui's size for the interface's font -- its line height: Arimo at 16 is a 14.3 px font
        // (interface-styling.md, "ImGui sizes a font by its line height").
        constexpr float c_FontSize = 16.0f;

        // The interface's font (interface-styling.md, "Settled"): Arimo, the legends' font, from
        // the fonts folder -- the same on every OS -- with the system's font merged behind it for
        // what Arimo lacks in device names: other scripts, symbols. ImGui 1.92 rasterises glyphs on
        // demand, so no glyph ranges are listed. Without Arimo, the system font alone; without that,
        // ImGui's built-in ASCII font. Returns the bold face, for headers, or null.
        ImFont* LoadInterfaceFont(ImGuiIO& io, const std::string& fontsFolder, const LogFunction& logError)
        {
            const std::string regular = fontsFolder + "Arimo-Regular.ttf";
            const std::string bold    = fontsFolder + "Arimo-Bold.ttf";

#if defined(_WIN32)
            const char*       windir = std::getenv("WINDIR");
            const std::string fonts  = std::string(windir != nullptr ? windir : "C:\\Windows") + "\\Fonts\\";

            const std::vector<std::string> primary  = { fonts + "segoeui.ttf", fonts + "arial.ttf" };
            const std::vector<std::string> fallback = { fonts + "seguisym.ttf" };
#elif defined(__APPLE__)
            const std::vector<std::string> primary  = { "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                                                        "/System/Library/Fonts/Helvetica.ttc" };
            const std::vector<std::string> fallback = {};
#else
            const std::vector<std::string> primary  = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                                        "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                                        "/usr/share/fonts/noto/NotoSans-Regular.ttf" };
            const std::vector<std::string> fallback = {};
#endif

            bool haveBase = std::filesystem::exists(PathFromUtf8(regular)) &&
                            io.Fonts->AddFontFromFileTTF(regular.c_str(), c_FontSize) != nullptr;
            if (!haveBase)
                logError("interface font missing: " + regular);

            // The system's font: behind Arimo, or in its place.
            ImFontConfig merge;
            merge.MergeMode = haveBase;
            for (const std::string& path : primary)
            {
                if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path.c_str(), c_FontSize, &merge) != nullptr)
                {
                    haveBase        = true;
                    merge.MergeMode = true;
                    break;
                }
            }

            if (!haveBase)
            {
                logError("no interface font found; it falls back to ImGui's ASCII font");
                return nullptr;
            }

            for (const std::string& path : fallback)
                if (std::filesystem::exists(path))
                    io.Fonts->AddFontFromFileTTF(path.c_str(), c_FontSize, &merge);

            return std::filesystem::exists(PathFromUtf8(bold)) ? io.Fonts->AddFontFromFileTTF(bold.c_str(), c_FontSize)
                                                               : nullptr;
        }

        // The legends' fonts. A missing Arimo leaves that weight null, and the board uses the
        // interface's font instead.
        LegendFonts LoadLegendFonts(ImGuiIO& io, const std::string& fontsFolder, const LogFunction& logError)
        {
            std::vector<std::string> missing;
            const LegendFonts        fonts = nazg::LoadLegendFonts(*io.Fonts, fontsFolder, missing);
            for (const std::string& path : missing)
                logError("legend font missing: " + path);
            return fonts;
        }

        // Tabler's icons (ui/NazgIcons.h). A font of their own rather than merged into the
        // interface's, so the icons are drawn at their size whatever the text's. Missing, sections
        // show monograms.
        ImFont* LoadIconFont(ImGuiIO& io, const std::string& fontsFolder, const LogFunction& logError)
        {
            const std::string path = fontsFolder + "tabler-icons.ttf";

            ImFont* const font = std::filesystem::exists(PathFromUtf8(path))
                                     ? io.Fonts->AddFontFromFileTTF(path.c_str(), c_FontSize)
                                     : nullptr;
            if (font == nullptr)
                logError("icon font missing: " + path);
            return font;
        }
    }

    void LoadFonts(ImGuiIO& io, const std::string& fontsFolder, const LogFunction& logError)
    {
        SetHeaderFont(LoadInterfaceFont(io, fontsFolder, logError));
        SetLegendFonts(LoadLegendFonts(io, fontsFolder, logError));
        SetIconFont(LoadIconFont(io, fontsFolder, logError));
    }
}
