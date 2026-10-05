// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The legends in the real font: Arimo and its Noto faces, loaded through ImGui's core -- no
// SDL, no GPU -- exactly as the app loads them, then every named keycode, the parameterised ones
// built from their parts, and every host layout's characters under the longest holds, laid out
// on a 1u key, in both families, with every modifier name and side, from the smallest board to
// the largest. The mockup's checks (ui-design.md, "Legends -- the plan" and short-forms.md,
// "Checked"): no legend is cut, none leaves the face, no two overlap, and every character has a
// glyph. So a new QMK keycode or host layout that does not fit fails the build.
//
// Takes the fonts folder as its argument. Registered with CTest:
//   ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "imgui.h"
#include "imgui_internal.h"   // ImTextCharFromUtf8

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

    void Summarise(const Tally& tally)
    {
        std::printf("    %d draws: %d cut, %d leaving the face, %d overlaps\n", tally.draws, tally.cut, tally.outside,
                    tally.overlaps);
    }

    // Every character a command or a standard key prints is in a bundled font -- "↔" in the Magic
    // swaps' headers, the true minus -- or it would draw as a box.
    void TestGlyphs(const nazg::LegendFonts& fonts)
    {
        std::printf("glyphs\n");

        std::set<unsigned int> missing;
        const auto             check = [&](const std::string& text)
        {
            for (const char* at = text.c_str(); *at != '\0';)
            {
                unsigned int codepoint = 0;
                at += ImTextCharFromUtf8(&codepoint, at, nullptr);
                for (ImFont* font : { fonts.regular, fonts.bold })
                    if (!font->IsGlyphInFont(static_cast<ImWchar>(codepoint)))
                        missing.insert(codepoint);
            }
        };

        int texts = 0;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
            for (nazg::ModifierNames names : nazg::AllModifierNames())
            {
                const KeycapLegend legend = nazg::LegendFor(nazg::NamedKey{ row.name }, { nazg::UsHostLayout(), names });
                for (const nazg::Words* words : { &legend.cylindrical, &legend.spherical, &legend.header.words, &legend.hold.words })
                {
                    check(words->full);
                    check(words->shortForm);
                    texts += 2;
                }
            }

        // The modifiers' words and short forms, ⌘ among them.
        for (nazg::ModifierNames names : nazg::AllModifierNames())
            for (uint8_t mods = 1; mods < 16; ++mods)
            {
                const KeycapLegend legend = nazg::LegendFor(nazg::OneShotModKey{ mods }, { nazg::UsHostLayout(), names });
                check(legend.cylindrical.full);
                check(legend.cylindrical.shortForm);
                texts += 2;
            }

        for (unsigned int codepoint : missing)
            std::printf("    no glyph for U+%04X\n", codepoint);
        Check(texts > 1000 && missing.empty(), "every character of every named key's words has a glyph");
    }

    // Lighting on a board with one system, and with each pair: every header word.
    constexpr uint8_t c_Lightings[] = {
        nazg::LightingSystem::Underglow,
        nazg::LightingSystem::Backlight | nazg::LightingSystem::Underglow,
        nazg::LightingSystem::Underglow | nazg::LightingSystem::RgbMatrix | nazg::LightingSystem::LedMatrix,
    };

    // Every named keycode: the standard keys and the commands, each in its words.
    void TestNamedKeys(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("named keys on 1u\n");

        Tally tally;
        int   commands = 0;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
        {
            if (!row.ExistsIn(nazg::c_LatestQmkKeycodeVersion))
                continue;

            for (nazg::ModifierNames names : nazg::AllModifierNames())
                for (nazg::KeySide side : { nazg::KeySide::Left, nazg::KeySide::Right, nazg::KeySide::Neither })
                    for (uint8_t lighting : c_Lightings)
                    {
                        const KeycapLegend legend =
                            nazg::LegendFor(nazg::NamedKey{ row.name }, { nazg::UsHostLayout(), names, side, lighting });
                        commands += legend.placement == nazg::PlacementClass::Command;
                        Draw(legend, row.name, false, measurer, tally);
                    }
        }

        Summarise(tally);
        Check(tally.draws > 10000 && commands > 1000,
              "every named keycode, commands included, every family, size, modifier name, side and lighting");
        Check(tally.cut == 0, "no legend is cut: every short form fits");
        Check(tally.outside == 0, "none leaves the face");
        Check(tally.overlaps == 0, "no two overlap");
    }

    // The parameterised keycodes, built from their parts: layers up to 31, every modifier set.
    void TestParameterised(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("parameterised keys on 1u\n");

        std::vector<uint8_t> modSets;
        for (uint8_t mods = 1; mods < 16; ++mods)
        {
            modSets.push_back(mods);
            modSets.push_back(static_cast<uint8_t>(mods << 4));
        }

        Tally tally;
        for (nazg::ModifierNames names : nazg::AllModifierNames())
        {
            const nazg::LegendContext context{ nazg::UsHostLayout(), names, nazg::KeySide::Neither };
            const auto                draw = [&](const nazg::Keycode& keycode)
            { Draw(nazg::LegendFor(keycode, context), nazg::FormatKeycode(keycode), false, measurer, tally); };

            for (uint8_t layer = 0; layer < 32; ++layer)
            {
                for (uint8_t op = 0; op <= static_cast<uint8_t>(nazg::LayerOp::TapToggle); ++op)
                    draw(nazg::LayerKey{ static_cast<nazg::LayerOp>(op), layer });
                for (uint8_t mods : modSets)
                    draw(nazg::LayerModKey{ layer, mods });
            }
            for (uint8_t mods : modSets)
            {
                draw(nazg::OneShotModKey{ mods });
                for (const char* key : { "KC_C", "KC_1", "KC_W", "KC_ENT", "KC_BSPC", "KC_PGDN", "KC_F12", "KC_UP" })
                    draw(nazg::ModifiedKey{ mods, key });
            }
            for (int index = 0; index < 256; ++index)
                draw(nazg::TapDanceKey{ static_cast<uint8_t>(index) });
            for (int index = 0; index < 128; ++index)
                draw(nazg::MacroKey{ static_cast<uint8_t>(index) });
            draw(nazg::UnknownKey{ 0x7E40 });
        }

        Summarise(tally);
        Check(tally.draws > 10000, "layer keys, one-shot and modified keys with every modifier set, tap dance, macros");
        Check(tally.cut == 0, "no legend is cut");
        Check(tally.outside == 0, "none leaves the face");
        Check(tally.overlaps == 0, "no two overlap");
    }

    // The keycode picker's tiles (ui-design.md, "The tiles"): every keycode the picker can offer,
    // with its header and with the header its group's title says left out, at every display
    // scale -- none cut, none leaving the tile, none overlapping. A tile's text room is a 1u
    // keycap's at the smallest board, so what fits there fits here.
    void TestTiles(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("keycode picker tiles\n");

        Tally      tally;
        const auto draw = [&](KeycapLegend legend, const std::string& name)
        {
            for (bool dropped : { false, true })
            {
                if (dropped)
                {
                    if (legend.header.IsEmpty() && legend.hold.IsEmpty())
                        continue;
                    legend.header = {};
                    legend.hold   = {};
                }
                for (float display : { 1.0f, 1.25f, 1.5f, 2.0f })
                {
                    const float   scale = display * nazg::c_TileZoom;
                    const FaceBox face{ 0.0f, 0.0f, nazg::c_TileWidth * scale, nazg::c_TileHeight * scale };
                    const KeycapPrimitives primitives = nazg::LayOutTile(legend, face, scale, measurer);
                    const std::vector<Box> boxes      = InkBoxes(primitives, measurer);
                    const std::string      where      = name + " x" + std::to_string(scale) + ": " + Describe(primitives);
                    ++tally.draws;

                    for (const nazg::PlacedText& text : primitives.texts)
                        if (text.cut)
                        {
                            ++tally.cut;
                            tally.Report("cut: " + where, name);
                        }
                    for (const Box& box : boxes)
                        if (!Inside(box, face))
                        {
                            ++tally.outside;
                            tally.Report("leaves the tile: " + where, name);
                            break;
                        }
                    for (size_t i = 0; i < boxes.size(); ++i)
                        for (size_t j = i + 1; j < boxes.size(); ++j)
                            if (Overlap(boxes[i], boxes[j]))
                            {
                                ++tally.overlaps;
                                tally.Report("overlap: " + where, name);
                            }
                }
            }
        };

        for (nazg::ModifierNames names : nazg::AllModifierNames())
            for (uint8_t lighting : c_Lightings)
            {
                const nazg::LegendContext context{ nazg::UsHostLayout(), names, nazg::KeySide::Neither, lighting };
                for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
                    if (row.ExistsIn(nazg::c_LatestQmkKeycodeVersion))
                        draw(nazg::LegendFor(nazg::NamedKey{ row.name }, context), row.name);
                for (uint8_t layer = 0; layer < 32; ++layer)
                    for (uint8_t op = 0; op <= static_cast<uint8_t>(nazg::LayerOp::TapToggle); ++op)
                        draw(nazg::LegendFor(nazg::LayerKey{ static_cast<nazg::LayerOp>(op), layer }, context), "layer key");
                for (uint8_t mods = 1; mods < 16; ++mods)
                    for (uint8_t sided : { mods, static_cast<uint8_t>(mods << 4) })
                        draw(nazg::LegendFor(nazg::OneShotModKey{ sided }, context), "OSM");
                for (int index = 0; index < 256; ++index)
                    draw(nazg::LegendFor(nazg::TapDanceKey{ static_cast<uint8_t>(index) }, context), "TD");
            }

        Summarise(tally);
        Check(tally.draws > 10000, "every keycode the picker offers, with and without its header, every scale");
        Check(tally.cut == 0, "no tile's words are cut");
        Check(tally.outside == 0, "none leaves its tile");
        Check(tally.overlaps == 0, "no two overlap");
    }

    // Tap-holds whose tap is a command: two headers stacked over the main legend -- "L1 / Media /
    // Play" -- for every basic keycode that is a command, under a layer and the longest holds.
    void TestCommandTaps(const nazg::ImGuiTextMeasurer& measurer)
    {
        std::printf("command taps under a hold on 1u\n");

        Tally tally;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
        {
            if (!row.ExistsIn(nazg::c_LatestQmkKeycodeVersion) || row.value > 0xFF)
                continue;

            for (nazg::ModifierNames names : nazg::AllModifierNames())
            {
                const nazg::LegendContext context{ nazg::UsHostLayout(), names, nazg::KeySide::Neither };
                if (nazg::LegendFor(nazg::NamedKey{ row.name }, context).placement != nazg::PlacementClass::Command)
                    continue;

                for (const nazg::Keycode& keycode :
                     { nazg::Keycode{ nazg::LayerTapKey{ 15, row.name } },
                       nazg::Keycode{ nazg::ModTapKey{ nazg::Mod::RightAlt, row.name } },
                       nazg::Keycode{ nazg::ModTapKey{ nazg::Mod::LeftCtrl | nazg::Mod::LeftShift, row.name } } })
                    Draw(nazg::LegendFor(keycode, context), nazg::FormatKeycode(keycode), false, measurer, tally);
            }
        }

        Summarise(tally);
        Check(tally.draws > 500, "every basic command under a layer, Alt Gr and two modifiers");
        Check(tally.cut == 0, "no legend is cut");
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

                // On its own, and with a hold in the corner -- the mockup's: a layer, and Ctrl -- and
                // the longest holds, which a letter goes below (Alt Gr over W on 1u).
                Draw(nazg::LegendFor(nazg::NamedKey{ key }, context), name, false, measurer, tally);
                Draw(nazg::LegendFor(nazg::LayerTapKey{ 1, key }, context), name + " LT", false, measurer, tally);
                for (uint8_t mods : { nazg::Mod::LeftCtrl, nazg::Mod::LeftShift, nazg::Mod::RightAlt, nazg::Mod::LeftGui,
                                      static_cast<uint8_t>(nazg::Mod::LeftCtrl | nazg::Mod::LeftShift) })
                    Draw(nazg::LegendFor(nazg::ModTapKey{ mods, key }, context),
                         name + " MT " + std::to_string(mods), false, measurer, tally);
                for (nazg::ModifierNames names : { nazg::ModifierNames::Mac, nazg::ModifierNames::Linux })
                    for (uint8_t mods : { nazg::Mod::LeftAlt, nazg::Mod::LeftGui })
                        Draw(nazg::LegendFor(nazg::ModTapKey{ mods, key }, { layout, names, nazg::KeySide::Neither }),
                             name + " MT " + std::string(nazg::IdOf(names)) + std::to_string(mods), false, measurer,
                             tally);
            }
        }

        Summarise(tally);
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
            TestGlyphs(fonts);
            TestNamedKeys(measurer);
            TestParameterised(measurer);
            TestCommandTaps(measurer);
            TestHostLayouts(measurer);
            TestTiles(measurer);
        }
    }
    ImGui::EndFrame();
    ImGui::DestroyContext();

    return TestResult();
}
