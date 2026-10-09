// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardDescription - what a section wants the board to show, and what the board tells it
// back. Point 2 of the section contract (ui/NazgSection.h).
//
// Nazg draws the board; a section only fills it. So a section says WHAT each key means --
// its legends by position, the meaning of its fill, its state -- and never how it looks:
// colours, fonts and sizes belong to the board renderer (ui/NazgBoardView.h) and the theme
// (ui/NazgTheme.h), and a restyle touches only those. No pixels either: positions are in
// key units, as in the definition. The six rules below are ui-design.md's, "How a section
// describes the board", checked there against the Leyden Jar tool's keys, keycap colour
// themes and sublegends.
//
// Pure data, no ImGui, so it tests with literals.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "model/NazgKeyboard.h"
#include "model/NazgKeycode.h"
#include "ui/NazgKeycapLegend.h"

namespace nazg
{
    // Rule 1: legends by position -- KLE's twelve, in KLE's order: the top face's three
    // rows of three, left to right, then the front face.
    enum class LegendSlot : uint8_t
    {
        TopLeft,    TopCentre,    TopRight,
        MiddleLeft, MiddleCentre, MiddleRight,
        BottomLeft, BottomCentre, BottomRight,
        FrontLeft,  FrontCentre,  FrontRight,
    };

    inline constexpr size_t c_LegendSlotCount = 12;

    // What a legend is, for the renderer to style. Decorative sublegends -- Hiragana, Hangul
    // -- were dropped with their role (ui-design.md, "Second legends: functional only").
    enum class LegendRole : uint8_t
    {
        Label,   // what the key does
        Value,   // a reading: a signal level, a bin number
    };

    struct Legend
    {
        std::string text;   // empty: the slot is empty
        LegendRole  role = LegendRole::Label;
    };

    using SlotLegends = std::array<Legend, c_LegendSlotCount>;

    // Rule 2: the fill, by meaning.
    enum class KeyFill : uint8_t
    {
        Neutral,

        // The keycap's colour class, which the theme colours (ui-design.md, "Keycap colour
        // classes"): from what the key does on the base layer -- see KeycapClassOf(). KLE
        // colours will only group the keys, once the definition parser keeps them.
        Alpha,
        Modifier,
        Accent,

        Heat,   // BoardKey::heat, 0 to 1 -- a heat map, overriding the theme while shown
    };

    // Rule 2 too: states, drawn as an outline or an overlay and never as the fill, so they
    // read on every theme. A bit set. Hovered is not one: the renderer knows it by itself.
    namespace Mark
    {
        inline constexpr uint8_t Selected    = 0x01;
        inline constexpr uint8_t Highlighted = 0x02;
        inline constexpr uint8_t Dimmed      = 0x04;
        inline constexpr uint8_t Warning     = 0x08;
        inline constexpr uint8_t Pressed     = 0x10;
        inline constexpr uint8_t Struck      = 0x20;   // edge labels: a position no key uses

        // A second highlight, told apart from the first where both show -- the matrix view's
        // column beside its row.
        inline constexpr uint8_t HighlightedSecond = 0x40;

        // Ticked off a checklist -- the live test's keys seen, its rows and columns complete.
        inline constexpr uint8_t Checked = 0x80;
    }

    // Rule 1 too: whose keycode a key's legends are (ui-design.md, "Transparent keys"). QMK takes
    // a key's keycode from the highest active layer down: KC_TRNS says "keep looking", KC_NO
    // "stop, do nothing". On a partial layer a key wears the keycode below it, and says which of
    // the two it is.
    enum class Fallthrough : uint8_t
    {
        None,          // the layer's own keycode
        Transparent,   // KC_TRNS: the keycode it falls through to
        Disabled,      // KC_NO: the keycode it disables, if any
    };

    struct BoardKey
    {
        // Rule 3: where it is. The definition's key unless the section brings its own
        // geometry -- the Leyden Jar's controller matrix is a plain grid. A decal takes
        // space and is never drawn; its legends and marks are ignored.
        DefinitionKey geometry;

        // Rule 1, in one of two forms (ui-design.md, "How the board's look is built"): what
        // the key IS -- keycap legends, placed by the renderer by the legend family, as Keymap
        // fills them -- or legends at the twelve slots, placed exactly there, for a section
        // whose positions mean something themselves. Blank keycap legends to start with.
        std::variant<KeycapLegend, SlotLegends> legends;
        Fallthrough                             fallthrough = Fallthrough::None;

        KeyFill fill  = KeyFill::Neutral;
        float   heat  = 0.0f;   // with KeyFill::Heat
        uint8_t marks = 0;

        // One slot -- turning the key's legends into slot legends, all empty, if they were not.
        Legend& operator[](LegendSlot slot);
    };

    // Rule 4: a line over the board through the centres of these keys, in this order --
    // the matrix view's wiring.
    struct BoardLine
    {
        std::vector<size_t> keys;        // indices into BoardDescription::keys
        uint8_t             marks = 0;   // Highlighted, HighlightedSecond, Dimmed
    };

    // Rule 5: labels around the board's edges -- the matrix view's rulers, the Leyden Jar's
    // R0.../C0... connectors. Left and top are the only edges anything uses.
    enum class BoardEdge : uint8_t
    {
        Left,   // one per row, down the left side: level with its leftmost key
        Top,    // one per column, along the top: centred on its top key
    };

    struct EdgeLabel
    {
        BoardEdge           edge = BoardEdge::Left;
        std::string         text;
        std::vector<size_t> keys;        // it is centred on the one nearest its edge
        uint8_t             marks = 0;   // Highlighted, HighlightedSecond, Dimmed, Struck, Checked
    };

    struct BoardDescription
    {
        std::vector<BoardKey>  keys;
        std::vector<BoardLine> lines;
        std::vector<EdgeLabel> labels;
    };

    // Rule 6: hover shared both ways. What the board saw this frame, told to the section --
    // which answers the other way by setting marks, from its panel as much as from here.
    struct BoardEvents
    {
        std::optional<size_t> hoveredKey;     // indices into BoardDescription::keys
        std::optional<size_t> clickedKey;
        std::optional<size_t> hoveredLabel;   // indices into BoardDescription::labels
        std::optional<size_t> clickedLabel;

        // What the board took, plate and edge labels included, in pixels: a splitter under it
        // moves from there. `fullWidth` is the board's own, wider than `width` while the window
        // is too narrow and it scrolls; `floorWidth` the board's at its legibility floor.
        float width      = 0.0f;
        float height     = 0.0f;
        float fullWidth  = 0.0f;
        float floorWidth = 0.0f;
    };

    // What a section starts from: the board as its definition draws it, at its layout
    // choice, decals included, every legend empty and nothing marked -- each key filled with
    // its keycap class, from the base layer.
    [[nodiscard]] BoardDescription DescribeKeyboard(const Keyboard& keyboard);

    // The same with other layout choices than the board's -- the Layout section's preview.
    [[nodiscard]] BoardDescription DescribeKeyboard(const Keyboard& keyboard, const std::vector<uint8_t>& selection);

    // What a key at `layer` comes to: its own keycode, or -- transparent or KC_NO -- the keycode
    // below it, found by walking down the layer numbers, exact while one layer is on at a time.
    // A transparent key that reaches a KC_NO is disabled with it; one that reaches nothing, on
    // the default layer say, is disabled with nothing below.
    struct ResolvedKey
    {
        Fallthrough            fallthrough = Fallthrough::None;
        Keycode                keycode;   // KC_NO when nothing is below
        std::optional<uint8_t> layer;     // where `keycode` is; none when nothing is below
    };

    [[nodiscard]] ResolvedKey ResolveKey(const Keyboard& keyboard, const DefinitionKey& key, uint8_t layer);

    // Keymap's legends for every key of `board` but decals: what `layer` of `keyboard` does
    // there -- or below it, for a transparent or KC_NO key -- seen through `settings`, the key's
    // side of the board and the board's lighting. Matrix views fill layer 0's, to find the keys by.
    void DescribeLegends(BoardDescription& board, const Keyboard& keyboard, uint8_t layer,
                         const LegendSettings& settings);

    // The lighting systems a board has, LightingSystem bits, for the lighting keys' headers
    // (ui-design.md, "Where the definition says it"): from Vial's or VIA V2's `lighting`, VIA V3's
    // keycode modules and standard menus -- what either says the board has -- and LED Matrix,
    // which no definition can declare, from an LM_* key anywhere on the keymap.
    [[nodiscard]] uint8_t LightingSystemsOf(const Keyboard& keyboard);

    // The board's own keycodes as its definition names them (`customKeycodes`), as legend words:
    // the name, its short name as the short form -- a line break in either read as a space, the
    // keycap breaking its lines itself.
    [[nodiscard]] std::vector<Words> CustomKeycodeWordsOf(const Keyboard& keyboard);

    // Which lighting keycodes work on a board's firmware (ui-design.md, "Which lighting keycodes a
    // board gets"): Old -- one set drives every system, the RGB_M modes too -- New -- UG_* and RM_*
    // apart -- or Unknown, the common state, where only what works on both is offered.
    enum class LightingFirmware : uint8_t
    {
        Old,
        New,
        Unknown,
    };

    // By the first rule that answers: VIA 13 or later, new; VIA 9 to 11 or Vial 5, old; Vial 6
    // with Caps Word or Layer Lock, new; VIA 12 with an RM_* key on the keymap, new -- never on
    // Vial, whose app offers RM_* on old firmware too; otherwise unknown. Found again at every
    // load, never stored: a board can be reflashed.
    [[nodiscard]] LightingFirmware LightingFirmwareOf(const Keyboard& keyboard);

    // What hover adds to a lighting key on `keyboard` (ui-design.md, "Which lighting keycodes a
    // board gets"): that it may do nothing on the board's firmware -- RM_* unless the firmware is
    // known to be new, RGB_M_* unless known to be old, or a mode only an underglow has, on a
    // board with an RGB Matrix alone -- or, for UG_* on a board with underglow and RGB Matrix,
    // that it drives both. Empty text for any other key.
    struct LightingNote
    {
        std::string text;
        bool        mayDoNothing = false;   // the picker marks the tile
    };

    [[nodiscard]] LightingNote LightingNoteOf(const Keycode& keycode, const Keyboard& keyboard);

    // Where a board's left half ends, in key units along x: the space bar's centre -- its
    // widest key, 3u or more -- or the board's own where there is none, on splits and
    // orthos. A full-size board's own centre falls near Backspace, because of the numpad, and
    // would put right Alt on the left (ui-design.md, "Names of the modifiers").
    [[nodiscard]] float SideLine(const std::vector<BoardKey>& keys);

    // The side of `line` a key sits on, by its centre; neither when it straddles the line.
    [[nodiscard]] KeySide SideOf(const DefinitionKey& key, float line);

    // A keycap's colour class from `base`, what the key does on layer 0, whichever layer is
    // shown -- a physical cap keeps its colour. Alpha: the character keys, the numpad's digits
    // and dot, the space bar. Accent: Esc, Enter, the numpad's Enter. Modifier: everything
    // else, F-keys and commands included. A tap-hold takes its tap's class; Shift with a
    // character key is still a character. KC_NO and KC_TRNS say nothing, so the key's width
    // decides: a modifier past 1.25u, else an alpha.
    [[nodiscard]] KeyFill KeycapClassOf(const Keycode& base, const DefinitionKey& key);

    // A key's centre in key units, where it is drawn -- its rotation applied. An L-shaped
    // key's is its first rectangle's.
    [[nodiscard]] std::pair<float, float> KeyCentre(const DefinitionKey& key);

    // Where labels wanted at `wanted` along one edge go, each `extents` long: as wanted, but
    // pushed on just enough that none overlaps the one before it, `gap` apart. Labels keep
    // the order of their wanted positions; the result is by input index.
    [[nodiscard]] std::vector<float> SpreadApart(const std::vector<float>& wanted,
                                                 const std::vector<float>& extents,
                                                 float                     gap);
}
