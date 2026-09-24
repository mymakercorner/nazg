// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgXz.h"

#include <limits>

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
    std::vector<uint8_t> DecompressXz(const std::vector<uint8_t>& compressed, uint32_t maxSize, const std::string& what)
    {
        if (compressed.empty())
            throw ProtocolError(what + " is empty");

        if (compressed.size() > std::numeric_limits<uint32_t>::max())
            throw ProtocolError(what + " is too large to decode");

        // minlzma decodes in two passes: with a null output buffer and a size of zero
        // it only reports how much room the result needs.
        uint32_t decompressedSize = 0;
        if (!XzDecode(compressed.data(), static_cast<uint32_t>(compressed.size()), nullptr, &decompressedSize))
            throw ProtocolError(what + " is not a valid XZ stream");

        if (decompressedSize == 0)
            throw ProtocolError(what + " decompresses to nothing");

        if (decompressedSize > maxSize)
            throw ProtocolError(what + " claims to decompress to " + std::to_string(decompressedSize) +
                                " bytes; refusing it");

        std::vector<uint8_t> decompressed(decompressedSize);
        uint32_t             outputSize = decompressedSize;

        if (!XzDecode(compressed.data(), static_cast<uint32_t>(compressed.size()), decompressed.data(), &outputSize))
            throw ProtocolError(XzChecksumError() ? what + " failed its checksum" : what + " could not be decompressed");

        decompressed.resize(outputSize);
        return decompressed;
    }
}
