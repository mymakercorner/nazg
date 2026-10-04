// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgPalette.h"

#include <algorithm>
#include <cmath>

namespace nazg
{
    namespace
    {
        // Every theme keeps the keycap's three tones in one order of lightness: the faces of the
        // alphas and modifiers lighter than the plate, the lip darker than it, each at least 0.04
        // apart in OKLab -- else a cap and the plate merge, or the lip vanishes into the plate
        // (Rico, 2026-10-03: Light's modifiers had the plate's colour). Accent caps stand apart by
        // their hue. The lip is one colour for every key, the alphas' (Rico, 2026-10-03: a shade
        // of each cap's own colour was disturbing, on Dark most) -- on Light a little darker, to
        // stay below the slate accent face too.
        //
        // The values are the mockup's, ui-design/board-look.html. Light and Dark share their
        // state hues, Dark's lighter; Dracula, Rico's own, takes the shared ones too. The
        // selection is the theme's text colour -- no hue, so it meets no category, highlight or
        // warning colour. The accent caps are slate on Light, a lifted slate on Dark.
        constexpr Palette c_Light{
            Hex(0xf3f3f5), Hex(0xe3e4e9), Hex(0x1d1e22), Hex(0x6b6e78), Hex(0xffffff), Hex(0xc8cad2), Hex(0x3b82f6),
            Hex(0xffffff),

            Hex(0xd3d4d8),
            { Hex(0xffffff), Hex(0x24262b) },
            { Hex(0xe4e5ea), Hex(0x24262b) },
            { Hex(0xafbfd5), Hex(0x18222f) },
            Hex(0xacafb7),
            Hex(0xb4b7c2),
            Hex(0x1c7ed6), Hex(0x1c7ed6, 0.22f), Hex(0xbf308f), Hex(0xbf308f, 0.20f), Hex(0x2f9e44), Hex(0x2f9e44, 0.32f),
            Hex(0xe03131),
            Hex(0x1c64f2, 0.50f), Hex(0x000000, 0.08f), Hex(0xf3f3f5, 0.72f),

            Hex(0xe03131), Hex(0xe8590c), Hex(0x2f9e44),
        };

        constexpr Palette c_Dark{
            Hex(0x0f0f11), Hex(0x24252b), Hex(0xececf1), Hex(0x80838f), Hex(0x20232b), Hex(0x3a3d48), Hex(0x4296fa),
            Hex(0xffffff),

            Hex(0x1c1d23),
            { Hex(0x3a3c48), Hex(0xebebf0) },
            { Hex(0x2c2e38), Hex(0xebebf0) },
            { Hex(0x5a6a80), Hex(0xe8eff9) },
            Hex(0x12131b),
            Hex(0x4b4e5c),
            Hex(0x71b6ff), Hex(0x71b6ff, 0.27f), Hex(0xf387c7), Hex(0xf387c7, 0.27f), Hex(0x78dc82), Hex(0x5ac86e, 0.43f),
            Hex(0xff8b7f),
            Hex(0x3c6eff, 0.67f), Hex(0xffffff, 0.13f), Hex(0x0c0c10, 0.75f),

            Hex(0xff8b7f), Hex(0xffb34d), Hex(0x78dc82),
        };

        constexpr Palette c_Dracula{
            Hex(0x282a36), Hex(0x21222c), Hex(0xf8f8f2), Hex(0x6272a4), Hex(0x343746), Hex(0x44475a), Hex(0xbd93f9),
            Hex(0x282a36),

            Hex(0x21222c),
            { Hex(0x44475a), Hex(0xf8f8f2) },
            { Hex(0x373949), Hex(0xf8f8f2) },
            { Hex(0xbd93f9), Hex(0x282a36) },
            Hex(0x151725),
            Hex(0x6272a4),
            Hex(0x71b6ff), Hex(0x71b6ff, 0.25f), Hex(0xf387c7), Hex(0xf387c7, 0.25f), Hex(0x50fa7b), Hex(0x50fa7b, 0.33f),
            Hex(0xff8b7f),
            Hex(0xf1fa8c, 0.45f), Hex(0xffffff, 0.10f), Hex(0x282a36, 0.75f),

            Hex(0xff5555), Hex(0xffb86c), Hex(0x50fa7b),
        };

        // The chroma the solver asks for before clamping to sRGB: the mockup's.
        constexpr double c_Chroma = 0.16;

        // How finely lightness is walked: 200 steps from black to white.
        constexpr int c_Steps = 200;

        double SrgbToLinear(double c)
        {
            return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double LinearToSrgb(double c)
        {
            return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        }

        LinearRgb OklchToLinear(double L, double C, double hue)
        {
            const double radians = hue * 3.14159265358979323846 / 180.0;
            const double a       = C * std::cos(radians);
            const double b       = C * std::sin(radians);

            const double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3.0);
            const double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3.0);
            const double s = std::pow(L - 0.0894841775 * a - 1.2914855480 * b, 3.0);

            return { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                     -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                     -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s };
        }

        bool InGamut(const LinearRgb& v)
        {
            return v.r >= 0.0 && v.r <= 1.0 && v.g >= 0.0 && v.g <= 1.0 && v.b >= 0.0 && v.b <= 1.0;
        }

        // The most chroma up to `chroma` that sRGB can show at this lightness and hue.
        LinearRgb ClampChroma(double L, double chroma, double hue)
        {
            if (InGamut(OklchToLinear(L, chroma, hue)))
                return OklchToLinear(L, chroma, hue);

            double low = 0.0, high = chroma;
            for (int i = 0; i < 20; ++i)
            {
                const double middle = (low + high) / 2.0;
                (InGamut(OklchToLinear(L, middle, hue)) ? low : high) = middle;
            }
            return OklchToLinear(L, low, hue);
        }

        PackedColour Pack(const LinearRgb& v)
        {
            const auto channel = [](double c)
            { return static_cast<uint32_t>(std::lround(255.0 * LinearToSrgb(std::clamp(c, 0.0, 1.0)))); };
            return (channel(v.r) << 16) | (channel(v.g) << 8) | channel(v.b);
        }
    }

    const Palette& PaletteOf(ThemeId theme)
    {
        switch (theme)
        {
        case ThemeId::Light:   return c_Light;
        case ThemeId::Dark:    break;
        case ThemeId::Dracula: return c_Dracula;
        }
        return c_Dark;
    }

    float HueOf(const CategoryHues& hues, CommandCategory category)
    {
        switch (category)
        {
        case CommandCategory::Behaviour: return hues.behaviour;
        case CommandCategory::Host:      return hues.host;
        case CommandCategory::Board:     return hues.board;
        case CommandCategory::Firmware:  return hues.firmware;
        case CommandCategory::None:      break;
        }
        return 0.0f;
    }

    float TargetOf(CommandCategory category, CategoryUse use)
    {
        // Firmware is solved to a higher contrast, so it differs in lightness as well as hue.
        if (category == CommandCategory::Firmware)
            return use == CategoryUse::Text ? 7.0f : 4.5f;
        return use == CategoryUse::Text ? 4.5f : 3.0f;
    }

    std::optional<PackedColour> SolveCategoryColour(PackedColour face, float hue, float target)
    {
        const double faceLuminance = Luminance(ToLinear(face));
        const bool   darker        = faceLuminance > 0.18;
        const double faceL         = ToOkLab(ToLinear(face)).L;

        for (int step = 0; step <= c_Steps; ++step)
        {
            // Only past the face, in the direction chosen: on a mid-tone cap, black already reaches
            // a band's 3:1 against it, and a walk from black stopped there -- every band on Dark's
            // slate Esc and Enter came out black.
            const double L = darker ? 1.0 - double(step) / c_Steps : double(step) / c_Steps;
            if (darker ? L > faceL : L < faceL)
                continue;

            const uint32_t     rgb    = Pack(ClampChroma(L, c_Chroma, hue));
            const PackedColour colour = Hex(rgb);
            if (Contrast(colour, face) >= target)
                return colour;
        }
        return std::nullopt;
    }

    std::optional<PackedColour> CategoryColour(const Palette& palette, PackedColour face, CommandCategory category,
                                               CategoryUse use)
    {
        if (category == CommandCategory::None)
            return std::nullopt;
        return SolveCategoryColour(face, HueOf(palette.categories, category), TargetOf(category, use));
    }

    LinearRgb ToLinear(PackedColour colour)
    {
        return { SrgbToLinear((colour & 0xFF) / 255.0), SrgbToLinear(((colour >> 8) & 0xFF) / 255.0),
                 SrgbToLinear(((colour >> 16) & 0xFF) / 255.0) };
    }

    double Luminance(const LinearRgb& colour)
    {
        return 0.2126 * colour.r + 0.7152 * colour.g + 0.0722 * colour.b;
    }

    double Contrast(PackedColour a, PackedColour b)
    {
        const double ya = Luminance(ToLinear(a));
        const double yb = Luminance(ToLinear(b));
        return (std::max(ya, yb) + 0.05) / (std::min(ya, yb) + 0.05);
    }

    OkLab ToOkLab(const LinearRgb& c)
    {
        const double l = std::cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
        const double m = std::cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
        const double s = std::cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);

        return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
    }
}
