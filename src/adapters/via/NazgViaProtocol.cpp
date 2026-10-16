// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaProtocol.h"

#include <algorithm>
#include <utility>

#include "adapters/NazgByteOrder.h"

namespace nazg::via
{
    namespace
    {
        // Only used for the error messages below, so it stays here rather than joining
        // the byte-order helpers.
        std::string Hex(uint8_t value)
        {
            constexpr char c_Digits[] = "0123456789ABCDEF";
            return std::string("0x") + c_Digits[(value >> 4) & 0x0F] + c_Digits[value & 0x0F];
        }
    }

    SwitchMatrixFormat SwitchMatrixFormatFor(uint16_t viaProtocol) noexcept
    {
        return viaProtocol >= 12 ? SwitchMatrixFormat::Paged : SwitchMatrixFormat::Whole;
    }

    bool WholeMatrixFits(uint8_t rows, uint8_t columns) noexcept
    {
        return (columns / 8 + 1) * rows <= 28;
    }

    bool SwitchMatrixState::IsPressed(uint8_t row, uint8_t column) const noexcept
    {
        if (row >= rows || column >= columns)
            return false;

        const size_t rowBytes = (columns + 7u) / 8u;
        const size_t index    = row * rowBytes + (rowBytes - 1 - column / 8u);
        return index < bytes.size() && ((bytes[index] >> (column % 8u)) & 1u) != 0;
    }

    std::vector<uint8_t> Protocol::MakeFrame(uint8_t command, std::initializer_list<uint8_t> arguments)
    {
        // Always the full 32 bytes, zero-padded: QMK's raw HID endpoint takes exactly
        // RAW_EPSIZE and a short write would be rejected by the host stack.
        std::vector<uint8_t> frame(c_ReportSize, 0x00);
        frame[0] = command;

        size_t index = 1;
        for (uint8_t argument : arguments)
            frame[index++] = argument;

        return frame;
    }

    size_t Protocol::EchoedBytes(Command command) noexcept
    {
        switch (command)
        {
        case Command::GetKeyboardValue:
        case Command::SetKeyboardValue:
            return 2;   // the value id; Keychron's own matrix reply keeps it too (send_analog_matrix())
        case Command::DynamicKeymapGetKeycode:
        case Command::DynamicKeymapSetKeycode:
        case Command::DynamicKeymapGetEncoder:
        case Command::DynamicKeymapSetEncoder:
            return 4;   // layer, row and column, or layer, index and direction
        case Command::MacroGetBuffer:
        case Command::MacroSetBuffer:
        case Command::DynamicKeymapGetBuffer:
        case Command::DynamicKeymapSetBuffer:
            return 4;   // offset and size
        default:
            return 1;
        }
    }

    ReplyCheck Protocol::ReplyTo(const std::vector<uint8_t>& frame, size_t echoed)
    {
        std::vector<uint8_t> sent(frame.begin(), frame.begin() + std::min(echoed, frame.size()));

        return [sent = std::move(sent)](const std::vector<uint8_t>& report) {
            // Too short to judge, and no firmware's report is: let the caller say so. A refusal
            // is the reply whatever follows its 0xFF -- QMK leaves the rest as sent, but nothing
            // promises a vendor's code does, and no report nobody asked for starts with it.
            if (report.size() < c_ReportSize || report[0] == static_cast<uint8_t>(Command::Unhandled))
                return true;

            return std::equal(sent.begin(), sent.end(), report.begin());
        };
    }

    Task<std::vector<uint8_t>> Protocol::Send(Command command, std::initializer_list<uint8_t> arguments)
    {
        co_return co_await SendFrame(MakeFrame(static_cast<uint8_t>(command), arguments), arguments.size() > 0,
                                     EchoedBytes(command));
    }

    Task<std::vector<uint8_t>> Protocol::SendFrame(std::vector<uint8_t> frame, bool hasArguments, size_t echoed)
    {
        const uint8_t commandId = frame[0];
        const Command command   = static_cast<Command>(commandId);
        const uint8_t subId     = frame[1];

        ReplyCheck           isReply = ReplyTo(frame, echoed);
        std::vector<uint8_t> reply   = co_await m_Channel.Request(std::move(frame), std::move(isReply));

        if (reply.size() < c_ReportSize)
            throw ProtocolError("short reply to command " + Hex(commandId) + ": " +
                                std::to_string(reply.size()) + " bytes");

        // The firmware echoes the command id on success and replaces it with 0xFF when
        // it does not recognise the command -- or when the command was compiled out,
        // or when the command is known but its sub-id is not. All three arrive
        // identically, so the message names the sub-id too rather than guessing which.
        if (reply[0] == static_cast<uint8_t>(Command::Unhandled) && command != Command::Unhandled)
        {
            std::string what = "command " + Hex(commandId);
            if (hasArguments)
                what += " (sub-id " + Hex(subId) + ")";

            throw ProtocolError(what + " is not supported by this firmware");
        }

        // A reply for another command never gets here: the channel read past it.
        co_return reply;
    }

    Task<bool> Protocol::Supports(Command command, std::initializer_list<uint8_t> arguments)
    {
        std::vector<uint8_t> frame   = MakeFrame(static_cast<uint8_t>(command), arguments);
        ReplyCheck           isReply = ReplyTo(frame, EchoedBytes(command));
        std::vector<uint8_t> reply   = co_await m_Channel.Request(std::move(frame), std::move(isReply));

        co_return reply.size() >= c_ReportSize &&
                  reply[0] == static_cast<uint8_t>(command);
    }

    Task<uint16_t> Protocol::GetProtocolVersion()
    {
        std::vector<uint8_t> reply = co_await Send(Command::GetProtocolVersion, {});
        co_return ReadBigEndian16(reply, 1);
    }

    Task<uint32_t> Protocol::GetKeyboardValue(KeyboardValue value)
    {
        std::vector<uint8_t> reply = co_await Send(Command::GetKeyboardValue, { static_cast<uint8_t>(value) });
        co_return ReadBigEndian32(reply, 2);
    }

    Task<void> Protocol::SetKeyboardValue(KeyboardValue value, uint32_t data)
    {
        co_await Send(Command::SetKeyboardValue,
                      { static_cast<uint8_t>(value), static_cast<uint8_t>(data >> 24), static_cast<uint8_t>(data >> 16),
                        static_cast<uint8_t>(data >> 8), static_cast<uint8_t>(data) });
    }

    Task<SwitchMatrixState> Protocol::GetSwitchMatrixState(SwitchMatrixFormat format, uint8_t rows,
                                                           uint8_t columns)
    {
        const uint8_t id       = static_cast<uint8_t>(KeyboardValue::SwitchMatrixState);
        const size_t  rowBytes = (columns + 7u) / 8u;

        SwitchMatrixState state;
        state.rows    = rows;
        state.columns = columns;
        state.bytes.reserve(rows * rowBytes);

        if (format == SwitchMatrixFormat::Whole)
        {
            std::vector<uint8_t> reply = co_await Send(Command::GetKeyboardValue, { id });

            const size_t length = std::min(rows * rowBytes, reply.size() - 2);
            state.bytes.assign(reply.begin() + 2, reply.begin() + 2 + length);
            state.bytes.resize(rows * rowBytes, 0x00);
            co_return state;
        }

        const uint8_t rowsPerReply = static_cast<uint8_t>(std::max<size_t>(1, c_BufferChunk / rowBytes));
        for (uint8_t first = 0; first < rows; first = static_cast<uint8_t>(first + rowsPerReply))
        {
            // The first row is echoed too, Keychron's own reply included (send_analog_matrix()):
            // a late second answer for another page is not taken for this one.
            std::vector<uint8_t> reply = co_await SendFrame(MakeFrame(static_cast<uint8_t>(Command::GetKeyboardValue),
                                                                      { id, first }),
                                                            true, 3);

            const size_t count  = std::min<size_t>(rowsPerReply, rows - first);
            const size_t length = std::min(count * rowBytes, reply.size() - 3);
            state.bytes.insert(state.bytes.end(), reply.begin() + 3, reply.begin() + 3 + length);
        }
        state.bytes.resize(rows * rowBytes, 0x00);
        co_return state;
    }

    Task<uint8_t> Protocol::GetLayerCount()
    {
        std::vector<uint8_t> reply = co_await Send(Command::DynamicKeymapGetLayerCount, {});
        co_return reply[1];
    }

    Task<uint16_t> Protocol::GetKeycode(uint8_t layer, uint8_t row, uint8_t column)
    {
        std::vector<uint8_t> reply = co_await Send(Command::DynamicKeymapGetKeycode, { layer, row, column });
        co_return ReadBigEndian16(reply, 4);
    }

    Task<void> Protocol::SetKeycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode)
    {
        co_await Send(Command::DynamicKeymapSetKeycode,
                      { layer, row, column,
                        static_cast<uint8_t>(keycode >> 8), static_cast<uint8_t>(keycode & 0xFF) });
    }

    Task<uint16_t> Protocol::GetEncoder(uint8_t layer, uint8_t index, bool clockwise)
    {
        std::vector<uint8_t> reply =
            co_await Send(Command::DynamicKeymapGetEncoder, { layer, index, static_cast<uint8_t>(clockwise ? 1 : 0) });
        co_return ReadBigEndian16(reply, 4);
    }

    Task<void> Protocol::SetEncoder(uint8_t layer, uint8_t index, bool clockwise, uint16_t keycode)
    {
        co_await Send(Command::DynamicKeymapSetEncoder,
                      { layer, index, static_cast<uint8_t>(clockwise ? 1 : 0), static_cast<uint8_t>(keycode >> 8),
                        static_cast<uint8_t>(keycode & 0xFF) });
    }

    Task<uint8_t> Protocol::GetMacroCount()
    {
        std::vector<uint8_t> reply = co_await Send(Command::MacroGetCount, {});
        co_return reply[1];
    }

    Task<uint16_t> Protocol::GetMacroBufferSize()
    {
        std::vector<uint8_t> reply = co_await Send(Command::MacroGetBufferSize, {});
        co_return ReadBigEndian16(reply, 1);
    }

    Task<std::vector<uint8_t>> Protocol::GetKeymapBuffer(uint16_t offset, uint16_t length)
    {
        co_return co_await ReadChunked(Command::DynamicKeymapGetBuffer, offset, length);
    }

    Task<std::vector<uint8_t>> Protocol::GetMacroBuffer(uint16_t offset, uint16_t length)
    {
        co_return co_await ReadChunked(Command::MacroGetBuffer, offset, length);
    }

    Task<void> Protocol::SetMacroBuffer(uint16_t offset, const std::vector<uint8_t>& bytes)
    {
        for (size_t done = 0; done < bytes.size();)
        {
            const uint16_t position = static_cast<uint16_t>(offset + done);
            const size_t   size     = std::min<size_t>(c_BufferChunk, bytes.size() - done);

            std::vector<uint8_t> frame = MakeFrame(static_cast<uint8_t>(Command::MacroSetBuffer),
                                                   { static_cast<uint8_t>(position >> 8),
                                                     static_cast<uint8_t>(position & 0xFF),
                                                     static_cast<uint8_t>(size) });
            std::copy(bytes.begin() + done, bytes.begin() + done + size, frame.begin() + 4);
            co_await SendFrame(std::move(frame), true, EchoedBytes(Command::MacroSetBuffer));
            done += size;
        }
    }

    namespace
    {
        // A custom value frame: the command, the channel when there is one, the value id, then `rest`.
        std::vector<uint8_t> CustomFrame(Command command, std::optional<uint8_t> channel, uint8_t valueId,
                                         const std::vector<uint8_t>& rest)
        {
            std::vector<uint8_t> frame(c_ReportSize, 0x00);
            frame[0]     = static_cast<uint8_t>(command);
            size_t index = 1;
            if (channel)
                frame[index++] = *channel;
            frame[index++] = valueId;
            for (uint8_t byte : rest)
                if (index < frame.size())
                    frame[index++] = byte;
            return frame;
        }
    }

    Task<std::vector<uint8_t>> Protocol::GetCustomValue(std::optional<uint8_t> channel, uint8_t valueId,
                                                        std::vector<uint8_t> arguments, size_t length)
    {
        // The firmware writes the value after the address -- channel and value id, or the value id
        // alone in the format before V3 -- and keeps the address.
        const size_t               first = channel ? 3 : 2;
        const std::vector<uint8_t> reply = co_await SendFrame(CustomFrame(Command::CustomGetValue, channel, valueId, arguments),
                                                              true, first);
        co_return std::vector<uint8_t>(reply.begin() + first, reply.begin() + std::min(reply.size(), first + length));
    }

    Task<void> Protocol::SetCustomValue(std::optional<uint8_t> channel, uint8_t valueId, const std::vector<uint8_t>& value)
    {
        co_await SendFrame(CustomFrame(Command::CustomSetValue, channel, valueId, value), true, channel ? 3 : 2);
    }

    Task<void> Protocol::SaveCustomValues(std::optional<uint8_t> channel)
    {
        std::vector<uint8_t> frame(c_ReportSize, 0x00);
        frame[0] = static_cast<uint8_t>(Command::CustomSave);
        if (channel)
            frame[1] = *channel;
        co_await SendFrame(std::move(frame), channel.has_value(), channel ? 2 : 1);
    }

    Task<std::vector<uint8_t>> Protocol::ReadMacros(uint8_t count, uint16_t bufferSize)
    {
        std::vector<uint8_t> bytes;
        uint8_t              seen = 0;
        while (seen < count && bytes.size() < bufferSize)
        {
            const uint16_t wanted = std::min<uint16_t>(c_BufferChunk, static_cast<uint16_t>(bufferSize - bytes.size()));
            std::vector<uint8_t> chunk = co_await GetMacroBuffer(static_cast<uint16_t>(bytes.size()), wanted);
            for (uint8_t byte : chunk)
            {
                bytes.push_back(byte);
                if (byte == 0 && ++seen == count)
                    break;
            }
        }
        co_return bytes;
    }

    Task<void> Protocol::WriteMacros(const std::vector<uint8_t>& stored, const std::vector<uint8_t>& bytes,
                                     uint16_t bufferSize)
    {
        if (bytes.size() > bufferSize)
            throw ProtocolError("the macros need " + std::to_string(bytes.size()) + " bytes, the board has " +
                                std::to_string(bufferSize));

        size_t first = 0;
        while (first < bytes.size() && first < stored.size() && bytes[first] == stored[first])
            ++first;
        if (first == bytes.size())
            co_return;

        const uint16_t last = static_cast<uint16_t>(bufferSize - 1);
        co_await SetMacroBuffer(last, { 0xFF });
        co_await SetMacroBuffer(static_cast<uint16_t>(first),
                                std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(first), bytes.end()));
        co_await SetMacroBuffer(last, { 0x00 });
    }

    Task<std::vector<uint8_t>> Protocol::ReadChunked(Command command, uint16_t offset, uint16_t length)
    {
        std::vector<uint8_t> out;
        out.reserve(length);

        // The payoff of coroutines: a loop of round trips, written as a loop. Each
        // iteration suspends, the frame loop keeps rendering, and Pump() resumes here.
        while (out.size() < length)
        {
            const uint16_t position = static_cast<uint16_t>(offset + out.size());
            const uint16_t wanted   = std::min<uint16_t>(c_BufferChunk,
                                                         static_cast<uint16_t>(length - out.size()));

            std::vector<uint8_t> reply = co_await Send(command,
                                                       { static_cast<uint8_t>(position >> 8),
                                                         static_cast<uint8_t>(position & 0xFF),
                                                         static_cast<uint8_t>(wanted) });

            out.insert(out.end(), reply.begin() + 4, reply.begin() + 4 + wanted);
        }

        co_return out;
    }
}
