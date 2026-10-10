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

#include <optional>
#include <set>
#include <string>
#include <vector>

using nazg::ArrowDirection;
using nazg::Header;
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

using Category = nazg::CommandCategory;

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

    std::string HeaderOf(const KeycapLegend& legend)
    {
        return legend.header.words.full;
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
        Check(Pair(Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }, french), "&", "1") &&
                  HeaderOf(Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" }, french)) == "Shift+",
              "LSFT(KC_1) on AZERTY: Shift+ over the key's own 1 and &");

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

        Check(Says(Of(nazg::NamedKey{ "KC_HELP" }), "Help") &&
                  Of(nazg::NamedKey{ "KC_HELP" }).placement == PlacementClass::Modifier,
              "a basic keycode a stock keyboard can have is a modifier in words, by its QMK label");
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

        // Shift alone is no exception: "Shift+" over the key's own pair, as any modifier.
        const KeycapLegend shifted1 = Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_1" });
        Check(Pair(shifted1, "1", "!") && shifted1.header == Header{ { "Shift+", "" }, Category::Host },
              "LSFT(KC_1): Shift+ over ! 1, one rule for every modifier");
        const KeycapLegend rightShifted = Of(nazg::ModifiedKey{ Mod::RightShift, "KC_SLSH" });
        Check(Pair(rightShifted, "/", "?") && rightShifted.Band() == Category::Host, "on either Shift");

        // A modified key: the modifiers as a header ending in "+", in the Host colour.
        const KeycapLegend shiftedA = Of(nazg::ModifiedKey{ Mod::LeftShift, "KC_A" });
        Check(Pair(shiftedA, "A", "") && shiftedA.header == Header{ { "Shift+", "" }, Category::Host },
              "a shifted letter has no separate character, so the modifier is the header, in Host's colour");
        Check(shiftedA.Band() == Category::Host, "with a Host band");
        Check(HeaderOf(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift, "KC_C" })) == "Ctrl Sft+",
              "two modifiers in short words");
        Check(HeaderOf(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt | Mod::LeftGui, "KC_C" })) ==
                  "Hyper+",
              "all four is Hyper");
        Check(HeaderOf(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt, "KC_C" })) == "Meh+",
              "three without the GUI key is Meh");
        Check(HeaderOf(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftShift | Mod::LeftGui, "KC_C" })) == "C S G+",
              "other threes by initials");

        // Two modifiers too wide for 1u -- only Cmd's pairs -- shorten to their shortest words, Cmd
        // as ⌘ (Rico, 2026-10-03).
        const auto mac = [](uint8_t mods)
        { return Of(nazg::ModifiedKey{ mods, "KC_C" }, UsHostLayout(), ModifierNames::Mac).header.words; };
        Check(mac(Mod::LeftCtrl | Mod::LeftGui) == nazg::Words{ "Ctrl Cmd+", "Ctl ⌘+" }, "Ctrl Cmd+, short Ctl ⌘+");
        Check(mac(Mod::LeftAlt | Mod::LeftGui) == nazg::Words{ "Opt Cmd+", "Opt ⌘+" }, "Opt Cmd+, short Opt ⌘+");
        Check(Of(nazg::ModifiedKey{ Mod::LeftCtrl | Mod::LeftGui, "KC_C" }).header.words ==
                  nazg::Words{ "Ctrl Win+", "Ctl Win+" },
              "on Windows, Ctrl Win+ -- which fits 1u, so its short form never shows");

        // Tap-hold keys: the hold above the tap's own legends, a Behaviour.
        const KeycapLegend space = Of(nazg::LayerTapKey{ 1, "KC_SPC" });
        Check(space.placement == PlacementClass::Blank && space.hold == Header{ { "L1", "" }, Category::Behaviour },
              "LT: the layer as the hold over the tap, a Behaviour");
        Check(space.header.IsEmpty() && space.Band() == Category::Behaviour, "the band is the hold's");
        const KeycapLegend escape = Of(nazg::ModTapKey{ Mod::LeftCtrl, "KC_ESC" });
        Check(Says(escape, "Esc") && escape.hold.words.full == "Ctrl", "MT: Ctrl over Esc");
        Check(Of(nazg::ModTapKey{ Mod::RightAlt, "KC_A" }).hold.words.full == "Alt Gr", "a right-Alt hold is Alt Gr");
        Check(Of(nazg::ModTapKey{ Mod::RightAlt, "KC_A" }, UsHostLayout(), ModifierNames::Mac).hold.words.full ==
                  "Option",
              "and Option on a Mac");
        Check(Of(nazg::SwapHandsTapKey{ "KC_A" }).hold.words.full == "Swap", "SH_T: Swap held");

        // A tap that is a command keeps its own header, under the hold's, each in its colour.
        const KeycapLegend play = Of(nazg::LayerTapKey{ 1, "KC_MPLY" });
        Check(play.hold == Header{ { "L1", "" }, Category::Behaviour } &&
                  play.header == Header{ { "Media", "" }, Category::Host } && Says(play, "Play"),
              "LT(1, KC_MPLY): L1 over Media / Play");
        Check(play.Band() == Category::Behaviour, "one band per key, the hold's");

        const KeycapLegend hold = Of(nazg::LayerKey{ nazg::LayerOp::Momentary, 2 });
        Check(hold.placement == PlacementClass::Command && HeaderOf(hold) == "Hold" && Says(hold, "L2") &&
                  hold.header.category == Category::Behaviour,
              "MO(2): Hold over L2, a Behaviour");
        Check(HeaderOf(Of(nazg::LayerKey{ nazg::LayerOp::PersistentDefault, 1 })) == "Set base", "PDF is Set base");
        Check(HeaderOf(Of(nazg::LayerKey{ nazg::LayerOp::TapToggle, 3 })) == "Tap tog", "TT is Tap tog");
        Check(Says(Of(nazg::LayerModKey{ 1, Mod::LeftCtrl }), "L1 Ctrl") &&
                  HeaderOf(Of(nazg::LayerModKey{ 1, Mod::LeftCtrl })) == "Hold",
              "LM(1, Ctrl): Hold over L1 Ctrl");
        Check(Says(Of(nazg::OneShotModKey{ Mod::LeftShift }), "Shift") &&
                  HeaderOf(Of(nazg::OneShotModKey{ Mod::LeftShift })) == "Once",
              "OSM(Shift): Once over Shift");
        Check(Says(Of(nazg::TapDanceKey{ 3 }), "TD 3") && HeaderOf(Of(nazg::TapDanceKey{ 3 })) == "Dance",
              "TD(3): Dance over TD 3");
        const KeycapLegend macro = Of(nazg::MacroKey{ 3 });
        Check(Says(macro, "M3") && macro.header == Header{ { "Macro", "" }, Category::Host }, "a macro is Host: Macro / M3");

        const KeycapLegend unknown = Of(nazg::UnknownKey{ 0x8123 });
        Check(Says(unknown, "0x8123") && unknown.Band() == Category::None, "an unknown value is its hex, with no band");
    }

    // A Vial tap dance once its slot is read: a tap and a hold as a tap-hold, the hold in its own
    // category; a tap alone under "Dance"; no tap, or unread, "Dance / TD n".
    void TestTapDances()
    {
        std::printf("tap dances, read\n");

        const auto dance = [](std::optional<nazg::Keycode> tap, std::optional<nazg::Keycode> hold)
        {
            nazg::TapDance d;
            d.actions = { tap, hold, nazg::Keycode{ nazg::NamedKey{ "KC_CAPS" } }, std::nullopt };
            return d;
        };
        const std::vector<nazg::TapDance> dances = {
            dance(nazg::NamedKey{ "KC_SPC" }, nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }),
            dance(nazg::NamedKey{ "KC_ESC" }, nazg::NamedKey{ "QK_BOOT" }),
            dance(nazg::NamedKey{ "KC_C" }, nazg::ModifiedKey{ Mod::LeftCtrl, "KC_C" }),
            dance(nazg::NamedKey{ "KC_ESC" }, std::nullopt),
            dance(nazg::NamedKey{ "KC_MPLY" }, std::nullopt),
            dance(std::nullopt, nazg::LayerKey{ nazg::LayerOp::Momentary, 1 }),
        };
        const auto read = [&](uint8_t index)
        {
            LegendContext context{ UsHostLayout(), ModifierNames::Windows, KeySide::Neither };
            context.tapDances = dances;
            return LegendFor(nazg::TapDanceKey{ index }, context);
        };

        const KeycapLegend layer = read(0);
        Check(layer == Of(nazg::LayerTapKey{ 1, "KC_SPC" }), "tap Space, hold L1: drawn exactly as LT(1, KC_SPC)");
        const KeycapLegend boot = read(1);
        Check(Says(boot, "Esc") && boot.hold == Header{ { "Boot", "" }, Category::Firmware } && boot.Band() == Category::Firmware,
              "Boot held behind Esc: the hold and the band in Firmware's colour");
        Check(read(2).hold == Header{ { "Ctrl+C", "" }, Category::Host }, "a shortcut held is one line, Host's");
        const KeycapLegend tapOnly = read(3);
        Check(Says(tapOnly, "Esc") && tapOnly.header == Header{ { "Dance", "" }, Category::Behaviour } && tapOnly.hold.IsEmpty(),
              "a tap without a hold: Dance over the tap");
        const KeycapLegend media = read(4);
        Check(media.header == Header{ { "Media", "" }, Category::Host } && media.hold == Header{ { "Dance", "" }, Category::Behaviour },
              "a command tap keeps its header, Dance takes the hold's place");
        Check(Says(read(5), "TD 5") && HeaderOf(read(5)) == "Dance", "no tap: Dance / TD n");
        Check(Says(read(9), "TD 9"), "a slot not read: Dance / TD n");
    }

    // The command table and the families built from parts (short-forms.md).
    void TestCommands()
    {
        std::printf("commands\n");

        const KeycapLegend haptic = Of(nazg::NamedKey{ "HF_TOGG" });
        Check(haptic.placement == PlacementClass::Command && Says(haptic, "On/Off") &&
                  haptic.header == Header{ { "Haptic", "" }, Category::Board },
              "HF_TOGG: Haptic / On/Off, a Board command");
        Check(haptic.spherical == haptic.cylindrical, "command keys leave the family's case: the same words on both");

        const KeycapLegend boot = Of(nazg::NamedKey{ "QK_BOOT" });
        Check(Says(boot, "Boot") && boot.header.category == Category::Firmware && boot.Band() == Category::Firmware,
              "QK_BOOT: Firmware / Boot");

        const KeycapLegend capsWord = Of(nazg::NamedKey{ "CW_TOGG" });
        Check(capsWord.header.words == nazg::Words{ "Caps Word", "Caps Wd" } && capsWord.header.category == Category::Behaviour,
              "a header carries its short form");
        Check(Says(Of(nazg::NamedKey{ "KC_MCTL" }), "Mission Ctrl", "Mission"), "so does a main legend");
        Check(Of(nazg::NamedKey{ "KC_VOLU" }).header.category == Category::Host, "media keys are Host");
        Check(Says(Of(nazg::NamedKey{ "KC_VOLD" }), "Vol −"), "a quantity goes down with the true minus");
        Check(Says(Of(nazg::NamedKey{ "MS_WHLD" }), "Wh Dn"), "a direction with Dn");

        // The modifier names, by the setting.
        const auto agTogg = [](ModifierNames names) { return Of(nazg::NamedKey{ "AG_TOGG" }, UsHostLayout(), names).header.words; };
        Check(agTogg(ModifierNames::Windows) == nazg::Words{ "Alt↔Win", "Alt/Win" }, "AG_TOGG: Alt↔Win on Windows");
        Check(agTogg(ModifierNames::Mac) == nazg::Words{ "Option↔Cmd", "Opt/Cmd" }, "Option↔Cmd on a Mac");
        Check(HeaderOf(Of(nazg::NamedKey{ "GU_TOGG" }, UsHostLayout(), ModifierNames::Linux)) == "Super key",
              "GU_TOGG: Super key on Linux");

        // The numbered families.
        Check(Says(Of(nazg::NamedKey{ "JS_3" }), "Btn 3") && HeaderOf(Of(nazg::NamedKey{ "JS_3" })) == "Joystick",
              "JS_3: Joystick / Btn 3");
        Check(Says(Of(nazg::NamedKey{ "PB_12" }), "12"), "PB_12: Prog btn / 12");
        Check(Says(Of(nazg::NamedKey{ "BT_PRF2" }), "Prof 2"), "BT_PRF2: Bluetooth / Prof 2");
        const KeycapLegend user = Of(nazg::NamedKey{ "QK_USER_4" });
        Check(Says(user, "U4") && user.header == Header{ { "User", "" }, Category::Board }, "QK_USER_4: User / U4, Board as QK_KB_");
        const KeycapLegend custom = Of(nazg::NamedKey{ "QK_KB_7" });
        Check(Says(custom, "KB 7") && custom.header == Header{ { "Custom", "" }, Category::Board }, "QK_KB_7: Custom / KB 7");

        // A name from the definition wins over the number (short-forms.md, rule 8).
        const nazg::Words           names[] = { { "Mission Control", "MCtrl" }, { "", "" } };
        const nazg::LegendContext   named{ UsHostLayout(), ModifierNames::Windows, nazg::KeySide::Neither, 0, names };
        const KeycapLegend          mctrl = nazg::LegendFor(nazg::NamedKey{ "QK_KB_0" }, named);
        Check(mctrl.cylindrical == nazg::Words{ "Mission Control", "MCtrl" } &&
                  mctrl.header == Header{ { "Custom", "" }, Category::Board },
              "QK_KB_0 named by the definition: Custom / Mission Control");
        Check(Says(nazg::LegendFor(nazg::NamedKey{ "QK_KB_1" }, named), "KB 1"), "an empty name keeps the number");
        Check(Says(nazg::LegendFor(nazg::NamedKey{ "QK_KB_5" }, named), "KB 5"), "and so does one past the names");

        // Space Cadet is drawn as a tap-hold key, its parenthesis what Shift+9 types on the host.
        const KeycapLegend cadet = Of(nazg::NamedKey{ "SC_LSPO" });
        Check(Pair(cadet, "(", "") && cadet.hold == Header{ { "Shift", "" }, Category::Behaviour },
              "SC_LSPO: Shift over (");
        Check(Pair(Of(nazg::NamedKey{ "SC_RCPC" }, *nazg::FindHostLayout("german")), "=", ""),
              "SC_RCPC on a German host: Shift+0 types =");
        Check(Says(Of(nazg::NamedKey{ "SC_SENT" }), "Enter"), "SC_SENT: Shift over Enter");
        Check(Of(nazg::NamedKey{ "SC_RAPC" }, UsHostLayout(), ModifierNames::Mac).hold.words.full == "Option",
              "SC_RAPC: Option held on a Mac");

        // MIDI and steno: what they go to, in Host's colour, over QMK's own name past its prefix.
        const KeycapLegend midi = Of(nazg::NamedKey{ "MI_CHND" });
        Check(Says(midi, "CHND") && midi.header == Header{ { "MIDI", "" }, Category::Host }, "MI_CHND: MIDI / CHND");
        const KeycapLegend steno = Of(nazg::NamedKey{ "ST_ST1" });
        Check(Says(steno, "ST1") && steno.header == Header{ { "Steno", "" }, Category::Host }, "ST_ST1: Steno / ST1");
        Check(HeaderOf(Of(nazg::NamedKey{ "SQ_TMPU" })) == "Seq" && Of(nazg::NamedKey{ "SQ_TMPU" }).Band() == Category::Host,
              "SQ_TMPU: the sequencer plays notes to the computer, Host");
        Check(Says(Of(nazg::NamedKey{ "QK_STENO_BOLT" }), "Bolt"), "a keycode QMK has since removed still has its words");
    }

    // Light on a board with one lighting system; the system's word with several (short-forms.md,
    // rule 5).
    void TestLighting()
    {
        std::printf("lighting\n");

        namespace System = nazg::LightingSystem;
        const auto header = [](const char* key, uint8_t board)
        { return LegendFor(nazg::NamedKey{ key }, LegendContext{ UsHostLayout(), ModifierNames::Windows, KeySide::Neither, board }).header.words.full; };

        Check(header("UG_TOGG", System::Underglow) == "Light", "one system: Light");
        Check(header("UG_TOGG", 0) == "Light", "none declared: Light");
        Check(header("BL_TOGG", System::Backlight | System::Underglow) == "Backlit" &&
                  header("UG_TOGG", System::Backlight | System::Underglow) == "UGlow",
              "backlight and underglow: Backlit and UGlow");
        Check(header("UG_TOGG", System::Underglow | System::RgbMatrix) == "UGlow" &&
                  header("RM_TOGG", System::Underglow | System::RgbMatrix) == "Matrix",
              "underglow and RGB Matrix: UG_ is UGlow, RM_ is Matrix");
        Check(header("UG_TOGG", System::Backlight | System::RgbMatrix) == "Matrix",
              "backlight and RGB Matrix: UG_ drives the matrix");
        Check(header("RGB_M_P", System::Backlight | System::Underglow) == "UGlow", "RGB_M_ modes are UGlow too");
        Check(header("LM_TOGG", System::Underglow | System::LedMatrix) == "LEDs", "LED Matrix: LEDs");
        Check(Of(nazg::NamedKey{ "UG_TOGG" }).header.category == Category::Board, "lighting is Board");
    }

    // Every QMK keycode a stock keyboard does not have is in the command table, or built from its
    // parts -- so a new one, once in the keycode table, cannot be forgotten here.
    void TestCoverage()
    {
        std::printf("coverage\n");

        std::set<std::string_view> missing;
        for (const nazg::QmkKeycode& row : nazg::QmkKeycodeTable())
        {
            const std::string_view group = row.group;
            if (group == "basic" || group == "modifiers" || group == "internal" || group == "midi" || group == "steno")
                continue;

            const KeycapLegend legend = Of(nazg::NamedKey{ row.name });
            if (legend.Band() == Category::None && legend.hold.IsEmpty())
                missing.insert(row.name);
        }
        for (std::string_view name : missing)
            std::printf("    no words: %.*s\n", static_cast<int>(name.size()), name.data());
        Check(missing.empty(), "every QMK feature keycode has a header and a category");

        std::set<std::string_view> keys;
        bool                       unique = true;
        for (const nazg::CommandEntry& entry : nazg::CommandEntries())
            unique &= keys.insert(entry.key).second;
        Check(unique, "no keycode twice in the command table");
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
    TestTapDances();
    TestCommands();
    TestLighting();
    TestCoverage();
    TestCaptions();

    return TestResult();
}
