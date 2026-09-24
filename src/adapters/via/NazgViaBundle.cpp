// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaBundle.h"

#include <algorithm>
#include <cstring>

#include "adapters/NazgXz.h"
#include "adapters/via/NazgViaProtocol.h"   // ProtocolError

namespace nazg
{
    namespace
    {
        // The bundle inflates to 35 MB (2026-09-24). Room to grow several times over,
        // while still refusing a corrupt header's absurd size.
        constexpr uint32_t c_MaxBundleSize = 256u * 1024u * 1024u;

        // The ustar header: 512 bytes, text fields at fixed offsets.
        constexpr size_t c_Block          = 512;
        constexpr size_t c_NameOffset     = 0;
        constexpr size_t c_NameLength     = 100;
        constexpr size_t c_SizeOffset     = 124;
        constexpr size_t c_SizeLength     = 12;
        constexpr size_t c_ChecksumOffset = 148;
        constexpr size_t c_ChecksumLength = 8;
        constexpr size_t c_TypeOffset     = 156;
        constexpr size_t c_MagicOffset    = 257;
        constexpr size_t c_PrefixOffset   = 345;
        constexpr size_t c_PrefixLength   = 155;

        // A NUL-terminated field that may fill its whole width.
        std::string ReadField(const uint8_t* header, size_t offset, size_t length)
        {
            const uint8_t* begin = header + offset;
            const uint8_t* end   = std::find(begin, begin + length, uint8_t{ 0 });
            return std::string(begin, end);
        }

        // Octal digits, padded with spaces or NULs on either side. False on anything else --
        // including GNU's base-256 form for sizes past 8 GB, which no bundle needs.
        bool ReadOctal(const uint8_t* header, size_t offset, size_t length, uint64_t& value)
        {
            value            = 0;
            bool seenDigit   = false;
            bool seenTrailer = false;

            for (size_t i = 0; i < length; ++i)
            {
                const uint8_t c = header[offset + i];
                if (c >= '0' && c <= '7')
                {
                    if (seenTrailer)
                        return false;
                    value     = value * 8 + (c - '0');
                    seenDigit = true;
                }
                else if (c == ' ' || c == 0)
                {
                    seenTrailer = seenTrailer || seenDigit;
                }
                else
                {
                    return false;
                }
            }
            return seenDigit;
        }

        // The header's checksum: the sum of its bytes, the checksum field counted as spaces.
        bool ChecksumMatches(const uint8_t* header)
        {
            uint64_t stored = 0;
            if (!ReadOctal(header, c_ChecksumOffset, c_ChecksumLength, stored))
                return false;

            uint64_t sum = 0;
            for (size_t i = 0; i < c_Block; ++i)
                sum += (i >= c_ChecksumOffset && i < c_ChecksumOffset + c_ChecksumLength) ? ' ' : header[i];

            return sum == stored;
        }

        bool IsZeroBlock(const uint8_t* block)
        {
            for (size_t i = 0; i < c_Block; ++i)
                if (block[i] != 0)
                    return false;
            return true;
        }

        std::string WithoutDotSlash(std::string path)
        {
            while (path.size() > 2 && path[0] == '.' && path[1] == '/')
                path.erase(0, 2);
            return path;
        }
    }

    std::string ViaDefinitionPath(uint16_t vendorId, uint16_t productId, uint16_t viaProtocol)
    {
        const uint32_t id = (static_cast<uint32_t>(vendorId) << 16) | productId;
        return std::string(viaProtocol >= 11 ? "v3/" : "v2/") + std::to_string(id) + ".json";
    }

    std::optional<std::vector<uint8_t>> ExtractTarFile(const std::vector<uint8_t>& tar, const std::string& path)
    {
        const std::string wanted = WithoutDotSlash(path);

        size_t offset = 0;
        while (offset + c_Block <= tar.size())
        {
            const uint8_t* header = tar.data() + offset;

            // The archive ends with zero blocks.
            if (IsZeroBlock(header))
                return std::nullopt;

            if (std::memcmp(header + c_MagicOffset, "ustar", 5) != 0)
                throw ProtocolError("the bundle is not a ustar archive");

            if (!ChecksumMatches(header))
                throw ProtocolError("the bundle has a damaged header at byte " + std::to_string(offset));

            uint64_t size = 0;
            if (!ReadOctal(header, c_SizeOffset, c_SizeLength, size))
                throw ProtocolError("the bundle has an unreadable size at byte " + std::to_string(offset));

            const size_t dataOffset = offset + c_Block;
            if (size > tar.size() - dataOffset)
                throw ProtocolError("the bundle is truncated at byte " + std::to_string(offset));

            // '0' and NUL are regular files; directories, links and pax headers are skipped.
            const char type = static_cast<char>(header[c_TypeOffset]);
            if (type == '0' || type == 0)
            {
                const std::string prefix = ReadField(header, c_PrefixOffset, c_PrefixLength);
                const std::string name   = ReadField(header, c_NameOffset, c_NameLength);
                const std::string full   = WithoutDotSlash(prefix.empty() ? name : prefix + "/" + name);

                if (full == wanted)
                    return std::vector<uint8_t>(tar.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                                                tar.begin() + static_cast<std::ptrdiff_t>(dataOffset + size));
            }

            // The data is padded to whole blocks.
            const uint64_t padded = (size + c_Block - 1) / c_Block * c_Block;
            if (padded > tar.size() - dataOffset)
                return std::nullopt;   // the last file's padding was cut: nothing follows it
            offset = dataOffset + static_cast<size_t>(padded);
        }

        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> FindViaDefinition(const std::vector<uint8_t>& bundle,
                                                          uint16_t                    vendorId,
                                                          uint16_t                    productId,
                                                          uint16_t                    viaProtocol)
    {
        const std::vector<uint8_t> tar = DecompressXz(bundle, c_MaxBundleSize, "the bundle");
        return ExtractTarFile(tar, ViaDefinitionPath(vendorId, productId, viaProtocol));
    }
}
