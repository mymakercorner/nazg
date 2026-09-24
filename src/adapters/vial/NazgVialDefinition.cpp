// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialDefinition.h"

#include "adapters/NazgXz.h"

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
        return DecompressXz(compressed, c_MaxDecompressedSize, "the definition");
    }

    KeyboardDefinition DecodeDefinition(const std::vector<uint8_t>& compressed)
    {
        return ParseDefinition(DecompressDefinition(compressed));
    }
}
