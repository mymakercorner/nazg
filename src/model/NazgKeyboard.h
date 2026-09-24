// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Keyboard - what a connected board IS, with no protocol in sight.
//
// Everything here is a plain value built from bytes someone else fetched: no transport,
// no coroutines, no device handle. The waiting happens in the loader above
// (adapters/vial/NazgVialLoader.h); this layer is pure, so it tests with literals.
//
// A Keyboard does not own its device and cannot be stale in a dangerous way. When a
// board is unplugged the value simply stops being refreshed -- device lifetime is the
// transport's problem, not the model's.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "model/NazgKeycode.h"

namespace nazg
{
    // The keycodes of every matrix cell, on every layer.
    //
    // Flat rather than nested: a vector of vectors can go ragged -- row 3 with 18 cells
    // and row 4 with 17 -- and nothing would stop it. One buffer with known extents
    // cannot, and the index arithmetic lives in IndexOf() instead of at every call
    // site. At this size (3 x 8 x 18 = 432 keycodes) it is not a performance question.
    //
    // Cells, not keys: the protocol addresses (layer, row, column), and a board has
    // more cells than keys -- 144 against 129 on a Model F B104. Storing per key would
    // silently drop whatever lives in the unused cells on a read-modify-write.
    //
    // Cells hold Keycode, never a raw value: what a value means depends on the board's
    // keycode version, so the adapter decodes on the way in (adapters/via/NazgViaKeymap.h)
    // and encodes on the way out.
    class Keymap
    {
    public:
        Keymap() = default;

        // Every cell starts as KC_NO.
        Keymap(uint8_t layers, uint8_t rows, uint8_t columns);

        const Keycode& At(uint8_t layer, uint8_t row, uint8_t column) const;
        void           Set(uint8_t layer, uint8_t row, uint8_t column, Keycode keycode);

        bool Contains(uint8_t layer, uint8_t row, uint8_t column) const noexcept;

        uint8_t Layers()  const noexcept { return m_Layers; }
        uint8_t Rows()    const noexcept { return m_Rows; }
        uint8_t Columns() const noexcept { return m_Columns; }
        bool    IsEmpty() const noexcept { return m_Keycodes.empty(); }

    private:
        size_t IndexOf(uint8_t layer, uint8_t row, uint8_t column) const;

        std::vector<Keycode> m_Keycodes;
        uint8_t              m_Layers  = 0;
        uint8_t              m_Rows    = 0;
        uint8_t              m_Columns = 0;
    };

    // One layout option group, as the definition's labels describe it.
    struct LayoutOptionGroup
    {
        std::string              name;
        std::vector<std::string> options;   // empty for a plain on/off toggle

        // A toggle has two states; a named choice has one per option.
        size_t Count() const noexcept { return options.empty() ? 2 : options.size(); }
    };

    struct Keyboard
    {
        KeyboardDefinition definition;
        Keymap             keymap;

        // The raw value from id_layout_options, and the per-group selection decoded
        // from it. Both are kept: the raw one is what goes back to the device.
        uint32_t             layoutOptions = 0;
        std::vector<uint8_t> layoutSelection;

        // The QMK keycode version the keymap was decoded with. Writing a key back must
        // encode with the same one, and a keycode picker lists what it offers.
        QmkKeycodeVersion keycodeVersion = c_LatestQmkKeycodeVersion;

        const std::string& Name() const noexcept { return definition.name; }

        // Is this key part of the currently selected layout? Keys with no layout
        // option are always shown; the alternatives to them are not.
        [[nodiscard]] bool IsKeyVisible(const DefinitionKey& key) const noexcept;

        // KC_NO for a key whose cell is outside the keymap.
        [[nodiscard]] Keycode KeycodeFor(const DefinitionKey& key, uint8_t layer) const;
    };

    // The label array mixes plain strings (toggles) with arrays whose first element is
    // the group name and whose rest are the option names.
    [[nodiscard]] std::vector<LayoutOptionGroup> ParseLayoutGroups(const std::vector<std::string>& labels);

    // Unpack id_layout_options into one selected index per group.
    //
    // Each group takes as many bits as its option count needs, and the FIRST group
    // occupies the most significant bits. Verified on hardware 2026-09-22: a Model F
    // with six one-bit groups had "Split Backspace" -- group 0 -- switched on in the
    // Vial GUI and reported 0x00000020, which is bit 5. The opposite packing would
    // have made that value mean the last group instead.
    [[nodiscard]] std::vector<uint8_t> DecodeLayoutOptions(uint32_t                        raw,
                                                           const std::vector<LayoutOptionGroup>& groups);

    // The keys of the selected layout, where they are drawn -- decals included, for the
    // space they take; a caller skips them when drawing. The always-present keys stay
    // where the definition put them; each option group's selected choice moves so that
    // its pivot lands on choice 0's, as a source definition draws its alternatives
    // beside the board.
    //
    // VIA's rule, from its reader (kle-parser.ts, extractGroups): a choice's pivot is its
    // topmost key, the leftmost of those, decals counted -- and that key's second
    // rectangle's corner when it sticks out up or left. On the converted form the choices
    // are already lined up, so every shift comes out zero. Like VIA's, the shift moves x
    // and y but not a rotated key's rotation origin; definitions are tuned to VIA.
    //
    // `selection` holds one choice per group; a key of a group beyond it is kept unmoved.
    [[nodiscard]] std::vector<DefinitionKey> PlaceKeys(const KeyboardDefinition&   definition,
                                                       const std::vector<uint8_t>& selection);

    // Assemble the model. Pure: the caller has already done the talking and decoded the
    // keymap. Throws std::invalid_argument if the keymap's matrix is not the one the
    // definition declares.
    [[nodiscard]] Keyboard BuildKeyboard(KeyboardDefinition definition,
                                         Keymap             keymap,
                                         uint32_t           layoutOptions,
                                         QmkKeycodeVersion  keycodeVersion);
}
