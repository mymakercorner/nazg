// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Reading integers out of a report, in both byte orders.
//
// Both are needed on the SAME device: VIA's scalars are big-endian, Vial's own -- the
// protocol version, the definition size, a page index, a settings id -- are
// little-endian, because those commands copy bytes straight out of EEPROM. Keycodes
// stay big-endian even in Vial commands, since they come from the shared dynamic keymap
// code. Getting this backwards is the easiest mistake in the whole adapter, so the byte
// order is in the function name at every call site.
//
// This lives outside adapters/ because it is mechanics, not semantics: any protocol
// that parses bytes wants it, and XAP later should not have to include the VIA adapter
// to get it. See docs/research_material/client-architecture.md, "Sharing between
// protocols: union, not intersection".
//
// The reports these read from are fixed-size and the protocol layer checks that size
// before parsing, so a short buffer here is a bug in this code rather than bad input
// from a device -- hence the assert rather than an exception.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nazg
{
    [[nodiscard]] inline uint16_t ReadBigEndian16(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 2 <= bytes.size() && "ReadBigEndian16 past the end of the report");

        return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                      static_cast<uint16_t>(bytes[offset + 1]));
    }

    [[nodiscard]] inline uint32_t ReadBigEndian32(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 4 <= bytes.size() && "ReadBigEndian32 past the end of the report");

        return (static_cast<uint32_t>(bytes[offset])     << 24) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 2]) <<  8) |
               (static_cast<uint32_t>(bytes[offset + 3]));
    }

    [[nodiscard]] inline uint16_t ReadLittleEndian16(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 2 <= bytes.size() && "ReadLittleEndian16 past the end of the report");

        return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset + 1]) << 8) |
                                      static_cast<uint16_t>(bytes[offset]));
    }

    [[nodiscard]] inline uint32_t ReadLittleEndian32(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 4 <= bytes.size() && "ReadLittleEndian32 past the end of the report");

        return (static_cast<uint32_t>(bytes[offset])          ) |
               (static_cast<uint32_t>(bytes[offset + 1]) <<  8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    [[nodiscard]] inline uint64_t ReadLittleEndian64(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 8 <= bytes.size() && "ReadLittleEndian64 past the end of the report");

        uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i)
            value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);

        return value;
    }
}
