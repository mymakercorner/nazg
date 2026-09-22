// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialProtocol.h"

#include <algorithm>
#include <string>
#include <utility>

namespace nazg
{
    namespace
    {
        // A definition is a few KB. This only exists so a wrong size byte cannot ask
        // for a gigabyte before anything notices.
        constexpr uint32_t c_MaxDefinitionSize = 256 * 1024;

        uint32_t ReadLittleEndian32(const std::vector<uint8_t>& bytes, size_t offset)
        {
            return (static_cast<uint32_t>(bytes[offset])            ) |
                   (static_cast<uint32_t>(bytes[offset + 1]) <<    8) |
                   (static_cast<uint32_t>(bytes[offset + 2]) <<   16) |
                   (static_cast<uint32_t>(bytes[offset + 3]) <<   24);
        }

        uint64_t ReadLittleEndian64(const std::vector<uint8_t>& bytes, size_t offset)
        {
            uint64_t value = 0;
            for (size_t i = 0; i < 8; ++i)
                value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
            return value;
        }

        uint16_t ReadBigEndian16(const std::vector<uint8_t>& bytes, size_t offset)
        {
            return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
        }
    }

    Task<std::vector<uint8_t>> VialProtocol::SendVial(VialCommand                    command,
                                                      std::initializer_list<uint8_t> arguments)
    {
        // [0] prefix, [1] sub-command, arguments from [2] -- which is where the
        // firmware reads them, since it keeps the prefix and sub-command in place.
        std::vector<uint8_t> frame(c_ViaReportSize, 0x00);
        frame[0] = static_cast<uint8_t>(ViaCommand::VialPrefix);
        frame[1] = static_cast<uint8_t>(command);

        size_t index = 2;
        for (uint8_t argument : arguments)
            frame[index++] = argument;

        std::vector<uint8_t> reply = co_await m_Channel.Request(std::move(frame));

        if (reply.size() < c_ViaReportSize)
            throw ProtocolError("short reply to Vial command " +
                                std::to_string(static_cast<int>(command)) + ": " +
                                std::to_string(reply.size()) + " bytes");

        co_return reply;
    }

    Task<std::optional<VialIdentity>> VialProtocol::Detect()
    {
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetKeyboardId, {});

        // A plain VIA board does not know 0xFE and answers with the unhandled marker.
        if (reply[0] == static_cast<uint8_t>(ViaCommand::Unhandled))
            co_return std::nullopt;

        VialIdentity identity;
        identity.protocolVersion = ReadLittleEndian32(reply, 0);
        identity.keyboardUid     = ReadLittleEndian64(reply, 4);
        identity.supportsVialRgb = reply[12] != 0;

        co_return identity;
    }

    Task<uint32_t> VialProtocol::GetDefinitionSize()
    {
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetSize, {});
        co_return ReadLittleEndian32(reply, 0);
    }

    Task<std::vector<uint8_t>> VialProtocol::DownloadDefinition()
    {
        const uint32_t size = co_await GetDefinitionSize();

        if (size > c_MaxDefinitionSize)
            throw ProtocolError("definition size " + std::to_string(size) +
                                " is implausible; refusing to download it");

        std::vector<uint8_t> definition;
        definition.reserve(size);

        // One round trip per 32-byte page. A few KB is roughly 100 of them, about
        // 200 ms -- the cost that LZMA compression exists to keep down.
        for (uint16_t page = 0; definition.size() < size; ++page)
        {
            std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetDefinition,
                                                           { static_cast<uint8_t>(page & 0xFF),
                                                             static_cast<uint8_t>(page >> 8) });

            const size_t remaining = size - definition.size();
            const size_t take      = std::min<size_t>(c_ViaReportSize, remaining);

            definition.insert(definition.end(), reply.begin(), reply.begin() + take);
        }

        co_return definition;
    }

    Task<VialEntryCounts> VialProtocol::GetEntryCounts()
    {
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::DynamicEntryOp,
                                                       { static_cast<uint8_t>(VialDynamicEntry::GetNumberOfEntries) });

        VialEntryCounts counts;
        counts.tapDance     = reply[0];
        counts.combo        = reply[1];
        counts.keyOverride  = reply[2];
        counts.altRepeatKey = reply[3];

        // Optional features are bit flags in the LAST byte of the report, not the first.
        const uint8_t features = reply[c_ViaReportSize - 1];
        counts.capsWord  = (features & (1 << 0)) != 0;
        counts.layerLock = (features & (1 << 1)) != 0;

        co_return counts;
    }

    Task<VialUnlockStatus> VialProtocol::GetUnlockStatus()
    {
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetUnlockStatus, {});

        VialUnlockStatus status;
        status.unlocked   = reply[0] != 0;
        status.inProgress = reply[1] != 0;

        // The firmware fills the buffer with 0xFF and then writes (row, column) pairs
        // over it, so the first 0xFF row ends the list. An insecure build writes none.
        for (size_t offset = 2; offset + 1 < c_ViaReportSize; offset += 2)
        {
            if (reply[offset] == 0xFF)
                break;

            status.combo.emplace_back(reply[offset], reply[offset + 1]);
        }

        co_return status;
    }

    Task<VialProtocol::EncoderPair> VialProtocol::GetEncoder(uint8_t layer, uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetEncoder, { layer, index });

        EncoderPair pair;
        pair.counterClockwise = ReadBigEndian16(reply, 0);
        pair.clockwise        = ReadBigEndian16(reply, 2);

        co_return pair;
    }
}
