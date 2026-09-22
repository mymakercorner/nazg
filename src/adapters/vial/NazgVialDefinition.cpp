// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialDefinition.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

// minlzma is C, and its header declares neither its dependencies nor C linkage, so
// both are supplied here.
#include <stdbool.h>
#include <stdint.h>
extern "C"
{
#include "minlzma.h"
}

namespace nazg
{
    namespace
    {
        // The Model F's definition inflates to about 2 KB. A megabyte is far past
        // anything a keyboard could hold, so it only guards against a corrupt header
        // asking for an absurd allocation.
        constexpr uint32_t c_MaxDecompressedSize = 1024 * 1024;

        // VIA definitions write the USB ids as strings ("0x1209"), but some are plain
        // numbers, so both are accepted.
        uint16_t ParseUsbId(const nlohmann::json& value, const char* what)
        {
            if (value.is_number_unsigned())
                return static_cast<uint16_t>(value.get<unsigned>());

            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                try
                {
                    // Base 0 so "0x1209" and "4617" both work.
                    return static_cast<uint16_t>(std::stoul(text, nullptr, 0));
                }
                catch (const std::exception&)
                {
                    throw ProtocolError(std::string("definition has an unreadable ") + what + ": " + text);
                }
            }

            throw ProtocolError(std::string("definition has no usable ") + what);
        }

        // A KLE key label is newline-separated positions. Position 0 carries the matrix
        // cell as "row,col"; position 3, when present, carries "layoutIndex,option".
        // Anything else -- a decal, a legend-only key -- has no position 0 to parse and
        // is skipped by the caller.
        bool ParsePair(const std::string& text, int& first, int& second)
        {
            const size_t comma = text.find(',');
            if (comma == std::string::npos || comma == 0 || comma + 1 >= text.size())
                return false;

            try
            {
                size_t consumed = 0;
                first = std::stoi(text.substr(0, comma), &consumed);
                if (consumed != comma)
                    return false;

                const std::string tail = text.substr(comma + 1);
                second = std::stoi(tail, &consumed);
                if (consumed != tail.size())
                    return false;
            }
            catch (const std::exception&)
            {
                return false;
            }

            return first >= 0 && second >= 0;
        }

        std::vector<std::string> SplitLabels(const std::string& label)
        {
            std::vector<std::string> parts;
            size_t                   start = 0;

            for (;;)
            {
                const size_t newline = label.find('\n', start);
                if (newline == std::string::npos)
                {
                    parts.push_back(label.substr(start));
                    break;
                }

                parts.push_back(label.substr(start, newline - start));
                start = newline + 1;
            }

            return parts;
        }

        float ReadFloat(const nlohmann::json& object, const char* key, float fallback)
        {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_number())
                return fallback;

            return found->get<float>();
        }

        // Walks one KLE row. Property objects apply to the keys that FOLLOW them, and
        // x/y are relative offsets rather than absolute positions -- that is the whole
        // trick of the format, and getting it wrong shifts half a keyboard sideways.
        void ParseKeymapRow(const nlohmann::json& row, float& x, float& y, std::vector<DefinitionKey>& keys)
        {
            float width        = 1.0f;
            float height       = 1.0f;
            float secondX      = 0.0f;
            float secondY      = 0.0f;
            float secondWidth  = 0.0f;
            float secondHeight = 0.0f;

            for (const nlohmann::json& entry : row)
            {
                if (entry.is_object())
                {
                    x += ReadFloat(entry, "x", 0.0f);
                    y += ReadFloat(entry, "y", 0.0f);

                    width        = ReadFloat(entry, "w", 1.0f);
                    height       = ReadFloat(entry, "h", 1.0f);
                    secondX      = ReadFloat(entry, "x2", 0.0f);
                    secondY      = ReadFloat(entry, "y2", 0.0f);
                    secondWidth  = ReadFloat(entry, "w2", 0.0f);
                    secondHeight = ReadFloat(entry, "h2", 0.0f);
                    continue;
                }

                if (!entry.is_string())
                    continue;

                const std::vector<std::string> labels = SplitLabels(entry.get<std::string>());

                int row_ = 0;
                int column = 0;
                if (!labels.empty() && ParsePair(labels[0], row_, column))
                {
                    DefinitionKey key;
                    key.x            = x;
                    key.y            = y;
                    key.width        = width;
                    key.height       = height;
                    key.secondX      = secondX;
                    key.secondY      = secondY;
                    key.secondWidth  = secondWidth;
                    key.secondHeight = secondHeight;
                    key.row          = static_cast<uint8_t>(row_);
                    key.column       = static_cast<uint8_t>(column);

                    int layoutIndex  = 0;
                    int layoutOption = 0;
                    if (labels.size() > 3 && ParsePair(labels[3], layoutIndex, layoutOption))
                    {
                        key.layoutIndex  = layoutIndex;
                        key.layoutOption = layoutOption;
                    }

                    keys.push_back(key);
                }
                // else: a decal or a legend-only key. It occupies space but is not a
                // switch, so it advances x below without being recorded.

                x += width;

                width        = 1.0f;
                height       = 1.0f;
                secondX      = 0.0f;
                secondY      = 0.0f;
                secondWidth  = 0.0f;
                secondHeight = 0.0f;
            }
        }
    }

    std::vector<uint8_t> DecompressDefinition(const std::vector<uint8_t>& compressed)
    {
        if (compressed.empty())
            throw ProtocolError("the device returned an empty definition");

        // minlzma decodes in two passes: with a null output buffer and a size of zero
        // it only reports how much room the result needs.
        uint32_t decompressedSize = 0;
        if (!XzDecode(compressed.data(), static_cast<uint32_t>(compressed.size()),
                      nullptr, &decompressedSize))
        {
            throw ProtocolError("the definition is not a valid XZ stream");
        }

        if (decompressedSize == 0)
            throw ProtocolError("the definition decompresses to nothing");

        if (decompressedSize > c_MaxDecompressedSize)
            throw ProtocolError("the definition claims to decompress to " +
                                std::to_string(decompressedSize) + " bytes; refusing it");

        std::vector<uint8_t> decompressed(decompressedSize);
        uint32_t             outputSize = decompressedSize;

        if (!XzDecode(compressed.data(), static_cast<uint32_t>(compressed.size()),
                      decompressed.data(), &outputSize))
        {
            throw ProtocolError(XzChecksumError()
                                    ? "the definition failed its checksum"
                                    : "the definition could not be decompressed");
        }

        decompressed.resize(outputSize);
        return decompressed;
    }

    VialDefinition ParseDefinition(const std::vector<uint8_t>& json)
    {
        nlohmann::json document;

        try
        {
            document = nlohmann::json::parse(json.begin(), json.end());
        }
        catch (const nlohmann::json::exception& failure)
        {
            throw ProtocolError(std::string("the definition is not valid JSON: ") + failure.what());
        }

        if (!document.is_object())
            throw ProtocolError("the definition is not a JSON object");

        VialDefinition definition;

        if (document.contains("name") && document["name"].is_string())
            definition.name = document["name"].get<std::string>();

        if (document.contains("vendorId"))
            definition.vendorId = ParseUsbId(document["vendorId"], "vendorId");

        if (document.contains("productId"))
            definition.productId = ParseUsbId(document["productId"], "productId");

        if (document.contains("lighting") && document["lighting"].is_string())
            definition.lighting = document["lighting"].get<std::string>();

        const auto matrix = document.find("matrix");
        if (matrix == document.end() || !matrix->is_object())
            throw ProtocolError("the definition has no matrix section");

        definition.matrixRows    = matrix->value("rows", 0);
        definition.matrixColumns = matrix->value("cols", 0);

        if (definition.matrixRows == 0 || definition.matrixColumns == 0)
            throw ProtocolError("the definition declares an empty matrix");

        const auto layouts = document.find("layouts");
        if (layouts == document.end() || !layouts->is_object())
            throw ProtocolError("the definition has no layouts section");

        const auto labels = layouts->find("labels");
        if (labels != layouts->end() && labels->is_array())
        {
            for (const nlohmann::json& label : *labels)
            {
                if (label.is_string())
                    definition.layoutLabels.push_back(label.get<std::string>());
                else
                    definition.layoutLabels.push_back(label.dump());   // nested option group
            }
        }

        const auto keymap = layouts->find("keymap");
        if (keymap == layouts->end() || !keymap->is_array())
            throw ProtocolError("the definition has no keymap");

        float y = 0.0f;
        for (const nlohmann::json& row : *keymap)
        {
            if (!row.is_array())
                continue;

            // x restarts every row; y carries across and advances by one per row.
            float x = 0.0f;
            ParseKeymapRow(row, x, y, definition.keys);
            y += 1.0f;
        }

        if (definition.keys.empty())
            throw ProtocolError("the definition describes no keys");

        return definition;
    }

    VialDefinition DecodeDefinition(const std::vector<uint8_t>& compressed)
    {
        return ParseDefinition(DecompressDefinition(compressed));
    }
}
