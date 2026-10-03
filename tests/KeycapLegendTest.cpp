// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Keycap legends: what a key says -- a Keycode seen through a host layout, the modifier names
// and the key's side of the board. Where it goes is KeycapLayoutTest's; these are the words
// and the rules of ui-design.md, "Legends -- the plan" and after: positions labelled by what
// they type on the host, Shift pairs, the AltGr character, Bépo's fourth level, modifiers by
// the OS and by their side, the legend set's words and placements.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgKeycapLegend.h"

#include "TestSupport.h"
#include "adapters/qmk/NazgQmkKeycodes.h"

#include <set>
#include <string>

using nazg::ArrowDirection;
using nazg::HostLayout;
using nazg::KeycapLegend;
using nazg::KeySide;
using nazg::LegendContext;
using nazg::LegendFor;
using nazg::ModifierNames;
using nazg::Placement;
using nazg::PlacementClass;
using nazg::UsHostLayout;

namespace Mod = nazg::Mod;

namespace
{
    KeycapLegend Of(const nazg::Keycode& keycode, const HostLayout& layout = UsHostLayout(),
                    ModifierNames names = ModifierNames::Windows, KeySide side = KeySide::Neither)
    {
        return LegendFor(keycode, LegendContext{ layout, names, side });
    }

    bool Pair(const KeycapLegend& legend, const char* plain, const char* shifted)
    {
        return legend.placement == PlacementClass::Character && legend.plain == plain && legend.shifted == shifted;
    }

    bool Says(const KeycapLegend& legend, const char* full, const char* shortForm = "")
    {
        return legend.cylindrical.full == full && legend.cylindrical.shortForm == shortForm;
    }

    // The host layout table is committed data produced by hand, so its shape is checked
    // the way the keycode table's is.
    void TestLayoutTable()
    {
        std::printf("host layout table\n");

        const auto layouts = nazg::HostLayouts();
        Check(layouts.size() == 69, "69 layouts: QMK's 72 minus plover, plover_dvorak and nordic");

        bool sorted     = true;
        bool nonEmpty   = true;
        bool uniqueKeys = true;
        bool basicKeys  = true;
        for (size_t i = 0; i < layouts.size(); ++i)
        {
            if (i > 0 && !(layouts[i - 1].id < layouts[i].id))
                sorted = false;
            if (layouts[i].legends.empty() || layouts[i].name.empty())
                nonEmpty = false;

            std::set<std::string_view> keys;
            for (const nazg::HostLegend& legend : layouts[i].legends)
            {
                uniqueKeys &= keys.insert(legend.key).second;

                // Every position must be a basic keycode, or LegendFor would never ask.
                const nazg::QmkKeycode* keycode =
                    nazg::FindQmkKeycodeByName(legend.key, nazg::c_LatestQmkKeycodeVersion);
                basicKeys &= keycode != nullptr && keycode->value <= 0xFF;
            }
        }
        Check(sorted, "sorted by id, so a list of them reads alphabetically");
        Check(nonEmpty, "every layout has a name and legends");
        Check(uniqueKeys, "no position appears twice in a layout");
        Check(basicKeys, "every position is a basic keycode");

        Check(nazg::FindHostLayout("french") != nullptr && nazg::FindHostLayout("french")->name == "French",
              "a layout is found by its QMK id");
        Check(nazg::FindHostLayout("french_mac_iso")->name == "French (Mac ISO)", "variants read naturally");
        Check(nazg::FindHostLayout("plover") == nullptr, "steno layouts are not host layouts");
        Check(nazg::FindHostLayout("klingon") == nullptr, "an unknown id finds nothing");
        Check(UsHostLayout().id == "us", "US is the default");

        nazg::LegendSettings settings;
        settings.hostLayout = "klingon";
        Check(settings.Layout().id == "us", "a setting this build does not know draws as US");
    }

    void TestCharacters()
    {
        std::printf("character keys\n");

        Check(Pair(Of(nazg::NamedKey{ "KC_A" }), "A", ""), "a letter is its capital, with no Shift legend");
        Check(Pair(Of(nazg::NamedKey{ "KC_1" }), "1", "!"), "a symbol key carries its Shift character too");
        Check(Pair(Of(nazg::NamedKey{ "KC_BSLS" }), "\\", "|"), "backslash and bar");
        Check(Pair(Of(nazg::NamedKey{ "KC_NUBS" }), "\\", "|"), "the extra ISO key types the same pair on US");
        Check(Of(nazg::NamedKey{ "KC_1" }).altgr.empty(), "plain US has no AltGr level");
    }

    // AZERTY, which is where this started: positions are QWERTY's, legends are not.
    void TestFrench()
    {
        std::printf("French AZERTY\n");

        const HostLayout& french = *nazg::FindHostLayout("french");

        Check(Pair(Of(nazg::NamedKey{ "KC_Q" }, french), "A", ""), "KC_Q types A");
        Check(Pair(Of(nazg::NamedKey{ "KC_SCLN" }, french), "M", ""), "KC_SCLN types M");
        Check(Pair(Of(nazg::NamedKey{ "KC_1" }, french), "&", "1"), "the number row: & with 1 on Shift");
        Check(Pair(Of(nazg::NamedKey{ "KC_2" }, french), "é", "2"), "é, in UTF-8");
        Check(Pair(Of(nazg::NamedKey{ "KC_LBRC" }, french), "^", "¨"), "a dead key shows its accent, not a tag");
        Check(Pair(Of(nazg::NamedKey{ "KC_NUHS" }, french), "*", "µ"), "the ISO hash key");
        Check(Pair(Of(nazg::NamedKey{ "KC_NUBS" }, french), "<", ">"), "the extra ISO key");
        Check(Says(Of(nazg::NamedKey{ "KC_ENT" }, french), "Enter"), "layout-independent keys are unchanged");
        Check(Pair(Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }, french), "1", ""), "LSFT(KC_1) types 1 on AZERTY");

        // The AltGr character, printed always where the layout has one (ui-design.md, "Host
        // layouts in the mockup").
        Check(Of(nazg::NamedKey{ "KC_2" }, french).altgr == "~", "AltGr+é is ~");
        Check(Of(nazg::NamedKey{ "KC_E" }, french).altgr == "€", "AltGr+E is the euro");
        Check(Of(nazg::NamedKey{ "KC_0" }, french).altgr == "@", "AltGr+à is @");
        Check(Of(nazg::NamedKey{ "KC_4" }, *nazg::FindHostLayout("uk")).altgr == "€", "UK: AltGr+4 is the euro");
    }

    // Shift+AltGr is printed on Bépo only, where the level is part of the layout.
    void TestFourthLevel()
    {
        std::printf("fourth level\n");

        const HostLayout& bepo = *nazg::FindHostLayout("bepo");
        Check(bepo.PrintsFourthLevel(), "Bépo prints its fourth level");
        Check(!nazg::FindHostLayout("us_international")->PrintsFourthLevel(), "other layouts print three levels");

        Check(Of(nazg::NamedKey{ "KC_4" }, bepo).shiftAltgr == "≤", "Bépo's ( key: ≤ on the fourth level");
        Check(Of(nazg::NamedKey{ "KC_4" }, *nazg::FindHostLayout("us_international")).shiftAltgr.empty(),
              "US International has a fourth level in the table, not on its keycaps");

        // A combining accent alone sits on a dotted circle, as Unicode's charts show one.
        Check(Of(nazg::NamedKey{ "KC_G" }, bepo).shiftAltgr == "◌̛", "Bépo's horn on a dotted circle");

        // Apple's logo is in no bundled font: left off.
        const nazg::HostLegend apple[] = { { "KC_K", "k", "K", "", "" } };
        const HostLayout       mac{ "bepo", "Test", apple };
        const KeycapLegend     k = Of(nazg::NamedKey{ "KC_K" }, mac);
        Check(k.plain == "k" && k.altgr.empty() && k.shiftAltgr.empty(), "the Apple logo is not printed");
    }

    // The legend set: words, short forms, placement classes and their exceptions.
    void TestLegendSet()
    {
        std::printf("legend set\n");

        const KeycapLegend backspace = Of(nazg::NamedKey{ "KC_BSPC" });
        Check(backspace.placement == PlacementClass::Modifier && Says(backspace, "Backspace", "Bksp"),
              "Backspace, short form Bksp");
        Check(backspace.spherical.full == "BACKSPACE" && backspace.spherical.shortForm == "BKSP",
              "spherical words are capitals by default");

        const KeycapLegend print = Of(nazg::NamedKey{ "KC_PSCR" });
        Check(print.spherical.full == "PRINT" && print.spherical.shortForm == "PRT", "SA's own words where they differ");
        Check(Of(nazg::NamedKey{ "KC_NUM" }).spherical.full == "NUM LOCK", "NUM LOCK whole, as Rico prefers");

        Check(Of(nazg::NamedKey{ "KC_ESC" }).place == Placement::MiddleLeft, "Esc sits with the function row");
        Check(Of(nazg::NamedKey{ "KC_F5" }).placement == PlacementClass::FunctionRow, "an F-key is the function row");
        Check(Of(nazg::NamedKey{ "KC_F24" }).placement == PlacementClass::FunctionRow, "F24 too");
        Check(Of(nazg::NamedKey{ "KC_FIND" }).placement == PlacementClass::Modifier, "Find is no F-key");
        Check(Of(nazg::NamedKey{ "KC_SPC" }).placement == PlacementClass::Blank, "the space bar is blank");
        Check(Of(nazg::NamedKey{ "KC_NO" }) == KeycapLegend{}, "KC_NO is a blank key");
        Check(Says(Of(nazg::NamedKey{ "KC_APP" }), "Menu"), "the App key says Menu");
        Check(Says(Of(nazg::NamedKey{ "KC_INT1" }), "Int 1"), "INT 1 in mixed case, where the layout prints none");
        Check(Of(nazg::NamedKey{ "KC_INT1" }, *nazg::FindHostLayout("japanese")).placement == PlacementClass::Character,
              "and a character where it does");
        Check(Says(Of(nazg::NamedKey{ "KC_LNG3" }), "Lang 3"), "LANG 3 in mixed case");

        const KeycapLegend up = Of(nazg::NamedKey{ "KC_UP" });
        Check(up.placement == PlacementClass::Arrow && up.arrow == ArrowDirection::Up, "arrows are drawn");

        const KeycapLegend divide = Of(nazg::NamedKey{ "KC_PSLS" });
        Check(divide.placement == PlacementClass::Numpad && divide.cylindrical.full == "÷" &&
                  divide.spherical.full == "/" && divide.place == Placement::MiddleLeft,
              "divide: ÷ as GMK prints it, / as SA does, at the left");
        Check(Of(nazg::NamedKey{ "KC_PMNS" }).cylindrical.full == "−", "minus is the true minus sign");
        Check(Of(nazg::NamedKey{ "KC_PPLS" }).place == Placement::Centre, "the tall + is centred");
        Check(Of(nazg::NamedKey{ "KC_PENT" }).place == Placement::Centre, "so is the numpad's Enter");

        Check(Of(nazg::NamedKey{ "KC_P0" }).second == "Ins", "0 is Insert with Num Lock off");
        Check(Of(nazg::NamedKey{ "KC_P2" }).secondArrow == ArrowDirection::Down, "2 is down");
        Check(Of(nazg::NamedKey{ "KC_P5" }).second.empty() && Of(nazg::NamedKey{ "KC_P5" }).secondArrow == ArrowDirection::None,
              "5 has no second legend");
        Check(Of(nazg::NamedKey{ "KC_PDOT" }).second == "Del", "the dot is Delete");

        const KeycapLegend haptic = Of(nazg::NamedKey{ "HF_TOGG" });
        Check(haptic.placement == PlacementClass::Command && Says(haptic, "Toggle Haptic"),
              "a QMK feature is a command, by its label until commands get their own legends");
        Check(Says(Of(nazg::NamedKey{ "QK_STENO_BOLT" }), "QK_STENO_BOLT"),
              "a keycode QMK has since removed falls back to its name");
    }

    // ui-design.md, "Names of the modifiers" and "Modifier names follow a setting".
    void TestModifiers()
    {
        std::printf("modifiers\n");

        const auto on = [](const char* key, KeySide side, ModifierNames names = ModifierNames::Windows)
        { return Of(nazg::NamedKey{ key }, UsHostLayout(), names, side); };

        Check(Says(on("KC_LCTL", KeySide::Left), "Control", "Ctrl"), "left Ctrl on the left: the plain keycap word");
        Check(on("KC_LCTL", KeySide::Left).spherical.full == "CTRL", "CTRL on spherical sets");
        Check(Says(on("KC_RCTL", KeySide::Right), "Control", "Ctrl"), "right Ctrl on the right: the same");
        Check(Says(on("KC_RCTL", KeySide::Left), "Right Control", "R Ctrl"),
              "right Ctrl on the left names its side");
        Check(on("KC_RCTL", KeySide::Left).spherical.full == "R CTRL", "R CTRL on spherical sets");
        Check(Says(on("KC_LSFT", KeySide::Neither), "Left Shift", "L Shift"), "a key on neither side names its side");

        Check(Says(on("KC_RALT", KeySide::Left), "Alt Gr"), "right Alt is Alt Gr, never naming its side");
        Check(on("KC_RALT", KeySide::Right).spherical.full == "ALT GR", "ALT GR on spherical sets");
        Check(Says(on("KC_LGUI", KeySide::Left), "Win"), "the GUI key is Win on Windows");

        Check(Says(on("KC_LGUI", KeySide::Left, ModifierNames::Mac), "Cmd"), "Cmd on a Mac");
        Check(Says(on("KC_LALT", KeySide::Left, ModifierNames::Mac), "Option", "Opt"), "Alt is Option on a Mac");
        Check(Says(on("KC_RALT", KeySide::Right, ModifierNames::Mac), "Option", "Opt"), "and so is right Alt");
        Check(Says(on("KC_LCTL", KeySide::Left, ModifierNames::Mac), "Control", "Ctrl"), "Ctrl stays Ctrl");
        Check(Says(on("KC_RGUI", KeySide::Right, ModifierNames::Linux), "Super"), "Super on Linux");
        Check(Says(on("KC_RALT", KeySide::Right, ModifierNames::Linux), "Alt Gr"), "with Alt Gr");

        Check(nazg::ModifierNamesFromId("mac") == ModifierNames::Mac, "names are saved by id");
        Check(!nazg::ModifierNamesFromId("beos"), "an unknown id is none");
    }

    void TestComposedKeys()
    {
        std::printf("composed keys\n");

        Check(Pair(Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }), "!", ""),
              "LSFT(KC_1) is shown as the character it types");
        Check(Pair(Of(nazg::ModifiedKey{ Mod::RightShift, "KC_SLSH" }), "?", ""), "on either Shift");

        const KeycapLegend shiftedA = Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_A" });
        Check(Pair(shiftedA, "A", "") && shiftedA.header == "Shift+",
              "a shifted letter has no separate character, so the modifier is the header");
        Check(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_C" }).header == "Ctrl Sft+",
              "two modifiers in short words");
        Check(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt | Mod::LeftGui, "KC_C" }).header ==
                  "Hyper+",
              "all four is Hyper");
        Check(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt, "KC_C" }).header == "Meh+",
              "three without the GUI key is Meh");
        Check(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftGui, "KC_C" }).header == "C S G+",
              "other threes by initials");

        const KeycapLegend space = Of(nazg::LayerTapKey{ 1, "KC_SPC" });
        Check(space.placement == PlacementClass::Blank && space.header == "L1", "LT: the layer as header over the tap");
        const KeycapLegend escape = Of(nazg::ModTapKey{ Mod::LeftCtrl, "KC_ESC" });
        Check(Says(escape, "Esc") && escape.header == "Ctrl", "MT: Ctrl over Esc");
        Check(Of(nazg::ModTapKey{ Mod::RightAlt, "KC_A" }).header == "Alt Gr", "a right-Alt hold is Alt Gr");
        Check(Of(nazg::ModTapKey{ Mod::RightAlt, "KC_A" }, UsHostLayout(), ModifierNames::Mac).header == "Option",
              "and Option on a Mac");

        const KeycapLegend hold = Of(nazg::LayerKey{ nazg::LayerOp::Momentary, 2 });
        Check(hold.placement == PlacementClass::Command && hold.header == "Hold" && Says(hold, "L2"),
              "MO(2): Hold over L2");
        Check(Of(nazg::LayerKey{ nazg::LayerOp::PersistentDefault, 1 }).header == "Set base", "PDF is Set base");
        Check(Says(Of(nazg::UnknownKey{ 0x8123 }), "0x8123"), "an unknown value is shown as hex");
    }

    void TestCaptions()
    {
        std::printf("captions\n");

        Check(nazg::CaptionOf(Of(nazg::NamedKey{ "KC_1" })) == "! 1", "a pair, Shift first");
        Check(nazg::CaptionOf(Of(nazg::NamedKey{ "KC_LCTL" })) == "Left Control", "a modifier off the board names its side");
        Check(nazg::CaptionOf(Of(nazg::ModTapKey{ Mod::LeftCtrl, "KC_ESC" })) == "Ctrl Esc", "the header first");
        Check(nazg::CaptionOf(Of(nazg::NamedKey{ "KC_SPC" })) == "Space", "the blank space bar still has a name");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestLayoutTable();
    TestCharacters();
    TestFrench();
    TestFourthLevel();
    TestLegendSet();
    TestModifiers();
    TestComposedKeys();
    TestCaptions();

    return TestResult();
}
