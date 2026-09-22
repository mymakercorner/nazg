// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialDefinition.h"

#include <string>

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
        // anything a keyboard could hold, so this only guards against a corrupt header
        // asking for an absurd allocation.
        constexpr uint32_t c_MaxDecompressedSize = 1024 * 1024;
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

    KeyboardDefinition DecodeDefinition(const std::vector<uint8_t>& compressed)
    {
        return ParseDefinition(DecompressDefinition(compressed));
    }
}
