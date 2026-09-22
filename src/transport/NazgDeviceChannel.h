// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DeviceChannel - one open device, seen from the protocol layer.
//
// Protocol code talks to this, never to HidTransport directly. Two reasons, both from
// docs/research_material/client-architecture.md:
//
//   - The protocol layer becomes testable with no hardware and no threads: a test
//     supplies a scripted channel and drives a whole command sequence deterministically.
//   - A DeviceId stops being passed hand to hand through every protocol function. The
//     channel binds the transport and the device together once.
//
// This is NOT the transport seam. HidTransport still calls hidapi directly; see
// NazgHidTransport.h. This seam sits one level up, where the cost is a single virtual
// call per protocol round trip -- nothing next to a USB transaction.

#pragma once

#include <cstdint>
#include <vector>

#include "async/NazgTask.h"
#include "transport/NazgHidTransport.h"

namespace nazg
{
    class DeviceChannel
    {
    public:
        virtual ~DeviceChannel() = default;

        // Write one report, wait for one back. Throws HidTransportError if the device
        // is gone or does not answer. What counts as a valid reply is the protocol
        // layer's business, not the channel's -- this moves bytes and nothing else.
        virtual Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload) = 0;
    };

    // The real channel: a device opened through HidTransport. The timeout is fixed per
    // channel rather than per call, because no command in VIA or Vial needs its own.
    class HidDeviceChannel : public DeviceChannel
    {
    public:
        HidDeviceChannel(HidTransport& transport, DeviceId device,
                         uint32_t timeoutMs = c_DefaultTimeoutMs)
            : m_Transport(transport), m_Device(device), m_TimeoutMs(timeoutMs) {}

        Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload) override;

        DeviceId Device() const noexcept { return m_Device; }

    private:
        HidTransport& m_Transport;
        DeviceId      m_Device;
        uint32_t      m_TimeoutMs;
    };
}
