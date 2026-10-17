// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLeydenJarProtocol.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "adapters/NazgByteOrder.h"

namespace nazg::leydenjar
{
    namespace
    {
        // The command, the value id and the magic: what the firmware leaves as sent.
        constexpr size_t c_Echoed = 4;

        // The payload's place, in the request and in the reply.
        constexpr size_t c_Payload = 4;

        // A column's levels and bins follow its index, two bytes in: so many fit in a report.
        constexpr size_t c_ColumnData = c_Payload + 2;

        void CheckFits(size_t count, size_t width, size_t offset, const char* what)
        {
            if (offset + count * width > via::c_ReportSize)
                throw std::invalid_argument(std::string(what) + ": " + std::to_string(count) +
                                            " do not fit in one report");
        }

        Version VersionIn(const std::vector<uint8_t>& reply)
        {
            return Version{ reply[c_Payload], reply[c_Payload + 1], ReadLittleEndian16(reply, c_Payload + 2) };
        }
    }

    std::vector<uint8_t> Protocol::MakeRequest(via::Command command, Value value, std::initializer_list<uint8_t> payload)
    {
        std::vector<uint8_t> frame = MakeFrame(static_cast<uint8_t>(command),
                                               { static_cast<uint8_t>(value), LowByte(c_Magic), HighByte(c_Magic) });
        size_t index = c_Payload;
        for (uint8_t byte : payload)
            frame[index++] = byte;
        return frame;
    }

    Task<std::vector<uint8_t>> Protocol::Ask(via::Command command, Value value, std::initializer_list<uint8_t> payload)
    {
        co_return co_await SendFrame(MakeRequest(command, value, payload), true, c_Echoed);
    }

    Task<std::optional<Version>> Protocol::Probe()
    {
        std::vector<uint8_t> frame   = MakeRequest(via::Command::GetKeyboardValue, Value::ProtocolVersion);
        ReplyCheck           isReply = ReplyTo(frame, c_Echoed);
        std::vector<uint8_t> reply   = co_await m_Channel.Request(std::move(frame), std::move(isReply));

        if (reply.size() < via::c_ReportSize || reply[0] == static_cast<uint8_t>(via::Command::Unhandled))
            co_return std::nullopt;

        // A firmware echoing what it does not know sends the request back: a payload of zeroes.
        const Version version = VersionIn(reply);
        if (version == Version{})
            co_return std::nullopt;

        co_return version;
    }

    Task<Version> Protocol::GetVersion()
    {
        co_return VersionIn(co_await Ask(via::Command::GetKeyboardValue, Value::ProtocolVersion));
    }

    Task<Details> Protocol::GetDetails()
    {
        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::Details);

        Details details;
        details.logicalRows       = reply[c_Payload];
        details.logicalColumns    = reply[c_Payload + 1];
        details.controllerRows    = reply[c_Payload + 2];
        details.controllerColumns = reply[c_Payload + 3];
        details.technology        = reply[c_Payload + 4] != 0 ? SwitchTechnology::BeamSpring : SwitchTechnology::ModelF;
        details.bins              = reply[c_Payload + 5];
        co_return details;
    }

    Task<MatrixMapping> Protocol::GetMatrixMapping(const Details& details)
    {
        // The wiring, the columns, then the rows -- as many as the controller has.
        CheckFits(1u + details.controllerColumns + details.controllerRows, 1, c_Payload, "matrix mapping");

        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::MatrixMapping);

        const auto columns = reply.begin() + c_Payload + 1;
        const auto rows    = columns + details.controllerColumns;

        MatrixMapping mapping;
        mapping.wiring = reply[c_Payload];
        mapping.columns.assign(columns, rows);
        mapping.rows.assign(rows, rows + details.controllerRows);
        co_return mapping;
    }

    Task<uint16_t> Protocol::GetDacThreshold(uint8_t bin)
    {
        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::DacThreshold, { bin, 0 });
        co_return ReadLittleEndian16(reply, c_Payload + 2);
    }

    Task<uint16_t> Protocol::GetDacReferenceLevel(uint8_t bin)
    {
        const std::vector<uint8_t> reply =
            co_await Ask(via::Command::GetKeyboardValue, Value::DacReferenceLevel, { bin, 0 });
        co_return ReadLittleEndian16(reply, c_Payload + 2);
    }

    Task<bool> Protocol::IsLeftHalf()
    {
        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::IsLeftHalf);
        if (reply[c_Payload] > 1)
            throw ProtocolError("Leyden Jar: a half that is neither left nor right (" +
                                std::to_string(reply[c_Payload]) + ")");
        co_return reply[c_Payload] == 1;
    }

    Task<std::vector<uint8_t>> Protocol::GetColumnBins(uint8_t column, uint8_t rows)
    {
        CheckFits(rows, 1, c_ColumnData, "bins");

        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::BinMap, { column, 0 });
        co_return std::vector<uint8_t>(reply.begin() + c_ColumnData, reply.begin() + c_ColumnData + rows);
    }

    Task<bool> Protocol::IsKeyOutputEnabled()
    {
        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::KeyOutput);
        co_return reply[c_Payload] != 0;
    }

    Task<void> Protocol::SetKeyOutputEnabled(bool enabled)
    {
        co_await Ask(via::Command::SetKeyboardValue, Value::KeyOutput, { static_cast<uint8_t>(enabled ? 1 : 0) });
    }

    Task<void> Protocol::DetectLevels()
    {
        co_await Ask(via::Command::SetKeyboardValue, Value::DetectLevels);
    }

    Task<std::vector<uint16_t>> Protocol::GetColumnLevels(uint8_t column, uint8_t rows)
    {
        CheckFits(rows, 2, c_ColumnData, "levels");

        const std::vector<uint8_t> reply =
            co_await Ask(via::Command::GetKeyboardValue, Value::ColumnLevels, { column, 0 });

        std::vector<uint16_t> levels(rows);
        for (size_t row = 0; row < rows; ++row)
            levels[row] = ReadLittleEndian16(reply, c_ColumnData + 2 * row);
        co_return levels;
    }

    Task<void> Protocol::ScanLogicalMatrix()
    {
        co_await Ask(via::Command::SetKeyboardValue, Value::ScanLogicalMatrix);
    }

    Task<uint32_t> Protocol::GetLogicalMatrixRow(uint8_t row)
    {
        // The row's index in two bytes, two more unused, then its 32 bits.
        const std::vector<uint8_t> reply =
            co_await Ask(via::Command::GetKeyboardValue, Value::LogicalMatrixRow, { row, 0 });
        co_return ReadLittleEndian32(reply, c_Payload + 4);
    }

    Task<void> Protocol::ScanControllerMatrix()
    {
        co_await Ask(via::Command::SetKeyboardValue, Value::ScanControllerMatrix);
    }

    Task<std::vector<uint8_t>> Protocol::GetControllerMatrix(uint8_t columns)
    {
        CheckFits(columns, 1, c_Payload, "controller columns");

        const std::vector<uint8_t> reply = co_await Ask(via::Command::GetKeyboardValue, Value::ControllerMatrix);
        co_return std::vector<uint8_t>(reply.begin() + c_Payload, reply.begin() + c_Payload + columns);
    }

    Task<void> Protocol::EnterBootloader()
    {
        co_await m_Channel.Send(MakeRequest(via::Command::SetKeyboardValue, Value::EnterBootloader));
    }

    Task<void> Protocol::EraseEeprom()
    {
        co_await m_Channel.Send(MakeRequest(via::Command::SetKeyboardValue, Value::EraseEeprom));
    }

    Task<ControllerInfo> ReadControllerInfo(Protocol& protocol)
    {
        ControllerInfo info;
        info.version = co_await protocol.GetVersion();
        info.details = co_await protocol.GetDetails();
        info.mapping = co_await protocol.GetMatrixMapping(info.details);

        const Details& details = info.details;
        info.bins.assign(details.controllerColumns, std::vector<uint8_t>(details.controllerRows, 0));

        if (info.version < c_BinsVersion)
        {
            info.thresholds.push_back(co_await protocol.GetDacThreshold(0));
            co_return info;
        }

        for (uint8_t bin = 0; bin < details.bins; ++bin)
        {
            info.thresholds.push_back(co_await protocol.GetDacThreshold(bin));
            info.referenceLevels.push_back(co_await protocol.GetDacReferenceLevel(bin));
        }

        for (uint8_t column = 0; column < details.controllerColumns; ++column)
            info.bins[column] = co_await protocol.GetColumnBins(column, details.controllerRows);

        if (info.version >= c_SplitVersion)
            info.isLeftHalf = co_await protocol.IsLeftHalf();

        co_return info;
    }

    uint16_t UnconnectedThreshold(SwitchTechnology technology) noexcept
    {
        return technology == SwitchTechnology::BeamSpring ? 300 : 1023;
    }

    bool IsWiredToNoMatrix(const ControllerInfo& info) noexcept
    {
        const uint16_t sentinel = UnconnectedThreshold(info.details.technology);
        return !info.thresholds.empty() &&
               std::all_of(info.thresholds.begin(), info.thresholds.end(),
                           [sentinel](uint16_t threshold) { return threshold == sentinel; });
    }
}
