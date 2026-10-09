// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins what the keycode picker offers a board (ui/NazgKeycodeCatalogue.h): every keycode the
// board's version can store is in a tab, but the few families left to search and the expression
// box; a group goes when the board says it cannot work; nothing the board would refuse is offered.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgKeycodeCatalogue.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "TestSupport.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <variant>

using nazg::CatalogueTab;
using nazg::Keyboard;
using nazg::Keycode;
using nazg::QmkKeycodeVersion;

namespace
{
    Keyboard MakeKeyboard(QmkKeycodeVersion version, uint8_t layers)
    {
        Keyboard keyboard;
        keyboard.keymap         = nazg::Keymap(layers, 1, 1);
        keyboard.keycodeVersion = version;
        return keyboard;
    }

    const CatalogueTab* FindTab(const std::vector<CatalogueTab>& tabs, std::string_view id)
    {
        const auto tab = std::find_if(tabs.begin(), tabs.end(), [&](const CatalogueTab& t) { return t.id == id; });
        return tab == tabs.end() ? nullptr : &*tab;
    }

    std::set<std::string> NamesIn(const std::vector<CatalogueTab>& tabs)
    {
        std::set<std::string> names;
        for (const CatalogueTab& tab : tabs)
            for (const nazg::CatalogueGroup& group : tab.groups)
                for (const Keycode& keycode : group.keycodes)
                    names.insert(nazg::FormatKeycode(keycode));
        return names;
    }

    bool Offers(const std::vector<CatalogueTab>& tabs, std::string_view name)
    {
        return NamesIn(tabs).count(std::string(name)) > 0;
    }

    // Not in a tab, on purpose: MIDI beyond its basic set and steno's extra chord keys -- by the
    // expression box -- LED Matrix, which no definition can declare, the RGB_M modes on new
    // firmware, where they are dead, and macros past the board's count (16 here).
    bool LeftOut(std::string_view name)
    {
        if (name.substr(0, 3) == "MC_")
            return std::stoi(std::string(name.substr(3))) >= 16;
        return name.substr(0, 3) == "MI_" || name.substr(0, 3) == "ST_" || name.substr(0, 3) == "LM_" ||
               name.substr(0, 6) == "RGB_M_";
    }

    void TestEveryKeycodeHasATab()
    {
        std::printf("every keycode has a tab\n");

        Keyboard keyboard = MakeKeyboard(QmkKeycodeVersion::V0_0_9, 4);
        keyboard.report.viaProtocol    = 13;   // new firmware: UG_ and RM_ apart
        keyboard.report.macroCount     = 16;
        keyboard.definition.lighting   = "qmk_backlight_rgblight";
        keyboard.definition.menuIds    = { "qmk_rgb_matrix" };

        const std::vector<CatalogueTab> tabs  = nazg::BuildKeycodeCatalogue(keyboard, {});
        const std::set<std::string>     names = NamesIn(tabs);

        std::string missing;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
            if (row.ExistsIn(keyboard.keycodeVersion) && names.count(nazg::FormatKeycode(
                                                             nazg::DecodeQmkKeycode(row.value, keyboard.keycodeVersion))) == 0 &&
                !LeftOut(row.name))
                missing += std::string(" ") + row.name;

        Check(missing.empty(), ("every stored keycode in a tab; missing:" + missing).c_str());
    }

    void TestNothingTheBoardRefuses()
    {
        std::printf("nothing the board refuses\n");

        for (QmkKeycodeVersion version : { QmkKeycodeVersion::Legacy, QmkKeycodeVersion::V0_0_1, QmkKeycodeVersion::V0_0_7 })
        {
            Keyboard keyboard = MakeKeyboard(version, 4);
            keyboard.report.macroCount = 16;
            bool all = true;
            for (const CatalogueTab& tab : nazg::BuildKeycodeCatalogue(keyboard, {}))
                for (const nazg::CatalogueGroup& group : tab.groups)
                    for (const Keycode& keycode : group.keycodes)
                        all = all && nazg::EncodeQmkKeycode(keycode, version).has_value();
            Check(all, ("every offered keycode encodes on " + std::string(nazg::QmkKeycodeVersionName(version))).c_str());
        }

        Keyboard legacy = MakeKeyboard(QmkKeycodeVersion::Legacy, 4);
        Check(!Offers(nazg::BuildKeycodeCatalogue(legacy, {}), "PDF(0)"), "PDF from 0.0.6 only");
    }

    void TestWhatTheBoardSays()
    {
        std::printf("what the board says\n");

        Keyboard vial = MakeKeyboard(QmkKeycodeVersion::V0_0_7, 3);
        vial.report.isVial        = true;
        vial.report.vialProtocol  = 6;
        vial.report.viaProtocol   = 9;
        vial.report.macroCount    = 16;
        vial.report.tapDanceCount = 8;
        vial.report.capsWord      = true;

        const std::vector<CatalogueTab> tabs = nazg::BuildKeycodeCatalogue(vial, {});
        Check(FindTab(tabs, "lighting") == nullptr, "no lighting declared: no Lighting tab");
        Check(Offers(tabs, "CW_TOGG") && !Offers(tabs, "QK_LLCK"), "Caps Word reported, Layer Lock not");
        Check(!Offers(tabs, "QK_REP"), "no alt repeat entries: no Repeat keys");
        Check(Offers(tabs, "MC_15") && !Offers(tabs, "MC_16"), "the board's macro count");
        Check(Offers(tabs, "TD(7)") && !Offers(tabs, "TD(8)"), "and its tap dance count");
        Check(Offers(tabs, "MO(2)") && !Offers(tabs, "MO(3)"), "a layer key per layer the board has");

        const CatalogueTab* layers = FindTab(tabs, "layers");
        Check(layers != nullptr && layers->groups.front().title == "Hold", "the operations, Hold first");

        Keyboard via = MakeKeyboard(QmkKeycodeVersion::V0_0_8, 4);
        via.report.viaProtocol = 12;
        const std::vector<CatalogueTab> viaTabs = nazg::BuildKeycodeCatalogue(via, {});
        Check(Offers(viaTabs, "CW_TOGG") && Offers(viaTabs, "QK_LLCK"), "VIA says nothing: both offered");
        Check(!Offers(viaTabs, "TD(0)"), "and no tap dances, VIA having none");
        Check(FindTab(viaTabs, "custom") != nullptr && Offers(viaTabs, "QK_KB_31"), "Custom on every board, numbered");
    }

    void TestLightingByPolicy()
    {
        std::printf("lighting by the policy\n");

        Keyboard board = MakeKeyboard(QmkKeycodeVersion::V0_0_8, 2);
        board.definition.lighting = "qmk_rgblight";
        board.definition.menuIds  = { "qmk_rgb_matrix" };

        board.report.viaProtocol = 12;   // unknown
        std::vector<CatalogueTab> tabs = nazg::BuildKeycodeCatalogue(board, {});
        Check(Offers(tabs, "UG_TOGG") && !Offers(tabs, "RM_TOGG") && !Offers(tabs, "RGB_M_P"),
              "unknown: the one set, no RM_, no modes");

        board.report.viaProtocol = 13;   // new
        tabs = nazg::BuildKeycodeCatalogue(board, {});
        Check(Offers(tabs, "UG_TOGG") && Offers(tabs, "RM_TOGG") && !Offers(tabs, "RGB_M_P"), "new: a set per system");

        board.report.viaProtocol = 11;   // old
        board.keycodeVersion     = QmkKeycodeVersion::V0_0_1;
        tabs = nazg::BuildKeycodeCatalogue(board, {});
        Check(Offers(tabs, "UG_TOGG") && !Offers(tabs, "RM_TOGG") && Offers(tabs, "RGB_M_P"), "old: one set and the modes");

        // Advanced tools: every set and mode for the systems declared, whatever the state.
        board.report.viaProtocol = 12;   // unknown
        board.keycodeVersion     = QmkKeycodeVersion::V0_0_8;
        tabs = nazg::BuildKeycodeCatalogue(board, {}, true);
        Check(Offers(tabs, "UG_TOGG") && Offers(tabs, "RM_TOGG") && Offers(tabs, "RGB_M_P") && Offers(tabs, "RGB_M_T"),
              "every lighting key: both sets and every mode");
        Check(!Offers(tabs, "BL_TOGG") && !Offers(tabs, "LM_TOGG"), "but only for the systems declared");
    }

    void TestCustomAndSearch()
    {
        std::printf("custom keys and search\n");

        Keyboard board = MakeKeyboard(QmkKeycodeVersion::V0_0_8, 2);
        board.definition.customKeycodes = { { "Mission Control", "Mission Control in macOS", "MCtrl" }, { "Siri", "", "" } };
        const std::vector<CatalogueTab> tabs   = nazg::BuildKeycodeCatalogue(board, {});
        const CatalogueTab*             custom = FindTab(tabs, "custom");
        Check(custom != nullptr && custom->groups.front().keycodes.size() == 2 && custom->groups.front().title == "This board's own keys",
              "a definition's names: only those keys, by name");

        const nazg::LegendContext context{ nazg::UsHostLayout() };
        const std::string         escape = nazg::SearchTextOf(nazg::NamedKey{ "KC_ESC" }, QmkKeycodeVersion::V0_0_8, context);
        Check(nazg::MatchesSearch(escape, "ESC") && nazg::MatchesSearch(escape, " kc_esc "), "QMK's name, any case");
        const std::string play = nazg::SearchTextOf(nazg::NamedKey{ "KC_MPLY" }, QmkKeycodeVersion::V0_0_8, context);
        Check(nazg::MatchesSearch(play, "play/pause") && nazg::MatchesSearch(play, "media"), "its label and its words");
        Check(!nazg::MatchesSearch(play, "volume"), "and nothing else");
    }
}

namespace
{
    // The key line keeps what it set: a hold, or the modifiers a key is sent with.
    void TestCompose()
    {
        std::printf("compose with the key\n");

        using nazg::ComposeWithKey;
        constexpr QmkKeycodeVersion version = QmkKeycodeVersion::V0_0_8;
        const Keycode               space   = nazg::NamedKey{ "KC_SPC" };

        Check(nazg::FormatKeycode(ComposeWithKey(space, nazg::LayerTapKey{ 1, "KC_A" }, version)) == "LT(1,KC_SPC)",
              "a hold is kept");
        Check(nazg::FormatKeycode(ComposeWithKey(space, nazg::ModTapKey{ nazg::Mod::LeftCtrl, "KC_ESC" }, version)) ==
                  "MT(MOD_LCTL,KC_SPC)",
              "and modifiers when held");
        Check(nazg::FormatKeycode(ComposeWithKey(nazg::NamedKey{ "KC_2" }, nazg::ModifiedKey{ nazg::Mod::LeftShift, "KC_1" },
                                                 version)) == "LSFT(KC_2)",
              "and the modifiers sent with the key");
        Check(ComposeWithKey(nazg::NamedKey{ "UG_TOGG" }, nazg::LayerTapKey{ 1, "KC_A" }, version) == Keycode{ nazg::NamedKey{ "UG_TOGG" } },
              "a keycode past the basic byte cannot be a tap: written as picked");
        Check(nazg::FormatKeycode(ComposeWithKey(nazg::NamedKey{ "KC_MPLY" }, nazg::LayerTapKey{ 1, "KC_A" }, version)) ==
                  "LT(1,KC_MPLY)",
              "a media key can, being in it");
        Check(ComposeWithKey(nazg::NamedKey{ "KC_TRNS" }, nazg::LayerTapKey{ 1, "KC_A" }, version) == Keycode{ nazg::NamedKey{ "KC_TRNS" } },
              "Transparent replaces the key");
        Check(ComposeWithKey(space, nazg::NamedKey{ "KC_A" }, version) == space, "a plain key: as picked");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestCompose();

    TestEveryKeycodeHasATab();
    TestNothingTheBoardRefuses();
    TestWhatTheBoardSays();
    TestLightingByPolicy();
    TestCustomAndSearch();

    return TestResult();
}
