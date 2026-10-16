// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialProtocol.h"

#include <algorithm>
#include <string>
#include <utility>

#include "adapters/NazgByteOrder.h"

namespace nazg::vial
{
    namespace
    {
        // A definition is a few KB -- 704 bytes on the board this was first run
        // against. This only exists so a wrong size byte cannot ask for a gigabyte
        // before anything notices.
        constexpr uint32_t c_MaxDefinitionSize = 256 * 1024;
    }

    Task<std::vector<uint8_t>> Protocol::SendVial(Command                        command,
                                                  std::initializer_list<uint8_t> arguments,
                                                  bool                           expectsData)
    {
        // [0] prefix, [1] sub-command, arguments from [2] -- which is where the
        // firmware reads them, since it keeps the prefix and sub-command in place.
        std::vector<uint8_t> frame(via::c_ReportSize, 0x00);
        frame[0] = static_cast<uint8_t>(via::Command::VialPrefix);
        frame[1] = static_cast<uint8_t>(command);

        size_t index = 2;
        for (uint8_t argument : arguments)
            frame[index++] = argument;

        const std::vector<uint8_t> sent = frame;

        // No reply check: Vial overwrites the buffer from byte 0, so a reply's first bytes are
        // data -- a definition page can start with anything -- and nothing in it tells a report
        // nobody asked for from the reply. Vial firmware sends none, so the first report is it.
        std::vector<uint8_t> reply = co_await m_Channel.Request(std::move(frame));

        if (reply.size() < via::c_ReportSize)
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

    Task<std::optional<Identity>> Protocol::Detect()
    {
        // "Not a Vial board" is an answer, not a failure, so this asks for the reply
        // without the unsupported-feature check and inspects it here.
        std::vector<uint8_t> reply = co_await SendVial(Command::GetKeyboardId, {}, false);

        // A plain VIA board does not know 0xFE and answers with the unhandled marker.
        if (reply[0] == static_cast<uint8_t>(via::Command::Unhandled))
            co_return std::nullopt;

        // Belt and braces for firmware that echoes an unknown command instead: a real
        // reply starts with the low byte of the protocol version, which runs 0 to 6 and
        // can never be 0xFE.
        if (reply[0] == static_cast<uint8_t>(via::Command::VialPrefix) &&
            reply[1] == static_cast<uint8_t>(Command::GetKeyboardId))
            co_return std::nullopt;

        Identity identity;
        identity.protocolVersion = ReadLittleEndian32(reply, 0);
        identity.keyboardUid     = ReadLittleEndian64(reply, 4);
        identity.supportsVialRgb = reply[12] != 0;

        co_return identity;
    }

    Task<uint32_t> Protocol::GetDefinitionSize()
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::GetSize, {});
        co_return ReadLittleEndian32(reply, 0);
    }

    Task<std::vector<uint8_t>> Protocol::DownloadDefinition()
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
            std::vector<uint8_t> reply = co_await SendVial(Command::GetDefinition,
                                                           { static_cast<uint8_t>(page & 0xFF),
                                                             static_cast<uint8_t>(page >> 8) });

            const size_t remaining = size - definition.size();
            const size_t take      = std::min<size_t>(via::c_ReportSize, remaining);

            definition.insert(definition.end(), reply.begin(), reply.begin() + take);
        }

        co_return definition;
    }

    Task<EntryCounts> Protocol::GetEntryCounts()
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::DynamicEntryOp,
                                                       { static_cast<uint8_t>(DynamicEntry::GetNumberOfEntries) });

        EntryCounts counts;
        counts.tapDance     = reply[0];
        counts.combo        = reply[1];
        counts.keyOverride  = reply[2];
        counts.altRepeatKey = reply[3];

        // Optional features are bit flags in the LAST byte of the report, not the first.
        const uint8_t features = reply[via::c_ReportSize - 1];
        counts.capsWord  = (features & (1 << 0)) != 0;
        counts.layerLock = (features & (1 << 1)) != 0;

        co_return counts;
    }

    Task<TapDanceEntry> Protocol::GetTapDance(uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::DynamicEntryOp,
                                                       { static_cast<uint8_t>(DynamicEntry::TapDanceGet), index });
        if (reply[0] != 0)
            throw ProtocolError("the board has no tap dance " + std::to_string(index));

        TapDanceEntry entry;
        entry.onTap       = ReadLittleEndian16(reply, 1);
        entry.onHold      = ReadLittleEndian16(reply, 3);
        entry.onDoubleTap = ReadLittleEndian16(reply, 5);
        entry.onTapHold   = ReadLittleEndian16(reply, 7);
        entry.tappingTerm = ReadLittleEndian16(reply, 9);
        co_return entry;
    }

    Task<void> Protocol::SetTapDance(uint8_t index, const TapDanceEntry& entry)
    {
        std::vector<uint8_t> reply = co_await SendVial(
            Command::DynamicEntryOp,
            { static_cast<uint8_t>(DynamicEntry::TapDanceSet), index,
              LowByte(entry.onTap), HighByte(entry.onTap),
              LowByte(entry.onHold), HighByte(entry.onHold),
              LowByte(entry.onDoubleTap), HighByte(entry.onDoubleTap),
              LowByte(entry.onTapHold), HighByte(entry.onTapHold),
              LowByte(entry.tappingTerm), HighByte(entry.tappingTerm) });
        if (reply[0] != 0)
            throw ProtocolError("the board has no tap dance " + std::to_string(index));
    }

    Task<ComboEntry> Protocol::GetCombo(uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::DynamicEntryOp,
                                                       { static_cast<uint8_t>(DynamicEntry::ComboGet), index });
        if (reply[0] != 0)
            throw ProtocolError("the board has no combo " + std::to_string(index));

        ComboEntry entry;
        for (size_t input = 0; input < entry.inputs.size(); ++input)
            entry.inputs[input] = ReadLittleEndian16(reply, 1 + 2 * input);
        entry.output = ReadLittleEndian16(reply, 9);
        co_return entry;
    }

    Task<void> Protocol::SetCombo(uint8_t index, const ComboEntry& entry)
    {
        const std::array<uint16_t, 4>& in = entry.inputs;
        std::vector<uint8_t> reply = co_await SendVial(
            Command::DynamicEntryOp,
            { static_cast<uint8_t>(DynamicEntry::ComboSet), index,
              LowByte(in[0]), HighByte(in[0]), LowByte(in[1]), HighByte(in[1]),
              LowByte(in[2]), HighByte(in[2]), LowByte(in[3]), HighByte(in[3]),
              LowByte(entry.output), HighByte(entry.output) });
        if (reply[0] != 0)
            throw ProtocolError("the board has no combo " + std::to_string(index));
    }

    Task<KeyOverrideEntry> Protocol::GetKeyOverride(uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::DynamicEntryOp,
                                                       { static_cast<uint8_t>(DynamicEntry::KeyOverrideGet), index });
        if (reply[0] != 0)
            throw ProtocolError("the board has no key override " + std::to_string(index));

        KeyOverrideEntry entry;
        entry.trigger         = ReadLittleEndian16(reply, 1);
        entry.replacement     = ReadLittleEndian16(reply, 3);
        entry.layers          = ReadLittleEndian16(reply, 5);
        entry.triggerMods     = reply[7];
        entry.negativeModMask = reply[8];
        entry.suppressedMods  = reply[9];
        entry.options         = reply[10];
        co_return entry;
    }

    Task<void> Protocol::SetKeyOverride(uint8_t index, const KeyOverrideEntry& entry)
    {
        std::vector<uint8_t> reply = co_await SendVial(
            Command::DynamicEntryOp,
            { static_cast<uint8_t>(DynamicEntry::KeyOverrideSet), index,
              LowByte(entry.trigger), HighByte(entry.trigger),
              LowByte(entry.replacement), HighByte(entry.replacement),
              LowByte(entry.layers), HighByte(entry.layers),
              entry.triggerMods, entry.negativeModMask, entry.suppressedMods, entry.options });
        if (reply[0] != 0)
            throw ProtocolError("the board has no key override " + std::to_string(index));
    }

    Task<AltRepeatKeyEntry> Protocol::GetAltRepeatKey(uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::DynamicEntryOp,
                                                       { static_cast<uint8_t>(DynamicEntry::AltRepeatKeyGet), index });
        if (reply[0] != 0)
            throw ProtocolError("the board has no alt repeat key " + std::to_string(index));

        AltRepeatKeyEntry entry;
        entry.keycode     = ReadLittleEndian16(reply, 1);
        entry.altKeycode  = ReadLittleEndian16(reply, 3);
        entry.allowedMods = reply[5];
        entry.options     = reply[6];
        co_return entry;
    }

    Task<void> Protocol::SetAltRepeatKey(uint8_t index, const AltRepeatKeyEntry& entry)
    {
        std::vector<uint8_t> reply = co_await SendVial(
            Command::DynamicEntryOp,
            { static_cast<uint8_t>(DynamicEntry::AltRepeatKeySet), index,
              LowByte(entry.keycode), HighByte(entry.keycode),
              LowByte(entry.altKeycode), HighByte(entry.altKeycode),
              entry.allowedMods, entry.options });
        if (reply[0] != 0)
            throw ProtocolError("the board has no alt repeat key " + std::to_string(index));
    }

    Task<uint32_t> Protocol::GetQmkSetting(uint16_t id, size_t width)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::QmkSettingsGet,
                                                       { static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8) });
        if (reply[0] != 0)
            throw ProtocolError("the board has no QMK setting " + std::to_string(id));

        uint32_t value = 0;
        for (size_t byte = 0; byte < std::min<size_t>(width, 4); ++byte)
            value |= static_cast<uint32_t>(reply[1 + byte]) << (8 * byte);
        co_return value;
    }

    Task<void> Protocol::SetQmkSetting(uint16_t id, uint32_t value)
    {
        const auto byte = [value](int n) { return static_cast<uint8_t>((value >> (8 * n)) & 0xFF); };
        std::vector<uint8_t> reply = co_await SendVial(Command::QmkSettingsSet,
                                                       { static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8),
                                                         byte(0), byte(1), byte(2), byte(3) });
        if (reply[0] != 0)
            throw ProtocolError("the board refused QMK setting " + std::to_string(id));
    }

    Task<std::vector<uint16_t>> Protocol::QueryQmkSettings(uint16_t after)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::QmkSettingsQuery,
                                                       { static_cast<uint8_t>(after & 0xFF),
                                                         static_cast<uint8_t>(after >> 8) });

        std::vector<uint16_t> ids;
        for (size_t offset = 0; offset + 1 < via::c_ReportSize; offset += 2)
            if (const uint16_t id = ReadLittleEndian16(reply, offset); id != 0xFFFF)
                ids.push_back(id);

        co_return ids;
    }

    Task<std::vector<uint16_t>> Protocol::ListQmkSettings()
    {
        constexpr size_t      c_PerReport = via::c_ReportSize / 2;
        std::vector<uint16_t> all;
        uint16_t              after = 0;
        for (;;)
        {
            const std::vector<uint16_t> ids = co_await QueryQmkSettings(after);
            for (uint16_t id : ids)
                if (id > after)
                    all.push_back(id);
            // A report not full holds the last of them; ids not rising would never end.
            if (ids.size() < c_PerReport || all.empty() || all.back() <= after)
                break;
            after = all.back();
        }
        co_return all;
    }

    Task<void> Protocol::ResetQmkSettings()
    {
        // vial-qmk leaves the buffer untouched: the reply is the request, as from a firmware without
        // the command. Nothing tells them apart; the settings read back after say what happened.
        co_await SendVial(Command::QmkSettingsReset, {}, false);
    }

    Task<UnlockStatus> Protocol::GetUnlockStatus()
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::GetUnlockStatus, {});

        UnlockStatus status;
        status.unlocked   = reply[0] != 0;
        status.inProgress = reply[1] != 0;

        // The firmware fills the buffer with 0xFF and then writes (row, column) pairs
        // over it, so the first 0xFF row ends the list. An insecure build writes none.
        for (size_t offset = 2; offset + 1 < via::c_ReportSize; offset += 2)
        {
            if (reply[offset] == 0xFF)
                break;

            status.unlockKeys.emplace_back(reply[offset], reply[offset + 1]);
        }

        co_return status;
    }

    Task<void> Protocol::StartUnlock()
    {
        co_await SendVial(Command::UnlockStart, {}, false);
    }

    Task<UnlockProgress> Protocol::PollUnlock()
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::UnlockPoll, {});

        UnlockProgress progress;
        progress.unlocked   = reply[0] != 0;
        progress.inProgress = reply[1] != 0;
        progress.countdown  = reply[2];

        co_return progress;
    }

    Task<void> Protocol::Lock()
    {
        co_await SendVial(Command::Lock, {}, false);
    }

    Task<Protocol::EncoderPair> Protocol::GetVialEncoder(uint8_t layer, uint8_t index)
    {
        std::vector<uint8_t> reply = co_await SendVial(Command::GetEncoder, { layer, index });

        EncoderPair pair;
        pair.counterClockwise = ReadBigEndian16(reply, 0);
        pair.clockwise        = ReadBigEndian16(reply, 2);

        co_return pair;
    }

    Task<void> Protocol::SetVialEncoder(uint8_t layer, uint8_t index, bool clockwise, uint16_t keycode)
    {
        co_await SendVial(Command::SetEncoder,
                          { layer, index, static_cast<uint8_t>(clockwise ? 1 : 0), static_cast<uint8_t>(keycode >> 8),
                            static_cast<uint8_t>(keycode & 0xFF) },
                          false);
    }
}
