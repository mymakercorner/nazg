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
#include <string>
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
        // layer's business, not the channel's -- this moves bytes and nothing else. The
        // protocol says it through isReply: reports failing it are read past and dropped,
        // as HidTransport::Request() does; without it the first report is the reply.
        // Every override repeats the default, so a call through either type means the same.
        virtual Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload, ReplyCheck isReply = {}) = 0;

        // Several requests in a row, as Request() each with its own check -- `checks` may be shorter,
        // the rest taking the first report -- the replies in order. Throws at the first that fails,
        // writing nothing after it. Here one Request() after the other; the real channel hands the
        // whole series to the transport's worker, so it does not wait a frame per round trip.
        virtual Task<std::vector<std::vector<uint8_t>>> RequestSeries(std::vector<std::vector<uint8_t>> payloads,
                                                                      std::vector<ReplyCheck>           checks);

        // Write one report and wait for nothing: for a command the firmware never answers,
        // because it restarts first. Throws HidTransportError if the device is gone.
        virtual Task<void> Send(std::vector<uint8_t> payload) = 0;

        // A report to write when the device is closed, whichever way -- the channel destroyed, or
        // the transport shut down at exit -- its answer dropped (HidTransport::SetFarewell()). For a
        // setting the firmware keeps in RAM, put back as it was. Empty: none.
        virtual void SetFarewell(std::vector<uint8_t> payload) = 0;
    };

    // The real channel: a device opened through HidTransport, and closed when the channel goes
    // -- on every path, a throw included:
    //
    //     HidDeviceChannel channel = co_await HidDeviceChannel::Open(transport, path);
    //     vial::Protocol vial(channel);
    //
    // It moves, it does not copy; a protocol made on it must not outlive it. The timeout is
    // fixed per channel rather than per call, because no command in VIA or Vial needs its own.
    class HidDeviceChannel : public DeviceChannel
    {
    public:
        // Throws HidTransportError if the device cannot be opened.
        [[nodiscard]] static Task<HidDeviceChannel> Open(HidTransport& transport, std::string path,
                                                         uint32_t timeoutMs = c_DefaultTimeoutMs);

        HidDeviceChannel(HidDeviceChannel&& other) noexcept;
        HidDeviceChannel& operator=(HidDeviceChannel&& other) noexcept;   // closes this one's device first
        ~HidDeviceChannel() override;

        Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload, ReplyCheck isReply = {}) override;
        Task<std::vector<std::vector<uint8_t>>> RequestSeries(std::vector<std::vector<uint8_t>> payloads,
                                                              std::vector<ReplyCheck>           checks) override;
        Task<void>                 Send(std::vector<uint8_t> payload) override;
        void                       SetFarewell(std::vector<uint8_t> payload) override;

    private:
        HidDeviceChannel(HidTransport& transport, DeviceId device, uint32_t timeoutMs) noexcept
            : m_Transport(&transport), m_Device(device), m_TimeoutMs(timeoutMs) {}

        HidTransport* m_Transport;
        DeviceId      m_Device;   // c_InvalidDevice once moved from
        uint32_t      m_TimeoutMs;
    };
}
