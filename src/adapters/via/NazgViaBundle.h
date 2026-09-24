// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VIA's official definitions, bundled with Nazg.
//
// The bundle is one solid XZ stream of a tar holding v3/<id>.json and v2/<id>.json and a
// manifest.json. tools/update_via_bundle.py builds it from github.com/the-via/keyboards
// at the commit pinned in resources/via-keyboards.commit, the files as the repository has
// them -- the source form; a bundle of VIA's converted files reads the same. 0.3 MB for all
// of VIA's boards. See docs/research_material/via-registry.md, "Official VIA definitions:
// bundle, refresh on request" and "Decoding it".
//
// Nothing is kept: a lookup inflates the whole bundle -- 35 MB, 37 ms on a fast desktop
// and a few hundred on a slow laptop -- copies out one file and frees the rest. Once per
// board connect, off the frame loop, that costs less than keeping it.
//
// What comes back is the definition's bytes, for ParseDefinition(); a bundle that has no
// file for a board is an ordinary answer, not an error.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nazg
{
    // Where VIA keeps a board's definition: named vendorId * 65536 + productId in decimal,
    // under v3/ for VIA protocol 11 and later and v2/ below -- the choice VIA's app makes
    // (devicesThunks.ts).
    [[nodiscard]] std::string ViaDefinitionPath(uint16_t vendorId, uint16_t productId, uint16_t viaProtocol);

    // Walks the regular files of an uncompressed ustar archive in order, handing each
    // one's path and bytes to `visit` until it returns false. A leading "./" on a name is
    // dropped. Throws ProtocolError on a malformed archive -- a bad header checksum, a
    // size that runs past the end.
    void ForEachTarFile(const std::vector<uint8_t>&                                       tar,
                        const std::function<bool(const std::string&, const uint8_t*, size_t)>& visit);

    // One regular file out of an uncompressed ustar archive, by its path; nullopt when the
    // archive has none. Throws as ForEachTarFile() does.
    [[nodiscard]] std::optional<std::vector<uint8_t>> ExtractTarFile(const std::vector<uint8_t>& tar,
                                                                     const std::string&          path);

    // What tools/update_via_bundle.py records in the bundle's manifest.json.
    struct ViaBundleManifest
    {
        std::string commit;   // of github.com/the-via/keyboards
        int         v2 = 0;   // how many definitions of each version
        int         v3 = 0;
    };

    // The bundle's manifest; nullopt when it has none or cannot read it. Inflates the whole
    // bundle, like a lookup. Throws ProtocolError if the bundle itself is corrupt.
    [[nodiscard]] std::optional<ViaBundleManifest> ReadViaBundleManifest(const std::vector<uint8_t>& bundle);

    // A board's definition out of the bundle's bytes; nullopt when VIA has none for it.
    // Throws ProtocolError if the bundle itself is corrupt.
    [[nodiscard]] std::optional<std::vector<uint8_t>> FindViaDefinition(const std::vector<uint8_t>& bundle,
                                                                        uint16_t                    vendorId,
                                                                        uint16_t                    productId,
                                                                        uint16_t                    viaProtocol);
}
