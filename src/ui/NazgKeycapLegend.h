// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeycapLegend - what to print on a key: a Keycode seen through the host's layout, the
// modifier names and where the key sits. What the key IS, with no positions: the renderer
// places it (ui/NazgKeycapLayout.h), by the legend family no section knows of.
//
// The firmware stores positions, not characters: KC_Q is "the key where US QWERTY has
// Q", and it types "a" on a French AZERTY host. So legends are presentation, computed
// here at draw time from the keycode and a HostLayout; the keymap itself never changes
// with the host. Decided 2026-09-23 -- see docs/research_material/keycodes.md, "Host
// layouts": plain and Shift legends, one global host layout, US by default, chosen from
// QMK's keymap extras.
//
// The words come from the legend set in NazgKeycapLegend.cpp -- placement classes, their
// exceptions, the standard keys' names and short forms of short-forms.md -- and the rules
// from ui-design.md, "Legends -- the plan" and after: Alt Gr and the GUI key by the
// modifier-names setting, a modifier naming its side only where its keycode's side is not
// where the key sits, the numpad's second legends, the AltGr character always, Bépo's
// fourth level. Command keys print a provisional fallback until their own legends come
// (step 4 of "How the board's look is built").
//
// Pure code, no ImGui, so it tests with literals.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "model/NazgKeycode.h"

namespace nazg
{
    // What one position types on a host layout: plain, and with Shift held. `shifted`
    // is empty where Shift adds nothing worth printing -- the letters, whose one legend
    // is the capital. `altgr` and `shiftAltgr` are the third and fourth levels -- AltGr on
    // PC layouts, Option on the Mac ones.
    struct HostLegend
    {
        std::string_view key;       // basic keycode name: KC_1, KC_Q, KC_NUBS
        std::string_view plain;
        std::string_view shifted;
        std::string_view altgr;
        std::string_view shiftAltgr;
    };

    // A host keyboard layout: the positions whose legend depends on it. Everything else
    // -- Enter, F1, arrows, keypad -- types the same on every layout and is labelled
    // from the legend set.
    struct HostLayout
    {
        std::string_view            id;        // QMK's name, stable: "french_mac_iso"
        std::string_view            name;      // for people: "French (Mac ISO)"
        std::span<const HostLegend> legends;

        [[nodiscard]] const HostLegend* Find(std::string_view key) const noexcept;

        // Whether keycaps for it print Shift+AltGr: Bépo only, where the level is part of
        // the layout (ui-design.md, "The fourth level, Bépo, and spherical AltGr").
        [[nodiscard]] bool PrintsFourthLevel() const noexcept;
    };

    // Every layout, from QMK's keymap extras (NazgHostLayoutTable.cpp), sorted by id.
    [[nodiscard]] std::span<const HostLayout> HostLayouts() noexcept;

    // nullptr for an id no layout has -- a setting saved by a newer build, say.
    [[nodiscard]] const HostLayout* FindHostLayout(std::string_view id) noexcept;

    // The default, and the only choice a build without the table would have.
    [[nodiscard]] const HostLayout& UsHostLayout() noexcept;

    // What the modifiers are called on the computer the board is used with -- a setting,
    // set at first launch from the OS Nazg runs on (ui-design.md, "Modifier names follow a
    // setting"). The host layout cannot tell: most Mac users pick a layout PCs share.
    enum class ModifierNames : uint8_t
    {
        Windows,   // Win, Alt, Alt Gr
        Mac,       // Cmd, Option, Option
        Linux,     // Super, Alt, Alt Gr
    };

    [[nodiscard]] std::span<const ModifierNames>  AllModifierNames();
    [[nodiscard]] std::string_view                IdOf(ModifierNames names);   // for imgui.ini
    [[nodiscard]] std::string_view                NameOf(ModifierNames names);
    [[nodiscard]] std::optional<ModifierNames>    ModifierNamesFromId(std::string_view id);

    // The legends' settings, which a section reads: the host layout by its id, kept as saved
    // -- an id this build does not know draws as US -- and the modifier names.
    struct LegendSettings
    {
        std::string   hostLayout    = "us";
        ModifierNames modifierNames = ModifierNames::Windows;

        [[nodiscard]] const HostLayout& Layout() const noexcept;
    };

    // Which side of the board a key sits on, for the modifiers with a keycode per side --
    // measured from the space bar's centre (see SideLine() in ui/NazgBoardDescription.h).
    // A key straddling the line is on neither; so is one with no place on a board, as in the
    // keycode picker. A key on neither side always names its side.
    enum class KeySide : uint8_t
    {
        Left,
        Right,
        Neither,
    };

    // How a legend is placed, by what the key is (ui-design.md, "Special cases are data:
    // placement classes"). Each legend family has a rule per class, in KeycapLayout.
    enum class PlacementClass : uint8_t
    {
        Blank,         // nothing printed: the space bar, KC_NO
        Character,     // what the host layout types: one legend or a Shift pair
        FunctionRow,   // F-keys -- text, small
        Modifier,      // a named key in words: Shift, Backspace, Page Up
        Arrow,         // drawn, not set in a font
        Numpad,        // a digit or an operator, at letter size
        Command,       // anything QMK adds -- provisional: its QMK label, centred
    };

    // Where a key breaks its class's rule -- data in the legend set, never a key named in the
    // drawing code. Only cylindrical legends read it: spherical ones are all centred.
    enum class Placement : uint8_t
    {
        ByClass,
        MiddleLeft,   // Esc with the function row; Num Lock and the numpad's top operators
        Centre,       // the numpad's tall + and Enter
    };

    enum class ArrowDirection : uint8_t
    {
        None,
        Up,
        Down,
        Left,
        Right,
    };

    // A name in words, and its short form for where the name does not fit even on two lines.
    // `shortForm` empty: there is none.
    struct Words
    {
        std::string full;
        std::string shortForm;

        bool operator==(const Words&) const = default;
    };

    // What a keycap says. Which fields are filled depends on `placement`:
    // - Character: `plain`, and `shifted` for a pair; `altgr`, printed bottom right on every
    //   layout that has one, and `shiftAltgr` where the layout prints the fourth level.
    // - FunctionRow, Modifier, Numpad, Command: the words, one set per legend family --
    //   cylindrical in GMK's mixed case, spherical in SA's capitals and words.
    // - Arrow: `arrow`.
    // Any key can carry a `header` -- a tap-hold's hold, a modified key's modifiers -- top right,
    // and a numpad key its `second` legend, what it does with Num Lock off.
    struct KeycapLegend
    {
        PlacementClass placement = PlacementClass::Blank;
        Placement      place     = Placement::ByClass;

        std::string plain;
        std::string shifted;
        std::string altgr;
        std::string shiftAltgr;

        Words cylindrical;
        Words spherical;

        ArrowDirection arrow = ArrowDirection::None;

        // Cylindrical sets only: text bottom left, or an arrow bottom right.
        std::string    second;
        ArrowDirection secondArrow = ArrowDirection::None;

        std::string header;

        bool operator==(const KeycapLegend&) const = default;
    };

    struct LegendContext
    {
        const HostLayout& layout;
        ModifierNames     names = ModifierNames::Windows;
        KeySide           side  = KeySide::Neither;
    };

    [[nodiscard]] KeycapLegend LegendFor(const Keycode& keycode, const LegendContext& context);

    // One line for lists, where nothing is placed -- the keycode picker: "! 1", "Backspace",
    // "Ctrl+ C".
    [[nodiscard]] std::string CaptionOf(const KeycapLegend& legend);
}
