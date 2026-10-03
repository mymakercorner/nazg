// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The palette: every theme against the rules of ui-design.md, "The board's look" -- so a new
// theme, or KLE keycap colours once the parser keeps them, cannot break them without failing
// the build:
// - the keycap's tones keep their order of lightness: faces lighter than the plate, the lip
//   darker, each at least 0.04 apart in OKLab;
// - every command category is legible on every keycap class: its band reaches 3:1 (Firmware
//   4.5:1) on every face, and its header 4.5:1 (Firmware 7:1) on the alpha and modifier faces;
// - the categories stay apart under protanopia, deuteranopia and tritanopia, simulated with
//   Machado's matrices (severity 1.0), as the palette search that chose the hues did -- asserted
//   on the caps the search was run on, Light's and Dark's alphas and modifiers, and measured on
//   the others: Dracula's alphas and the accent caps fall below it (ui-design.md, "Step 4 as
//   built").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgPalette.h"

#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using nazg::CategoryUse;
using nazg::CommandCategory;
using nazg::PackedColour;
using nazg::Palette;
using nazg::ThemeId;

namespace
{
    struct NamedTheme
    {
        ThemeId     id;
        const char* name;
    };

    constexpr NamedTheme c_Themes[] = { { ThemeId::Light, "Light" }, { ThemeId::Dark, "Dark" },
                                        { ThemeId::Dracula, "Dracula" } };

    // The smallest OKLab distance the palette search reached between two categories, under every
    // deficiency, on the caps it searched -- Light's and Dark's alphas and modifiers: 0.086 in
    // the mockup, 0.085 here with colours rounded to 8 bits as drawn.
    constexpr double c_SearchedDistance = 0.08;

    constexpr CommandCategory c_Categories[] = { CommandCategory::Behaviour, CommandCategory::Host,
                                                 CommandCategory::Board, CommandCategory::Firmware };

    const char* NameOf(CommandCategory category)
    {
        switch (category)
        {
        case CommandCategory::Behaviour: return "Behaviour";
        case CommandCategory::Host:      return "Host";
        case CommandCategory::Board:     return "Board";
        case CommandCategory::Firmware:  return "Firmware";
        default:                         return "None";
        }
    }

    struct NamedFace
    {
        const char*  name;
        PackedColour face;
        bool         extreme;   // an alpha or modifier face, light or dark -- not a mid-tone accent
    };

    std::vector<NamedFace> FacesOf(const Palette& palette)
    {
        return { { "alpha", palette.alpha.face, true },
                 { "modifier", palette.modifier.face, true },
                 { "accent", palette.accentCap.face, false } };
    }

    double Lightness(PackedColour colour)
    {
        return nazg::ToOkLab(nazg::ToLinear(colour)).L;
    }

    void TestLightnessOrder()
    {
        std::printf("lightness order\n");

        for (const NamedTheme& theme : c_Themes)
        {
            const Palette& palette = nazg::PaletteOf(theme.id);
            const double   plate   = Lightness(palette.plate);
            const double   alpha   = Lightness(palette.alpha.face);
            const double   mod     = Lightness(palette.modifier.face);
            const double   lip     = Lightness(palette.lip);
            std::printf("    %s: alpha %.3f, modifier %.3f, plate %.3f, lip %.3f\n", theme.name, alpha, mod, plate, lip);

            const std::string faces = std::string(theme.name) + ": alpha and modifier faces 0.04 lighter than the plate";
            Check(alpha >= plate + 0.04 && mod >= plate + 0.04, faces.c_str());
            const std::string lips = std::string(theme.name) + ": the lip 0.04 darker than the plate";
            Check(lip <= plate - 0.04, lips.c_str());
        }
    }

    void TestLegibility()
    {
        std::printf("categories legible on every face\n");

        int  fallbacks   = 0;
        bool bandsSolve  = true;
        bool textOnMain  = true;
        bool meetTargets = true;

        for (const NamedTheme& theme : c_Themes)
        {
            const Palette& palette = nazg::PaletteOf(theme.id);
            for (const NamedFace& face : FacesOf(palette))
                for (CommandCategory category : c_Categories)
                {
                    const auto band = nazg::CategoryColour(palette, face.face, category, CategoryUse::Band);
                    const auto text = nazg::CategoryColour(palette, face.face, category, CategoryUse::Text);

                    bandsSolve &= band.has_value();
                    if (band)
                        meetTargets &= nazg::Contrast(*band, face.face) >= nazg::TargetOf(category, CategoryUse::Band);

                    if (text)
                        meetTargets &= nazg::Contrast(*text, face.face) >= nazg::TargetOf(category, CategoryUse::Text);
                    else
                    {
                        ++fallbacks;
                        textOnMain &= !face.extreme;
                        std::printf("    %s %s, %s: header in the legend colour\n", theme.name, face.name,
                                    NameOf(category));
                    }
                }
        }

        Check(bandsSolve, "every category's band is legible on every face of every theme");
        Check(textOnMain, "every category's header is legible on every alpha and modifier face");
        Check(meetTargets, "every solved colour reaches its contrast, as packed");
        Check(!nazg::CategoryColour(nazg::PaletteOf(ThemeId::Light), nazg::Hex(0xffffff), CommandCategory::None,
                                    CategoryUse::Text),
              "no category, no colour");
        std::printf("    %d headers fall back to the legend colour\n", fallbacks);
    }

    // Machado, Oliveira and Fernandes (2009), severity 1.0, on linear RGB.
    struct Simulation
    {
        const char* name;
        double      m[3][3];
    };

    constexpr Simulation c_Simulations[] = {
        { "normal vision", { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } },
        { "protanopia",
          { { 0.152286, 1.052583, -0.204868 }, { 0.114503, 0.786281, 0.099216 }, { -0.003882, -0.048116, 1.051998 } } },
        { "deuteranopia",
          { { 0.367322, 0.860646, -0.227968 }, { 0.280085, 0.672501, 0.047413 }, { -0.011820, 0.042940, 0.968881 } } },
        { "tritanopia",
          { { 1.255528, -0.076749, -0.178779 }, { -0.078411, 0.930809, 0.147602 }, { 0.004733, 0.691367, 0.303900 } } },
    };

    nazg::OkLab Simulate(const Simulation& simulation, PackedColour colour)
    {
        const nazg::LinearRgb c = nazg::ToLinear(colour);
        const auto            row = [&](int i)
        {
            const double v = simulation.m[i][0] * c.r + simulation.m[i][1] * c.g + simulation.m[i][2] * c.b;
            return std::clamp(v, 0.0, 1.0);
        };
        return nazg::ToOkLab({ row(0), row(1), row(2) });
    }

    double Distance(const nazg::OkLab& a, const nazg::OkLab& b)
    {
        return std::sqrt((a.L - b.L) * (a.L - b.L) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b));
    }

    // The smallest OKLab distance between two categories' headers on one face, under each
    // simulation -- what tells a violet Behaviour header from a cyan Host one.
    void TestColourBlindness()
    {
        std::printf("categories apart under colour blindness\n");

        double lowestSearched = 1.0;   // on the caps the palette search was run on
        double lowestOthers   = 1.0;
        for (const NamedTheme& theme : c_Themes)
        {
            const Palette& palette = nazg::PaletteOf(theme.id);
            for (const NamedFace& face : FacesOf(palette))
                for (const Simulation& simulation : c_Simulations)
                {
                    double      lowest = 1.0;
                    std::string closest;
                    for (size_t i = 0; i < std::size(c_Categories); ++i)
                        for (size_t j = i + 1; j < std::size(c_Categories); ++j)
                        {
                            const auto a = nazg::CategoryColour(palette, face.face, c_Categories[i], CategoryUse::Text);
                            const auto b = nazg::CategoryColour(palette, face.face, c_Categories[j], CategoryUse::Text);
                            if (!a || !b)
                                continue;

                            const double distance = Distance(Simulate(simulation, *a), Simulate(simulation, *b));
                            if (distance < lowest)
                            {
                                lowest  = distance;
                                closest = std::string(NameOf(c_Categories[i])) + " and " + NameOf(c_Categories[j]);
                            }
                        }

                    std::printf("    %s %s, %s: %.3f, %s\n", theme.name, face.name, simulation.name, lowest,
                                closest.c_str());
                    double& tally = face.extreme && theme.id != ThemeId::Dracula ? lowestSearched : lowestOthers;
                    tally         = std::min(tally, lowest);
                }
        }

        std::printf("    lowest: %.3f on Light's and Dark's alphas and modifiers, %.3f on the other caps\n",
                    lowestSearched, lowestOthers);
        Check(lowestSearched >= c_SearchedDistance,
              "headers at least 0.08 apart on Light's and Dark's alphas and modifiers, under every deficiency");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestLightnessOrder();
    TestLegibility();
    TestColourBlindness();

    return TestResult();
}
