// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Inflating an XZ stream, whole, in memory -- through minlzma.
//
// Two callers with nothing else in common: Vial's embedded definition, a couple of KB off
// the board (adapters/vial/NazgVialDefinition.h), and the bundle of VIA's official
// definitions, 0.4 MB inflating to 35 MB (adapters/via/NazgViaBundle.h). Each passes the
// most it will accept, so a corrupt header cannot ask for an absurd allocation.
//
// minlzma's limits apply: a single-block stream, whole in memory, CRC-32, CRC-64 or no
// check. That is what `xz`, 7-Zip and Python's lzma module write by default -- see
// docs/research_material/via-registry.md, "Decoding it". Nazg builds it without
// MINLZ_INTEGRITY_CHECKS, so the block's checksum is not verified.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nazg
{
    // Throws ProtocolError, naming `what` ("the definition", "the bundle"), if the input
    // is empty, is not a valid stream, inflates to nothing or to more than maxSize bytes,
    // or cannot be decoded.
    [[nodiscard]] std::vector<uint8_t> DecompressXz(const std::vector<uint8_t>& compressed,
                                                    uint32_t                    maxSize,
                                                    const std::string&          what);
}
