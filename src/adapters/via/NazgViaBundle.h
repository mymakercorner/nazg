// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VIA's official definitions, bundled with Nazg.
//
// The bundle is one solid XZ stream of a tar holding VIA's built output, as usevia.app
// serves it: v3/<id>.json and v2/<id>.json in the converted form, plus the index files.
// 0.4 MB for all of VIA's boards. See docs/research_material/via-registry.md, "Official
// VIA definitions: bundle, refresh on request" and "Decoding it".
//
// Nothing is kept: a lookup inflates the whole bundle -- 35 MB, 37 ms on a fast desktop
// and a few hundred on a slow laptop -- copies out one file and frees the rest. Once per
// board connect, off the frame loop, that costs less than keeping it.
//
// What comes back is the definition's bytes, for ParseDefinition(); a bundle that has no
// file for a board is an ordinary answer, not an error.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nazg
{
    // Where VIA keeps a board's definition: named vendorId * 65536 + productId in decimal,
    // under v3/ for VIA protocol 11 and later and v2/ below -- the choice VIA's app makes
    // (devicesThunks.ts).
    [[nodiscard]] std::string ViaDefinitionPath(uint16_t vendorId, uint16_t productId, uint16_t viaProtocol);

    // One regular file out of an uncompressed ustar archive, by its path; nullopt when the
    // archive has none. A leading "./" on the archive's names is ignored. Throws
    // ProtocolError on a malformed archive -- a bad header checksum, a size that runs
    // past the end.
    [[nodiscard]] std::optional<std::vector<uint8_t>> ExtractTarFile(const std::vector<uint8_t>& tar,
                                                                     const std::string&          path);

    // A board's definition out of the bundle's bytes; nullopt when VIA has none for it.
    // Throws ProtocolError if the bundle itself is corrupt.
    [[nodiscard]] std::optional<std::vector<uint8_t>> FindViaDefinition(const std::vector<uint8_t>& bundle,
                                                                        uint16_t                    vendorId,
                                                                        uint16_t                    productId,
                                                                        uint16_t                    viaProtocol);
}
