// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The keyboard definition: VIA's JSON format, describing what a board physically is.
//
// This is shared, not Vial's. The same document -- name, USB ids, matrix dimensions,
// and a KLE keymap whose labels carry the matrix cell -- is what VIA fetches from its
// registry or a side-loaded file, and what Vial embeds in the firmware. Only the
// SOURCING differs, and that is the expensive part for VIA and near-free for Vial (see
// docs/research_material/client-architecture.md, "The real cost of generic support").
//
//     Vial : device -> XZ -> this parser        (adapters/vial/NazgVialDefinition.h)
//     VIA  : registry or file -> this parser    (sourcing still to be built)
//
// Two forms of the same document are read. The SOURCE form carries a KLE keymap in
// `layouts.keymap` -- what Vial embeds and what a vendor's via.json holds. The CONVERTED
// form is what VIA's build serves from its registry: the KLE already parsed into
// `layouts.keys` and `layouts.optionKeys`, layout options already aligned. Both end in
// the same DefinitionKey list; the conversion is never written anywhere (see
// docs/research_material/via-registry.md, "Why VIA converts").
//
// The matrix cell on each key is the reason any of this is needed: the protocol
// addresses keys as (layer, row, column), and only the definition says which key on the
// board that is.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nazg
{
    // One key, as the definition describes it. Positions and sizes are in KLE units,
    // where 1.0 is a standard key -- the renderer scales them, so they stay as authored.
    struct DefinitionKey
    {
        float x      = 0.0f;
        float y      = 0.0f;
        float width  = 1.0f;
        float height = 1.0f;

        // The second rectangle of an L-shaped key (ISO Enter, big-ass Enter). Only
        // meaningful when secondWidth and secondHeight are both non-zero.
        float secondX      = 0.0f;
        float secondY      = 0.0f;
        float secondWidth  = 0.0f;
        float secondHeight = 0.0f;

        // KLE's rotation: `rotation` degrees clockwise about (rotationX, rotationY), in the
        // same key units. x and y stay the position BEFORE rotating -- how both forms
        // store it, and what lining up layout options works on. Ortho splits and
        // Alice-style boards rely on it: 214 of VIA's 2029 V3 boards.
        float rotation  = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;

        uint8_t row    = 0;
        uint8_t column = 0;

        // Layout options this key belongs to, from the fourth KLE label. -1 means the
        // key is always present; otherwise it appears only when layout `layoutIndex`
        // is set to `layoutOption`.
        int layoutIndex  = -1;
        int layoutOption = -1;

        // A decal is a blank: it takes space and counts when layout options are lined up
        // -- a choice's top-left spot is often one -- but it is not a switch and is not
        // drawn. Its row and column mean nothing.
        bool decal = false;

        // A rotary encoder's knob: its index, -1 on any other key. VIA gives a knob one key,
        // turned both ways -- encoderDirection -1 -- with its push switch's matrix cell in row
        // and column when it has one (encoderPush). Vial gives it one key per direction,
        // encoderDirection 0 counter-clockwise and 1 clockwise, and never a cell: its push
        // switch, if any, is a key of its own.
        int  encoderIndex     = -1;
        int  encoderDirection = -1;
        bool encoderPush      = false;

        bool HasSecondRectangle() const noexcept
        {
            return secondWidth != 0.0f && secondHeight != 0.0f;
        }

        // A switch of the matrix, at row and column: not a decal, nor a knob without a push
        // switch.
        [[nodiscard]] bool IsSwitch() const noexcept
        {
            return !decal && (encoderIndex < 0 || encoderPush);
        }
    };

    // A board's own menu, as VIA V3's `menus` describe it (@the-via/reader's menu-types; wire in
    // via-vial-commands.md, "A board's own menus on the wire"): submenus of lines, each line a
    // label and a control bound to a value of the custom value commands. Slices -- entries with
    // content and no label -- only group entries under a condition: they are flattened, their
    // condition handed down.
    //
    // A condition is VIA's `showIf`, kept as written: "{id_dks_enabled} == 1". All of an entry's
    // must hold for it to show -- its own and those of the slices around it.

    // A slider kept beyond another (VIA's range `constraints`): `operation` the other's value
    // plus `offset` -- pushing the other along when `push`, else stopping there.
    struct RangeConstraint
    {
        enum class Operation : uint8_t
        {
            Less,
            LessOrEqual,
            Greater,
            GreaterOrEqual,
        };

        Operation   operation = Operation::GreaterOrEqual;
        std::string reference;   // the other control's value name
        int         offset = 0;
        bool        push   = false;
    };

    struct CustomControl
    {
        enum class Type : uint8_t
        {
            Range,      // a slider, minimum to maximum
            Dropdown,   // one of `options`
            Toggle,     // `off` or `on`
            Keycode,    // two bytes, big-endian
            Colour,     // hue, then saturation
            Button,     // sends `on`
            Label,      // a fixed `text`, or -- bound -- a string the board sends
        };

        Type                     type = Type::Label;
        std::string              label;
        std::vector<std::string> conditions;

        // The value it is bound to -- [name, channel, value id, extra...] -- the extra bytes sent
        // after the value id, before the value. Not bound: a fixed label.
        bool                 isBound = false;
        std::string          name;   // "id_dks_threshold[0][1]", what conditions call it
        uint8_t              channel = 0;
        uint8_t              valueId = 0;
        std::vector<uint8_t> extra;

        int                     minimum = 0;     // Range
        int                     maximum = 255;
        std::string             unit;            // "mins"
        std::vector<RangeConstraint> constraints;

        std::vector<std::pair<std::string, uint8_t>> options;   // Dropdown: each name and its value

        std::vector<uint8_t> off{ 0 };   // Toggle; a Button sends `on`
        std::vector<uint8_t> on{ 1 };

        std::string text;   // a fixed Label's
    };

    struct CustomSubmenu
    {
        std::string                label;   // "DKS 0 - DKS(0)"
        std::vector<std::string>   conditions;
        std::vector<CustomControl> controls;
    };

    struct CustomMenu
    {
        std::string                label;   // "DKS"; empty when the definition's is not text
        std::vector<std::string>   conditions;
        std::vector<CustomSubmenu> submenus;
    };

    struct KeyboardDefinition
    {
        // A dynamic name -- an object whose value the board picks -- gives its first
        // option; reading the board's choice is not done yet.
        std::string name;
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;

        // What the definition says of the board's lighting, kept verbatim: VIA V2's and Vial's
        // `lighting` preset -- the one it extends, when given as an object -- and VIA V3's
        // `keycodes` modules and standard `menus` -- the ids.
        std::string              lighting;
        std::vector<std::string> keycodeModules;
        std::vector<std::string> menuIds;

        // VIA V3's custom menus -- the `menus` entries that are objects -- in the definition's
        // order: each a section of its own in the column (ui-design.md, "Nazg's sections, then
        // the board's menus", "A board's own menus").
        std::vector<CustomMenu> customMenus;

        // The board's own keycodes, named: `customKeycodes`, the first on QK_KB_0, the next on
        // QK_KB_1... -- VIA's CUSTOM(n), what Vial calls USER(n). Kept verbatim; a field the
        // author left out is empty.
        struct CustomKeycode
        {
            std::string name;        // "Mission Control"
            std::string title;       // what it does, for hover: "Mission Control in macOS"
            std::string shortName;   // for a small key: "MCtrl"
        };
        std::vector<CustomKeycode> customKeycodes;

        uint8_t matrixRows    = 0;
        uint8_t matrixColumns = 0;

        std::vector<DefinitionKey> keys;

        // Labels for the layout options, as the definition authored them. How they
        // nest is VIA's business and is kept verbatim rather than interpreted here.
        std::vector<std::string> layoutLabels;
    };

    // Either form; a document with `layouts.keymap` is read as the source form. Throws
    // ProtocolError on anything malformed. A definition that cannot be read is a device
    // or registry problem, reported the same way as a bad reply.
    [[nodiscard]] KeyboardDefinition ParseDefinition(const std::vector<uint8_t>& json);
}
