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
// The matrix cell on each key is the reason any of this is needed: the protocol
// addresses keys as (layer, row, column), and only the definition says which key on the
// board that is.

#pragma once

#include <cstdint>
#include <string>
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

        uint8_t row    = 0;
        uint8_t column = 0;

        // Layout options this key belongs to, from the fourth KLE label. -1 means the
        // key is always present; otherwise it appears only when layout `layoutIndex`
        // is set to `layoutOption`.
        int layoutIndex  = -1;
        int layoutOption = -1;

        bool HasSecondRectangle() const noexcept
        {
            return secondWidth != 0.0f && secondHeight != 0.0f;
        }
    };

    struct KeyboardDefinition
    {
        std::string name;
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;
        std::string lighting;

        uint8_t matrixRows    = 0;
        uint8_t matrixColumns = 0;

        std::vector<DefinitionKey> keys;

        // Labels for the layout options, as the definition authored them. How they
        // nest is VIA's business and is kept verbatim rather than interpreted here.
        std::vector<std::string> layoutLabels;
    };

    // Throws ProtocolError on anything malformed. A definition that cannot be read is
    // a device or registry problem, reported the same way as a bad reply.
    [[nodiscard]] KeyboardDefinition ParseDefinition(const std::vector<uint8_t>& json);
}
