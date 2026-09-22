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

class FakeDeviceChannel : public nazg::DeviceChannel
{
public:
    // Queue one reply. Short vectors are padded to a full report, so a test only
    // spells out the bytes it cares about.
    void Reply(std::vector<uint8_t> reply)
    {
        reply.resize(nazg::c_ViaReportSize, 0x00);
        m_Replies.push_back(std::move(reply));
    }

    // Queue the firmware's "I do not know that command" answer: 0xFF in byte 0.
    void ReplyUnhandled() { Reply({ 0xFF }); }

    // Queue a reply exactly as given, with no padding -- for testing what happens when
    // a device returns something shorter than a full report.
    void ReplyRaw(std::vector<uint8_t> reply) { m_Replies.push_back(std::move(reply)); }

    nazg::Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload) override
    {
        m_Requests.push_back(payload);

        if (m_Replies.empty())
            throw nazg::HidTransportError("FakeDeviceChannel: no reply scripted for this request");

        std::vector<uint8_t> reply = std::move(m_Replies.front());
        m_Replies.pop_front();

        co_return reply;
    }

    const std::vector<std::vector<uint8_t>>& Requests() const noexcept { return m_Requests; }

    size_t RequestCount() const noexcept { return m_Requests.size(); }

    const std::vector<uint8_t>& RequestAt(size_t index) const { return m_Requests.at(index); }

private:
    std::deque<std::vector<uint8_t>>  m_Replies;
    std::vector<std::vector<uint8_t>> m_Requests;
};
