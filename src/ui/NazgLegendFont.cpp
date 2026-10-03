// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLegendFont.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"   // ImTextCharFromUtf8

namespace nazg
{
    namespace
    {
        // A reference size only: ImGui 1.92 bakes glyphs at whatever size text is drawn.
        constexpr float c_ReferenceSize = 16.0f;

        std::filesystem::path PathFromUtf8(const std::string& path)
        {
            return std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str()));
        }

        // A font's line height in em, as ImGui's stb_truetype sizes it: the 'hhea' ascent minus
        // its descent, over the 'head' units per em. 0 when the file cannot be read.
        float LineHeightPerEm(const std::string& path)
        {
            std::ifstream stream(PathFromUtf8(path), std::ios::binary);
            const std::vector<unsigned char> data((std::istreambuf_iterator<char>(stream)),
                                                  std::istreambuf_iterator<char>());

            const auto u16 = [&](size_t at) { return at + 2 <= data.size() ? (data[at] << 8) | data[at + 1] : 0; };
            const auto u32 = [&](size_t at)
            { return at + 4 <= data.size() ? (static_cast<size_t>(u16(at)) << 16) | static_cast<size_t>(u16(at + 2)) : 0; };

            size_t head = 0, hhea = 0;
            const size_t tables = static_cast<size_t>(u16(4));
            for (size_t i = 0; i < tables; ++i)
            {
                const size_t record = 12 + 16 * i;
                if (record + 16 > data.size())
                    break;
                const std::string tag(data.begin() + static_cast<ptrdiff_t>(record),
                                      data.begin() + static_cast<ptrdiff_t>(record + 4));
                if (tag == "head")
                    head = u32(record + 8);
                else if (tag == "hhea")
                    hhea = u32(record + 8);
            }

            const int unitsPerEm = u16(head + 18);
            const int ascent     = static_cast<int16_t>(u16(hhea + 4));
            const int descent    = static_cast<int16_t>(u16(hhea + 6));
            if (head == 0 || hhea == 0 || unitsPerEm == 0)
                return 0.0f;
            return static_cast<float>(ascent - descent) / static_cast<float>(unitsPerEm);
        }
    }

    LegendFonts LoadLegendFonts(ImFontAtlas& atlas, const std::string& folder, std::vector<std::string>& missing)
    {
        LegendFonts fonts;

        const auto exists = [&](const std::string& path)
        {
            if (std::filesystem::exists(PathFromUtf8(path)))
                return true;
            missing.push_back(path);
            return false;
        };

        // Arimo's line height sets the scale; each merged face is sized so its em is Arimo's.
        const float arimoLine = LineHeightPerEm(folder + "Arimo-Regular.ttf");
        if (arimoLine > 0.0f)
            fonts.sizePerEm = arimoLine;

        const auto load = [&](const char* face) -> ImFont*
        {
            const std::string path = folder + face;
            if (!exists(path))
                return nullptr;

            ImFont* font = atlas.AddFontFromFileTTF(path.c_str(), c_ReferenceSize);
            if (font == nullptr)
                return nullptr;

            for (const char* fallback : { "NotoSansArabic-Regular.ttf", "NotoSansMath-Regular.ttf",
                                          "NotoSansSymbols2-Regular.ttf" })
            {
                const std::string fallbackPath = folder + fallback;
                const float       line         = LineHeightPerEm(fallbackPath);
                if (!exists(fallbackPath) || line <= 0.0f)
                    continue;

                ImFontConfig merge;
                merge.MergeMode = true;
                atlas.AddFontFromFileTTF(fallbackPath.c_str(), c_ReferenceSize * line / fonts.sizePerEm, &merge);
            }
            return font;
        };

        fonts.regular = load("Arimo-Regular.ttf");
        fonts.bold    = load("Arimo-Bold.ttf");
        return fonts;
    }

    ImFont* ImGuiTextMeasurer::FontFor(LegendWeight weight) const
    {
        ImFont* wanted = weight == LegendWeight::Bold ? m_Fonts.bold : m_Fonts.regular;
        ImFont* other  = weight == LegendWeight::Bold ? m_Fonts.regular : m_Fonts.bold;
        return wanted != nullptr ? wanted : other != nullptr ? other : m_Fallback;
    }

    float ImGuiTextMeasurer::Width(std::string_view text, float size, LegendWeight weight) const
    {
        ImFont* font = FontFor(weight);
        if (font == nullptr || text.empty())
            return 0.0f;
        return font->CalcTextSizeA(ImGuiSize(size), FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
    }

    InkExtent ImGuiTextMeasurer::Ink(std::string_view text, float size, LegendWeight weight) const
    {
        ImFont* font = FontFor(weight);
        if (font == nullptr)
            return {};

        ImFontBaked* baked = font->GetFontBaked(ImGuiSize(size));
        InkExtent    ink{ FLT_MAX, -FLT_MAX };

        const char* at  = text.data();
        const char* end = text.data() + text.size();
        while (at < end)
        {
            unsigned int codepoint = 0;
            at += ImTextCharFromUtf8(&codepoint, at, end);

            const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
            if (glyph == nullptr || !glyph->Visible)
                continue;
            ink.top    = std::min(ink.top, glyph->Y0);
            ink.bottom = std::max(ink.bottom, glyph->Y1);
        }

        if (ink.top > ink.bottom)
            return {};
        return ink;
    }
}
