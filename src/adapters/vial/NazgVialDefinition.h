// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Vial's half of the definition: getting the bytes and inflating them.
//
// The document inside is an ordinary VIA keyboard definition, parsed by the shared
// adapters/via/NazgKeyboardDefinition.h. What is Vial-specific is only that the board
// carries it in flash, XZ-compressed, and hands it over 32 bytes at a time:
//
//     auto definition = DecodeDefinition(co_await protocol.DownloadDefinition());
//
// That is the idea the survey calls the most important one -- the board describes
// itself, so there is no registry to consult and no definition/firmware skew. It is
// also why Vial is the cheap backend and VIA the expensive one: VIA has to solve
// sourcing, and this is Vial's entire answer to it.
//
// Verified on hardware: the payload is a single-block XZ stream starting FD 37 7A 58
// 5A 00, which is exactly what minlzma decodes -- no wrapper, no full LZMA SDK.

#pragma once

#include <cstdint>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

namespace nazg
{
    // Inflate the XZ stream. Throws ProtocolError if it is not a valid stream, if the
    // checksum fails, or if it claims an implausible size.
    [[nodiscard]] std::vector<uint8_t> DecompressDefinition(const std::vector<uint8_t>& compressed);

    // Inflate and parse, which is what a caller almost always wants.
    [[nodiscard]] KeyboardDefinition DecodeDefinition(const std::vector<uint8_t>& compressed);
}
