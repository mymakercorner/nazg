// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDeviceChannel.h"

#include <utility>

namespace nazg
{
    Task<std::vector<std::vector<uint8_t>>> DeviceChannel::RequestSeries(std::vector<std::vector<uint8_t>> payloads,
                                                                         std::vector<ReplyCheck>           checks)
    {
        std::vector<std::vector<uint8_t>> replies;
        replies.reserve(payloads.size());
        for (size_t index = 0; index < payloads.size(); ++index)
            replies.push_back(co_await Request(std::move(payloads[index]),
                                               index < checks.size() ? std::move(checks[index]) : ReplyCheck{}));
        co_return replies;
    }

    Task<HidDeviceChannel> HidDeviceChannel::Open(HidTransport& transport, std::string path, uint32_t timeoutMs)
    {
        const DeviceId device = co_await transport.Open(std::move(path));
        co_return HidDeviceChannel(transport, device, timeoutMs);
    }

    HidDeviceChannel::HidDeviceChannel(HidDeviceChannel&& other) noexcept
        : m_Transport(other.m_Transport), m_Device(std::exchange(other.m_Device, c_InvalidDevice)),
          m_TimeoutMs(other.m_TimeoutMs)
    {
    }

    HidDeviceChannel& HidDeviceChannel::operator=(HidDeviceChannel&& other) noexcept
    {
        if (this != &other)
        {
            if (m_Device != c_InvalidDevice)
                m_Transport->Close(m_Device);
            m_Transport = other.m_Transport;
            m_Device    = std::exchange(other.m_Device, c_InvalidDevice);
            m_TimeoutMs = other.m_TimeoutMs;
        }
        return *this;
    }

    HidDeviceChannel::~HidDeviceChannel()
    {
        if (m_Device != c_InvalidDevice)
            m_Transport->Close(m_Device);
    }

    Task<std::vector<uint8_t>> HidDeviceChannel::Request(std::vector<uint8_t> payload, ReplyCheck isReply)
    {
        co_return co_await m_Transport->Request(m_Device, std::move(payload), m_TimeoutMs, std::move(isReply));
    }

    Task<std::vector<std::vector<uint8_t>>> HidDeviceChannel::RequestSeries(std::vector<std::vector<uint8_t>> payloads,
                                                                            std::vector<ReplyCheck>           checks)
    {
        co_return co_await m_Transport->RequestSeries(m_Device, std::move(payloads), m_TimeoutMs, std::move(checks));
    }

    Task<void> HidDeviceChannel::Send(std::vector<uint8_t> payload)
    {
        co_await m_Transport->Send(m_Device, std::move(payload));
    }

    void HidDeviceChannel::SetFarewell(std::vector<uint8_t> payload)
    {
        m_Transport->SetFarewell(m_Device, std::move(payload));
    }
}
