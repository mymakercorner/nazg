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

#include "adapters/via/NazgKeyboardDefinition.h"

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
    class Keymap
    {
    public:
        Keymap() = default;
        Keymap(uint8_t layers, uint8_t rows, uint8_t columns);

        uint16_t At(uint8_t layer, uint8_t row, uint8_t column) const;
        void     Set(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);

        bool Contains(uint8_t layer, uint8_t row, uint8_t column) const noexcept;

        uint8_t Layers()  const noexcept { return m_Layers; }
        uint8_t Rows()    const noexcept { return m_Rows; }
        uint8_t Columns() const noexcept { return m_Columns; }
        bool    IsEmpty() const noexcept { return m_Keycodes.empty(); }

        // How many bytes the device sends for a keymap of this shape: two per cell.
        [[nodiscard]] static size_t ByteCount(uint8_t layers, uint8_t rows, uint8_t columns) noexcept;

        // Decode the buffer exactly as dynamic_keymap_get_buffer returns it: keycodes
        // big-endian, cells in row-major order, layers one after another. Throws
        // std::invalid_argument if the buffer is the wrong size for the shape.
        [[nodiscard]] static Keymap FromBuffer(const std::vector<uint8_t>& bytes,
                                               uint8_t layers, uint8_t rows, uint8_t columns);

    private:
        size_t IndexOf(uint8_t layer, uint8_t row, uint8_t column) const;

        std::vector<uint16_t> m_Keycodes;
        uint8_t               m_Layers  = 0;
        uint8_t               m_Rows    = 0;
        uint8_t               m_Columns = 0;
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

        const std::string& Name() const noexcept { return definition.name; }

        // Is this key part of the currently selected layout? Keys with no layout
        // option are always shown; the alternatives to them are not.
        [[nodiscard]] bool IsKeyVisible(const DefinitionKey& key) const noexcept;

        [[nodiscard]] uint16_t KeycodeFor(const DefinitionKey& key, uint8_t layer) const;
    };

    // The label array mixes plain strings (toggles) with arrays whose first element is
    // the group name and whose rest are the option names.
    [[nodiscard]] std::vector<LayoutOptionGroup> ParseLayoutGroups(const std::vector<std::string>& labels);

    // Unpack id_layout_options into one selected index per group.
    //
    // NOT yet verified against hardware: VIA packs each group into as many bits as its
    // option count needs, most significant group first, and that ordering is taken from
    // reading the format rather than from watching a board change. Everything else in
    // this file has been checked against a real definition.
    [[nodiscard]] std::vector<uint8_t> DecodeLayoutOptions(uint32_t                        raw,
                                                           const std::vector<LayoutOptionGroup>& groups);

    // Assemble the model. Pure: the caller has already done the talking.
    [[nodiscard]] Keyboard BuildKeyboard(KeyboardDefinition          definition,
                                         const std::vector<uint8_t>& keymapBytes,
                                         uint8_t                     layers,
                                         uint32_t                    layoutOptions);
}
