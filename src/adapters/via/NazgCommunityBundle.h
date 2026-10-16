// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The community definitions, bundled with Nazg beside VIA's official ones.
//
// VIA definitions vendors publish themselves for boards VIA's registry lacks or has older,
// gathered in the nazg-community-definitions repository and shipped as one asset of its releases
// (via-registry.md, "Sharing: three levels" and "Decisions to take"). The bundle is a
// single-block XZ stream of a tar, as VIA's is (adapters/via/NazgViaBundle.h): every definition
// under its path in that repository -- boards/<publisher>/<board>/<file> -- byte for byte, an
// index.json naming each one's board, VID:PID, publisher, licence and status, and a
// manifest.json naming the release. 0.1 MB for some 700 boards, inflated once at start and
// kept, as VIA's is.
//
// Unlike VIA's, several definitions may share a VID:PID: vendors' made-up ids collide, and a
// board can come as a hotswap and a solder PCB. Nazg asks which one, once, and remembers
// (library/NazgDefinitionChoice.h).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nazg::via
{
    // One definition, as the bundle's index lists it.
    struct CommunityDefinition
    {
        std::string path;        // in the bundle, and in the nazg-community-definitions repository
        std::string name;        // the board's, as its definition names it
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;
        std::string publisher;   // who publishes the file: "GEONWORKS", "Keychron"
        std::string licence;     // an SPDX identifier, or why it is redistributed without one
        std::string status;      // "unverified" -- from the publisher's file -- or "verified"
    };

    // What nazg-community-definitions' tools/build_bundle.py records in the bundle's
    // manifest.json.
    struct CommunityBundleManifest
    {
        std::string release;   // the tag of the nazg-community-definitions release
        int         definitions = 0;
    };

    class CommunityDefinitionBundle
    {
    public:
        // Inflates the bundle's bytes and reads its index. Throws ProtocolError if the bundle
        // is corrupt or has no index; an index entry whose file is missing is left out.
        explicit CommunityDefinitionBundle(const std::vector<uint8_t>& bundle);

        // Every definition for a VID:PID, in the index's order; empty when there is none.
        [[nodiscard]] std::vector<const CommunityDefinition*> Find(uint16_t vendorId, uint16_t productId) const;

        // A definition by its path; null when the bundle has none -- a choice made with an
        // older bundle, say.
        [[nodiscard]] const CommunityDefinition* Entry(const std::string& path) const;

        // A definition's bytes, for ParseDefinition(); nullopt when the bundle has none.
        [[nodiscard]] std::optional<std::vector<uint8_t>> Bytes(const std::string& path) const;

        [[nodiscard]] const std::vector<CommunityDefinition>& Definitions() const noexcept { return m_Definitions; }

        // What manifest.json says; nullopt for a bundle without one, or with one that does
        // not read.
        [[nodiscard]] const std::optional<CommunityBundleManifest>& Manifest() const noexcept { return m_Manifest; }

    private:
        struct Span
        {
            size_t offset = 0;
            size_t size   = 0;
        };

        std::vector<uint8_t>                  m_Tar;   // the whole bundle, inflated
        std::unordered_map<std::string, Span> m_Files;
        std::vector<CommunityDefinition>      m_Definitions;
        std::optional<CommunityBundleManifest> m_Manifest;
    };
}
