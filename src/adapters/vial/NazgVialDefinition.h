// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The Vial keyboard definition: XZ-compressed JSON pulled off the device.
//
// This is the idea the whole survey calls the important one -- the board describes
// itself, so there is no registry to consult and no definition/firmware skew. The bytes
// arrive from VialProtocol::DownloadDefinition(); everything here turns them into data
// the capability model can use.
//
//     auto definition = DecodeDefinition(co_await protocol.DownloadDefinition());
//
// The payload is a single-block XZ stream (verified on hardware: it starts FD 37 7A 58
// 5A 00), which is exactly what minlzma decodes -- no wrapper, no full LZMA SDK.
//
// The JSON inside is VIA's keyboard-definition format: a name, USB ids, matrix
// dimensions, and a KLE keymap whose key labels carry the matrix position. See
// docs/research_material/via-vial-commands.md for where the bytes come from.

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

        // Where this key lives in the switch matrix. This is the whole reason the
        // definition is needed to talk keymaps: VIA addresses keys by (row, column).
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

    struct VialDefinition
    {
        std::string name;
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;
        std::string lighting;

        uint8_t matrixRows    = 0;
        uint8_t matrixColumns = 0;

        std::vector<DefinitionKey> keys;

        // Labels for the layout options, as the definition authored them. The outer
        // vector is one entry per option group; how they nest is VIA's business and is
        // kept verbatim rather than interpreted here.
        std::vector<std::string> layoutLabels;
    };

    // Inflate the XZ stream. Throws ProtocolError if it is not a valid stream, if the
    // checksum fails, or if it claims an implausible size.
    [[nodiscard]] std::vector<uint8_t> DecompressDefinition(const std::vector<uint8_t>& compressed);

    // Parse the decompressed JSON. Throws ProtocolError on anything malformed -- a
    // definition that cannot be read is a device problem, the same as a bad reply.
    [[nodiscard]] VialDefinition ParseDefinition(const std::vector<uint8_t>& json);

    // Both steps, which is what a caller almost always wants.
    [[nodiscard]] VialDefinition DecodeDefinition(const std::vector<uint8_t>& compressed);
}
