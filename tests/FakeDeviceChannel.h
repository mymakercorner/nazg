// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A DeviceChannel that answers from a script instead of a keyboard. This is what the
// architecture notes mean by "the protocol layer becomes testable without hardware or
// threads": no hidapi, no worker thread, no Pump(), and every reply is chosen by the
// test. The coroutine completes synchronously, because Task<T> starts eagerly and
// nothing here suspends.

#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "async/NazgTask.h"
#include "transport/NazgDeviceChannel.h"

namespace via = nazg::via;

class FakeDeviceChannel : public nazg::DeviceChannel
{
public:
    // Queue one reply. Short vectors are padded to a full report, so a test only
    // spells out the bytes it cares about.
    void Reply(std::vector<uint8_t> reply)
    {
        reply.resize(via::c_ReportSize, 0x00);
        m_Replies.push_back({ std::move(reply), false });
    }

    // Queue the firmware's echo of whatever is asked -- a VIA set command's answer, the request
    // back as sent, which passes the protocol's reply check as the real one does.
    void ReplyEcho() { m_Replies.push_back({ {}, true }); }

    // Queue the firmware's "I do not know that command" answer: 0xFF in byte 0.
    void ReplyUnhandled() { Reply({ 0xFF }); }

    // Queue a reply exactly as given, with no padding -- for testing what happens when
    // a device returns something shorter than a full report.
    void ReplyRaw(std::vector<uint8_t> reply) { m_Replies.push_back({ std::move(reply), false }); }

    // As HidTransport's worker does, a scripted report failing the protocol's reply check is
    // dropped and the next one read; running out of them while dropping is the timeout.
    nazg::Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload, nazg::ReplyCheck isReply = {}) override
    {
        m_Requests.push_back(payload);

        if (m_Replies.empty())
            throw nazg::HidTransportError("FakeDeviceChannel: no reply scripted for this request");

        while (!m_Replies.empty())
        {
            Scripted scripted = std::move(m_Replies.front());
            m_Replies.pop_front();

            std::vector<uint8_t> reply = scripted.echo ? payload : std::move(scripted.bytes);
            if (scripted.echo)
                reply.resize(via::c_ReportSize, 0x00);

            if (!isReply || isReply(reply))
                co_return reply;

            ++m_Dropped;
        }

        throw nazg::HidTransportError("FakeDeviceChannel: no scripted report was the reply -- the device did not answer");
    }

    // A report sent with no reply awaited -- recorded with the requests, in order.
    nazg::Task<void> Send(std::vector<uint8_t> payload) override
    {
        m_Requests.push_back(std::move(payload));
        ++m_Sent;
        co_return;
    }

    // Reports sent by Send(), awaiting nothing.
    size_t SentCount() const noexcept { return m_Sent; }

    // The report the real channel would write on closing, as last set; empty for none.
    void SetFarewell(std::vector<uint8_t> payload) override { m_Farewell = std::move(payload); }
    const std::vector<uint8_t>& Farewell() const noexcept { return m_Farewell; }

    // Series run, each one a call -- their requests are in Requests() with the rest.
    nazg::Task<std::vector<std::vector<uint8_t>>> RequestSeries(std::vector<std::vector<uint8_t>> payloads,
                                                                std::vector<nazg::ReplyCheck>     checks) override
    {
        ++m_Series;
        co_return co_await nazg::DeviceChannel::RequestSeries(std::move(payloads), std::move(checks));
    }

    size_t SeriesCount() const noexcept { return m_Series; }

    // Reports read past because they were not the reply, over every request so far.
    size_t DroppedCount() const noexcept { return m_Dropped; }

    const std::vector<std::vector<uint8_t>>& Requests() const noexcept { return m_Requests; }

    size_t RequestCount() const noexcept { return m_Requests.size(); }

    const std::vector<uint8_t>& RequestAt(size_t index) const { return m_Requests.at(index); }

private:
    struct Scripted
    {
        std::vector<uint8_t> bytes;
        bool                 echo = false;   // the request itself, not `bytes`
    };

    std::deque<Scripted>              m_Replies;
    std::vector<std::vector<uint8_t>> m_Requests;
    size_t                            m_Dropped = 0;
    size_t                            m_Sent    = 0;
    size_t                            m_Series  = 0;
    std::vector<uint8_t>              m_Farewell;
};
