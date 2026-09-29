// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgViaProtocol.h"

#include <algorithm>
#include <utility>

#include "adapters/NazgByteOrder.h"

namespace nazg
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

    SwitchMatrixFormat SwitchMatrixFormatForVia(uint16_t viaProtocol) noexcept
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

    std::vector<uint8_t> ViaProtocol::MakeFrame(uint8_t command, std::initializer_list<uint8_t> arguments)
    {
        // Always the full 32 bytes, zero-padded: QMK's raw HID endpoint takes exactly
        // RAW_EPSIZE and a short write would be rejected by the host stack.
        std::vector<uint8_t> frame(c_ViaReportSize, 0x00);
        frame[0] = command;

        size_t index = 1;
        for (uint8_t argument : arguments)
            frame[index++] = argument;

        return frame;
    }

    Task<std::vector<uint8_t>> ViaProtocol::Send(ViaCommand command, std::initializer_list<uint8_t> arguments)
    {
        const uint8_t commandId = static_cast<uint8_t>(command);

        std::vector<uint8_t> reply = co_await m_Channel.Request(MakeFrame(commandId, arguments));

        if (reply.size() < c_ViaReportSize)
            throw ProtocolError("short reply to command " + Hex(commandId) + ": " +
                                std::to_string(reply.size()) + " bytes");

        // The firmware echoes the command id on success and replaces it with 0xFF when
        // it does not recognise the command -- or when the command was compiled out,
        // or when the command is known but its sub-id is not. All three arrive
        // identically, so the message names the sub-id too rather than guessing which.
        if (reply[0] == static_cast<uint8_t>(ViaCommand::Unhandled) && command != ViaCommand::Unhandled)
        {
            std::string what = "command " + Hex(commandId);
            if (arguments.size() > 0)
                what += " (sub-id " + Hex(*arguments.begin()) + ")";

            throw ProtocolError(what + " is not supported by this firmware");
        }

        if (reply[0] != commandId)
            throw ProtocolError("reply " + Hex(reply[0]) + " does not match command " + Hex(commandId));

        co_return reply;
    }

    Task<bool> ViaProtocol::Supports(ViaCommand command, std::initializer_list<uint8_t> arguments)
    {
        std::vector<uint8_t> reply = co_await m_Channel.Request(MakeFrame(static_cast<uint8_t>(command), arguments));

        co_return reply.size() >= c_ViaReportSize &&
                  reply[0] == static_cast<uint8_t>(command);
    }

    Task<uint16_t> ViaProtocol::GetProtocolVersion()
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::GetProtocolVersion, {});
        co_return ReadBigEndian16(reply, 1);
    }

    Task<uint32_t> ViaProtocol::GetKeyboardValue(ViaKeyboardValue value)
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::GetKeyboardValue, { static_cast<uint8_t>(value) });
        co_return ReadBigEndian32(reply, 2);
    }

    Task<SwitchMatrixState> ViaProtocol::GetSwitchMatrixState(SwitchMatrixFormat format, uint8_t rows,
                                                              uint8_t columns)
    {
        const uint8_t id       = static_cast<uint8_t>(ViaKeyboardValue::SwitchMatrixState);
        const size_t  rowBytes = (columns + 7u) / 8u;

        SwitchMatrixState state;
        state.rows    = rows;
        state.columns = columns;
        state.bytes.reserve(rows * rowBytes);

        if (format == SwitchMatrixFormat::Whole)
        {
            std::vector<uint8_t> reply = co_await Send(ViaCommand::GetKeyboardValue, { id });

            const size_t length = std::min(rows * rowBytes, reply.size() - 2);
            state.bytes.assign(reply.begin() + 2, reply.begin() + 2 + length);
            state.bytes.resize(rows * rowBytes, 0x00);
            co_return state;
        }

        const uint8_t rowsPerReply = static_cast<uint8_t>(std::max<size_t>(1, c_ViaBufferChunk / rowBytes));
        for (uint8_t first = 0; first < rows; first = static_cast<uint8_t>(first + rowsPerReply))
        {
            std::vector<uint8_t> reply = co_await Send(ViaCommand::GetKeyboardValue, { id, first });

            const size_t count  = std::min<size_t>(rowsPerReply, rows - first);
            const size_t length = std::min(count * rowBytes, reply.size() - 3);
            state.bytes.insert(state.bytes.end(), reply.begin() + 3, reply.begin() + 3 + length);
        }
        state.bytes.resize(rows * rowBytes, 0x00);
        co_return state;
    }

    Task<uint8_t> ViaProtocol::GetLayerCount()
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::DynamicKeymapGetLayerCount, {});
        co_return reply[1];
    }

    Task<uint16_t> ViaProtocol::GetKeycode(uint8_t layer, uint8_t row, uint8_t column)
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::DynamicKeymapGetKeycode, { layer, row, column });
        co_return ReadBigEndian16(reply, 4);
    }

    Task<void> ViaProtocol::SetKeycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode)
    {
        co_await Send(ViaCommand::DynamicKeymapSetKeycode,
                      { layer, row, column,
                        static_cast<uint8_t>(keycode >> 8), static_cast<uint8_t>(keycode & 0xFF) });
    }

    Task<uint8_t> ViaProtocol::GetMacroCount()
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::MacroGetCount, {});
        co_return reply[1];
    }

    Task<uint16_t> ViaProtocol::GetMacroBufferSize()
    {
        std::vector<uint8_t> reply = co_await Send(ViaCommand::MacroGetBufferSize, {});
        co_return ReadBigEndian16(reply, 1);
    }

    Task<std::vector<uint8_t>> ViaProtocol::GetKeymapBuffer(uint16_t offset, uint16_t length)
    {
        co_return co_await ReadChunked(ViaCommand::DynamicKeymapGetBuffer, offset, length);
    }

    Task<std::vector<uint8_t>> ViaProtocol::GetMacroBuffer(uint16_t offset, uint16_t length)
    {
        co_return co_await ReadChunked(ViaCommand::MacroGetBuffer, offset, length);
    }

    Task<std::vector<uint8_t>> ViaProtocol::ReadChunked(ViaCommand command, uint16_t offset, uint16_t length)
    {
        std::vector<uint8_t> out;
        out.reserve(length);

        // The payoff of coroutines: a loop of round trips, written as a loop. Each
        // iteration suspends, the frame loop keeps rendering, and Pump() resumes here.
        while (out.size() < length)
        {
            const uint16_t position = static_cast<uint16_t>(offset + out.size());
            const uint16_t wanted   = std::min<uint16_t>(c_ViaBufferChunk,
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
