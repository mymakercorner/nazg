// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialProtocol.h"

#include <algorithm>
#include <string>
#include <utility>

#include "adapters/NazgByteOrder.h"

namespace nazg
{
    namespace
    {
        // A definition is a few KB -- 704 bytes on the board this was first run
        // against. This only exists so a wrong size byte cannot ask for a gigabyte
        // before anything notices.
        constexpr uint32_t c_MaxDefinitionSize = 256 * 1024;
    }

    Task<std::vector<uint8_t>> VialProtocol::SendVial(VialCommand                    command,
                                                      std::initializer_list<uint8_t> arguments,
                                                      bool                           expectsData)
    {
        // [0] prefix, [1] sub-command, arguments from [2] -- which is where the
        // firmware reads them, since it keeps the prefix and sub-command in place.
        std::vector<uint8_t> frame(c_ViaReportSize, 0x00);
        frame[0] = static_cast<uint8_t>(ViaCommand::VialPrefix);
        frame[1] = static_cast<uint8_t>(command);

        size_t index = 2;
        for (uint8_t argument : arguments)
            frame[index++] = argument;

        const std::vector<uint8_t> sent = frame;

        std::vector<uint8_t> reply = co_await m_Channel.Request(std::move(frame));

        if (reply.size() < c_ViaReportSize)
            throw ProtocolError("short reply to Vial command " +
                                std::to_string(static_cast<int>(command)) + ": " +
                                std::to_string(reply.size()) + " bytes");

        // Vial does NOT use VIA's 0xFF marker. A sub-command compiled out of the
        // firmware falls through its switch without touching the buffer, so the reply
        // is the request, echoed back byte for byte. Observed on a board without
        // encoders: vial_get_encoder "returned" 0xFE03, which is the prefix and
        // sub-command being read back as a keycode.
        //
        // Commands that legitimately return nothing (lock, unlock start, settings
        // reset) echo the request too, so only callers expecting data check for it.
        if (expectsData && reply == sent)
            throw ProtocolError("Vial command " + std::to_string(static_cast<int>(command)) +
                                " is not supported by this firmware");

        co_return reply;
    }

    Task<std::optional<VialIdentity>> VialProtocol::Detect()
    {
        // "Not a Vial board" is an answer, not a failure, so this asks for the reply
        // without the unsupported-feature check and inspects it here.
        std::vector<uint8_t> reply = co_await SendVial(VialCommand::GetKeyboardId, {}, false);

        // A plain VIA board does not know 0xFE and answers with the unhandled marker.
        if (reply[0] == static_cast<uint8_t>(ViaCommand::Unhandled))
            co_return std::nullopt;

        // Belt and braces for firmware that echoes an unknown command instead: a real
        // reply starts with the low byte of the protocol version, which runs 0 to 6 and
        // can never be 0xFE.
        if (reply[0] == static_cast<uint8_t>(ViaCommand::VialPrefix) &&
            reply[1] == static_cast<uint8_t>(VialCommand::GetKeyboardId))
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
