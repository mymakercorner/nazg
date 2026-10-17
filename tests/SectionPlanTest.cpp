// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins which sections a board gets, in which order and with which icon (ui/NazgSectionPlan.h):
// Nazg's own from what the load read, then the definition's custom menus; a custom menu's icon
// by its label, then by keyword, else a monogram.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgSectionPlan.h"

#include "TestSupport.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using nazg::Icon;
using nazg::Keyboard;
using nazg::PlannedSection;
using nazg::SectionGroup;
using nazg::SectionKind;

namespace
{
    std::vector<std::string> NamesOf(const std::vector<PlannedSection>& sections)
    {
        std::vector<std::string> names;
        for (const PlannedSection& section : sections)
            names.push_back(section.name);
        return names;
    }

    void TestBareBoard()
    {
        std::printf("a board with nothing but its keymap\n");

        const std::vector<PlannedSection> sections = nazg::PlanSections(Keyboard{});
        Check(sections.size() == 1 && sections[0].kind == SectionKind::Keymap && sections[0].icon == Icon::Keyboard,
              "Keymap alone -- the column is hidden");
    }

    // The Model F as Vial reports it: every feature compiled in, lighting declared.
    void TestVialBoard()
    {
        std::printf("a Vial board\n");

        Keyboard keyboard;
        keyboard.definition.layoutLabels       = { "Split Backspace" };
        keyboard.definition.lighting           = "qmk_backlight";
        keyboard.report.isVial                 = true;
        keyboard.report.macroCount             = 16;
        keyboard.report.tapDanceCount          = 8;
        keyboard.report.comboCount             = 8;
        keyboard.report.keyOverrideCount       = 4;
        keyboard.report.altRepeatKeyCount      = 4;
        keyboard.report.hasQmkSettings         = true;

        const std::vector<PlannedSection> sections = nazg::PlanSections(keyboard);
        Check(NamesOf(sections) == std::vector<std::string>{ "Keymap", "Layout", "Macros", "Lighting", "Tap Dance", "Combos",
                                                             "Key Overrides", "Alt Repeat Key", "QMK Settings" },
              "Keymap, Layout, Macros, Lighting, then Vial's features, in ui-design.md's order");
        Check(sections.size() == 9 && sections[4].icon == Icon::HandClick && sections[7].icon == Icon::Repeat,
              "with the icons Rico chose");

        bool allNazg = true;
        for (const PlannedSection& section : sections)
            allNazg = allNazg && section.group == SectionGroup::Protocol;
        Check(allNazg, "all in Nazg's group");

        keyboard.definition.lighting = "none";
        keyboard.report.comboCount   = 0;
        const std::vector<std::string> fewer = NamesOf(nazg::PlanSections(keyboard));
        Check(std::find(fewer.begin(), fewer.end(), "Lighting") == fewer.end(), "lighting \"none\" is no section");
        Check(std::find(fewer.begin(), fewer.end(), "Combos") == fewer.end(), "a feature with no entries is no section");
    }

    // A Leyden Jar board: Diagnostics in the Tools group, after everything else.
    void TestLeydenJarBoard()
    {
        std::printf("a Leyden Jar board\n");

        Keyboard keyboard;
        keyboard.report.isVial      = true;
        keyboard.report.macroCount  = 64;
        keyboard.report.isLeydenJar = true;

        const std::vector<PlannedSection> sections = nazg::PlanSections(keyboard);
        Check(NamesOf(sections) == std::vector<std::string>{ "Keymap", "Macros", "Diagnostics" }, "Diagnostics last");
        Check(sections.back().kind == SectionKind::Diagnostics && sections.back().group == SectionGroup::Tools &&
                  sections.back().icon == Icon::Activity,
              "in the Tools group, with Tabler's activity");

        keyboard.report.isLeydenJar = false;
        Check(NamesOf(nazg::PlanSections(keyboard)).size() == 2, "no Leyden Jar, no Diagnostics");
    }

    // NEVEREST 60: VIA's sections, then cipulot's eight menus in the definition's order.
    void TestViaBoardWithMenus()
    {
        std::printf("a VIA board with custom menus\n");

        Keyboard keyboard;
        keyboard.report.macroCount     = 16;
        keyboard.definition.menuIds    = { "qmk_rgb_matrix", "qmk_audio" };
        keyboard.definition.customMenus = {
            { "Switch Configuration", {}, {} },
            { "Tap Dance", { "{id_firmware_version} >= 5" }, {} },
            { "SOCD", {}, {} },
            { "Mystery", { "{id_mode} == 1" }, {} },
            { "DKS", { "{id_firmware_version} >= 6" }, {} },
        };
        keyboard.report.firmwareVersion = 5;
        keyboard.report.viaProtocol     = 12;

        const std::vector<PlannedSection> sections = nazg::PlanSections(keyboard);
        Check(NamesOf(sections) == std::vector<std::string>{ "Keymap", "Macros", "Lighting", "Audio", "Switch Configuration",
                                                             "Tap Dance", "SOCD", "Mystery" },
              "Nazg's first, VIA's built-in lighting and audio among them, then the menus in order");
        Check(sections.size() == 8 && sections[4].group == SectionGroup::Board && sections[3].group == SectionGroup::Protocol,
              "the menus in the Board group");
        Check(sections.size() == 8 && sections[4].icon == Icon::ArrowBarToDown && sections[5].icon == Icon::HandClick &&
                  sections[7].icon == Icon::None,
              "each menu's icon from its label; an unknown one gets a monogram");
        Check(sections.size() == 8 && sections[5].customMenu == 1 && sections[7].customMenu == 3,
              "each knows its menu in the definition");
        Check(sections.size() == 8 && sections[5].name == "Tap Dance" && sections[7].name == "Mystery",
              "a menu the firmware version allows is shown, as is one whose condition names another value -- the "
              "section judges that one; DKS, which wants version 6, is left out");

        keyboard.report.viaProtocol = 10;
        const std::vector<std::string> old = NamesOf(nazg::PlanSections(keyboard));
        Check(std::find(old.begin(), old.end(), "Audio") == old.end(), "no Audio before VIA protocol 11: no channel 4");
        keyboard.report.viaProtocol = 12;

        keyboard.report.isVial = true;
        const std::vector<std::string> vial = NamesOf(nazg::PlanSections(keyboard));
        Check(std::find(vial.begin(), vial.end(), "SOCD") == vial.end(), "no custom menu on Vial");
        Check(std::find(vial.begin(), vial.end(), "Audio") == vial.end(), "no Audio on Vial: vial-qmk has no channel 4");
    }

    void TestMenuIcons()
    {
        std::printf("custom menu icons\n");

        Check(nazg::IconOfMenu("Board System") == Icon::Settings, "a label in the table");
        Check(nazg::IconOfMenu("INDICATORS") == Icon::CircleDot, "case aside");
        Check(nazg::IconOfMenu("Advanced Features") == Icon::Sparkles, "a label no keyword finds");

        Check(nazg::IconOfMenu("Tap-dance timings") == Icon::HandClick, "keyword: tap dance, however joined");
        Check(nazg::IconOfMenu("Per-key RGB") == Icon::Bulb, "keyword: rgb");
        Check(nazg::IconOfMenu("LEDs") == Icon::Bulb, "keyword: a word starting with led");
        Check(nazg::IconOfMenu("Sledge") == Icon::None, "but not led inside a word");
        Check(nazg::IconOfMenu("DKS curves") == Icon::Gauge, "keyword: dks, as a word");
        Check(nazg::IconOfMenu("OLED Screen") == Icon::DeviceDesktop, "keyword: OLED is a display, not a word starting with led");
        Check(nazg::IconOfMenu("Encoder") == Icon::RotateClockwise, "keyword: encoder");
        Check(nazg::IconOfMenu("Firmware") == Icon::None, "no keyword: none");
    }

    void TestMonograms()
    {
        std::printf("monograms\n");

        Check(nazg::MonogramOf("Advanced Features") == "AF", "two words: their initials");
        Check(nazg::MonogramOf("state of the art") == "SA", "short words left out, initials capitalised");
        Check(nazg::MonogramOf("Knob") == "Kn", "one word: its first two letters");
        Check(nazg::MonogramOf("Écran tactile") == "ÉT", "a UTF-8 letter stays whole");
        Check(nazg::MonogramOf("") == "?" && nazg::MonogramOf("--") == "?", "no letters: ?");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestBareBoard();
    TestVialBoard();
    TestViaBoardWithMenus();
    TestLeydenJarBoard();
    TestMenuIcons();
    TestMonograms();

    return TestResult();
}
