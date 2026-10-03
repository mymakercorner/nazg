// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The legends in the real font: Arimo and its Noto faces, loaded through ImGui's core -- no
// SDL, no GPU -- exactly as the app loads them, then every standard key and every host
// layout's characters laid out on a 1u key, in both families, with every modifier name and
// side, from the smallest board to the largest. The mockup's checks (ui-design.md, "Legends --
// the plan" and short-forms.md, "Checked"): no standard legend is cut, none leaves the face,
// no two overlap. So a new QMK keycode or host layout that does not fit fails the build.
//
// Takes the fonts folder as its argument. Registered with CTest:
//   ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "imgui.h"

#include "TestSupport.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "ui/NazgKeycapLayout.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgLegendFont.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using nazg::FaceBox;
using nazg::KeycapLegend;
using nazg::KeycapPrimitives;
using nazg::LegendFamily;

namespace
{
    // Board units from the smallest -- 60 px, where headers reach the 9 px floor -- to the
    // largest the board grows to.
    constexpr float c_Units[] = { nazg::c_SmallestUnit, 72.0f, 84.0f, 96.0f };

    constexpr std::string_view c_CharacterKeys[] = {
        "KC_A", "KC_B", "KC_C", "KC_D", "KC_E", "KC_F", "KC_G", "KC_H", "KC_I", "KC_J", "KC_K", "KC_L", "KC_M",
        "KC_N", "KC_O", "KC_P", "KC_Q", "KC_R", "KC_S", "KC_T", "KC_U", "KC_V", "KC_W", "KC_X", "KC_Y", "KC_Z",
        "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8", "KC_9", "KC_0",
        "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_NUHS", "KC_SCLN", "KC_QUOT", "KC_GRV",
        "KC_COMM", "KC_DOT", "KC_SLSH", "KC_NUBS",
    };

    struct Box
    {
        float x0, y0, x1, y1;
    };

    // What each primitive inks: text by its glyphs' bounds, an arrow by its shape.
    std::vector<Box> InkBoxes(const KeycapPrimitives& primitives, const nazg::TextMeasurer& measurer)
    {
        std::vector<Box> boxes;
        for (const nazg::PlacedText& text : primitives.texts)
        {
            const nazg::InkExtent ink = measurer.Ink(text.text, text.size, text.weight);
            boxes.push_back({ text.x, text.y + ink.top, text.x + text.width, text.y + ink.bottom });
        }
        for (const nazg::PlacedArrow& arrow : primitives.arrows)
        {
            const nazg::ArrowShape shape = nazg::ShapeOf(arrow);
            const float xs[] = { shape.tailX, shape.tipX, shape.baseAX, shape.baseBX };
            const float ys[] = { shape.tailY, shape.tipY, shape.baseAY, shape.baseBY };
            const float half = arrow.stroke / 2.0f;
            boxes.push_back({ *std::min_element(std::begin(xs), std::end(xs)) - half,
                              *std::min_element(std::begin(ys), std::end(ys)) - half,
                              *std::max_element(std::begin(xs), std::end(xs)) + half,
                              *std::max_element(std::begin(ys), std::end(ys)) + half });
        }
        return boxes;
    }

    // Glyph bounds are the bitmaps' whole-pixel boxes, up to a pixel larger than the ink on
    // each side: a pixel either way is not an overlap.
    constexpr float c_Tolerance = 1.0f;

    bool Overlap(const Box& a, const Box& b)
    {
        return a.x0 < b.x1 - c_Tolerance && b.x0 < a.x1 - c_Tolerance && a.y0 < b.y1 - c_Tolerance &&
               b.y0 < a.y1 - c_Tolerance;
    }

    bool Inside(const Box& box, const FaceBox& face)
    {
        return box.x0 >= face.x0 - c_Tolerance && box.x1 <= face.x1 + c_Tolerance && box.y0 >= face.y0 - c_Tolerance &&
               box.y1 <= face.y1 + c_Tolerance;
    }

    struct Tally
    {
        int         draws    = 0;
        int         cut      = 0;
        int         outside  = 0;
        int         overlaps = 0;
        std::set<std::string> reported;   // each key and family once, whatever the size

        void Report(const std::string& what, const std::string& key)
        {
            if (reported.insert(key).second && reported.size() <= 400)
                std::printf("    %s\n", what.c_str());
        }
    };

    std::string Describe(const KeycapPrimitives& primitives)
    {
        std::string text;
        for (const nazg::PlacedText& placed : primitives.texts)
            text += "[" + placed.text + "]";
        if (!primitives.arrows.empty())
            text += "[arrow]";
        return text;
    }

    // One legend on a 1u key, every family and size.
    void Draw(const KeycapLegend& legend, const std::string& name, bool cutAllowed,
              const nazg::TextMeasurer& measurer, Tally& tally)
    {
        for (LegendFamily family : { LegendFamily::Cylindrical, LegendFamily::Spherical })
            for (float unit : c_Units)
            {
                const float   gap = nazg::c_KeyGap * unit;
                const FaceBox face{ gap, gap, unit - gap, unit - gap };

                const KeycapPrimitives primitives = nazg::LayOutKeycap(legend, family, face, unit, true, measurer);
                const std::vector<Box> boxes      = InkBoxes(primitives, measurer);
                const std::string      key   = name + (family == LegendFamily::Spherical ? " spherical" : " cylindrical");
                const std::string      where = key + " " + std::to_string(static_cast<int>(unit)) + " px: " + Describe(primitives);
                ++tally.draws;

                for (const nazg::PlacedText& text : primitives.texts)
                    if (text.cut && !cutAllowed)
                    {
                        ++tally.cut;
                        tally.Report("cut: " + where, key);
                    }

                for (const Box& box : boxes)
                    if (!Inside(box, face))
                    {
                        ++tally.outside;
                        tally.Report("leaves the face: " + where, key);
                        break;
                    }

                for (size_t i = 0; i < boxes.size(); ++i)
                    for (size_t j = i + 1; j < boxes.size(); ++j)
                        if (Overlap(boxes[i], boxes[j]))
                        {
                            ++tally.overlaps;
                            tally.Report("overlap: " + where, key);
                        }
            }
    }

    void TestFonts(const nazg::LegendFonts& fonts, const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("fonts\n");

        Check(fonts.regular != nullptr && fonts.bold != nullptr, "Arimo Regular and Bold load");
        Check(std::fabs(fonts.sizePerEm - 2288.0f / 2048.0f) < 1e-4f, "Arimo's line is 1.117 em, from its own tables");

        // An em size is an em, whichever face a glyph comes from: Noto Sans Arabic's alef, drawn
        // from a face whose line is 2.1 em, stands as tall as Arimo's l, not half as tall.
        const nazg::InkExtent alef = measurer.Ink("ا", 100.0f, nazg::LegendWeight::Regular);
        const nazg::InkExtent l    = measurer.Ink("l", 100.0f, nazg::LegendWeight::Regular);
        const float           ratio = (alef.bottom - alef.top) / (l.bottom - l.top);
        std::printf("    alef / l: %.2f\n", ratio);
        Check(ratio > 0.8f && ratio < 1.25f, "a merged face's glyphs at the legend's em size");

        const nazg::InkExtent h = measurer.Ink("H", 100.0f, nazg::LegendWeight::Regular);
        std::printf("    H: %.1f to %.1f at 100 px\n", h.top, h.bottom);
        Check(std::fabs((h.bottom - h.top) - 68.8f) < 1.5f, "a capital is 0.688 em, as Arimo draws it");
    }

    void TestStandardKeys(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("standard keys on 1u\n");

        Tally tally;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
        {
            if (!row.ExistsIn(nazg::c_LatestQmkKeycodeVersion))
                continue;

            for (nazg::ModifierNames names : nazg::AllModifierNames())
                for (nazg::KeySide side : { nazg::KeySide::Left, nazg::KeySide::Right, nazg::KeySide::Neither })
                {
                    const KeycapLegend legend =
                        nazg::LegendFor(nazg::NamedKey{ row.name }, { nazg::UsHostLayout(), names, side });
                    if (legend.placement == nazg::PlacementClass::Command)
                        continue;   // their own legends come with step 4
                    Draw(legend, row.name, false, measurer, tally);
                }
        }

        std::printf("    %d draws: %d cut, %d leaving the face, %d overlaps\n", tally.draws, tally.cut, tally.outside,
                    tally.overlaps);
        Check(tally.draws > 1000, "every standard keycode, family, size, modifier name and side");
        Check(tally.cut == 0, "no standard legend is cut");
        Check(tally.outside == 0, "none leaves the face");
        Check(tally.overlaps == 0, "no two overlap");
    }

    void TestHostLayouts(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("host layouts on 1u\n");

        Tally tally;
        for (const nazg::HostLayout& layout : nazg::HostLayouts())
        {
            std::vector<std::string_view> keys(std::begin(c_CharacterKeys), std::end(c_CharacterKeys));
            for (const nazg::HostLegend& legend : layout.legends)
                if (std::find(keys.begin(), keys.end(), legend.key) == keys.end())
                    keys.push_back(legend.key);

            for (std::string_view key : keys)
            {
                const nazg::LegendContext context{ layout, nazg::ModifierNames::Windows, nazg::KeySide::Neither };
                const std::string         name = std::string(layout.id) + " " + std::string(key);

                // On its own, and with a hold in the corner -- the mockup's: a layer, and Ctrl.
                Draw(nazg::LegendFor(nazg::NamedKey{ key }, context), name, false, measurer, tally);
                Draw(nazg::LegendFor(nazg::LayerTapKey{ 1, key }, context), name + " LT", false, measurer, tally);
                Draw(nazg::LegendFor(nazg::ModTapKey{ nazg::Mod::LeftCtrl, key }, context), name + " MT", false,
                     measurer, tally);
            }
        }

        std::printf("    %d draws: %d cut, %d leaving the face, %d overlaps\n", tally.draws, tally.cut, tally.outside,
                    tally.overlaps);
        Check(tally.draws > 10000, "every layout's every position, alone and under a hold");
        Check(tally.outside == 0, "no character leaves the face");
        Check(tally.overlaps == 0, "no two overlap");
    }
}

int main(int argc, char** argv)
{
    ConfigureCrtReporting();

    if (argc < 2)
    {
        std::printf("usage: %s <fonts folder>\n", argv[0]);
        return 1;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.DisplaySize  = ImVec2(1280.0f, 720.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // glyphs baked on demand, as in the app

    std::vector<std::string> missing;
    const nazg::LegendFonts  fonts = nazg::LoadLegendFonts(*io.Fonts, std::string(argv[1]) + "/", missing);
    for (const std::string& path : missing)
        std::printf("missing: %s\n", path.c_str());
    Check(missing.empty(), "every font file is there");

    ImGui::NewFrame();
    {
        const nazg::ImGuiTextMeasurer measurer(fonts, nullptr);
        TestFonts(fonts, measurer);
        if (fonts.regular != nullptr && fonts.bold != nullptr)
        {
            TestStandardKeys(measurer);
            TestHostLayouts(measurer);
        }
    }
    ImGui::EndFrame();
    ImGui::DestroyContext();

    return TestResult();
}
