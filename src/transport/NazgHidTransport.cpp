// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The half of HidTransport seen from the main thread, the same on every platform: making the
// operations and resuming what completed. Who services them is the platform's --
// NazgHidTransportHidapi.cpp's worker thread natively, NazgHidTransportWebHid.cpp in a browser.

#include "NazgHidTransport.h"

#include <cstdio>
#include <utility>

namespace nazg
{
    namespace
    {
        // Guards against a coroutine that resubmits work on every resume.
        constexpr int c_MaxPumpPasses = 64;
    }

    HidTransport::~HidTransport()
    {
        Shutdown();
    }

    HidTransport::EnumerateAwaitable HidTransport::Enumerate(uint16_t usagePage, uint16_t usage)
    {
        auto operation       = std::make_shared<Operation>();
        operation->kind      = OperationKind::Enumerate;
        operation->usagePage = usagePage;
        operation->usage     = usage;

        return EnumerateAwaitable(*this, std::move(operation));
    }

    HidTransport::OpenAwaitable HidTransport::Open(std::string path)
    {
        auto operation  = std::make_shared<Operation>();
        operation->kind = OperationKind::Open;
        operation->path = std::move(path);

        return OpenAwaitable(*this, std::move(operation));
    }

    HidTransport::RequestAwaitable HidTransport::Request(DeviceId             device,
                                                         std::vector<uint8_t> payload,
                                                         uint32_t             timeoutMs,
                                                         ReplyCheck           isReply)
    {
        auto operation       = std::make_shared<Operation>();
        operation->kind      = OperationKind::Request;
        operation->device    = device;
        operation->payload   = std::move(payload);
        operation->timeoutMs = timeoutMs;
        operation->isReply   = std::move(isReply);

        return RequestAwaitable(*this, std::move(operation));
    }

    HidTransport::SeriesAwaitable HidTransport::RequestSeries(DeviceId                          device,
                                                              std::vector<std::vector<uint8_t>> payloads,
                                                              uint32_t                          timeoutMs,
                                                              std::vector<ReplyCheck>           checks)
    {
        auto operation       = std::make_shared<Operation>();
        operation->kind      = OperationKind::Series;
        operation->device    = device;
        operation->payloads  = std::move(payloads);
        operation->timeoutMs = timeoutMs;
        operation->checks    = std::move(checks);

        return SeriesAwaitable(*this, std::move(operation));
    }

    void HidTransport::SetFarewell(DeviceId device, std::vector<uint8_t> payload)
    {
        auto operation     = std::make_shared<Operation>();
        operation->kind    = OperationKind::Farewell;
        operation->device  = device;
        operation->payload = std::move(payload);

        // No continuation, as Close().
        Submit(std::move(operation));
    }

    HidTransport::SendAwaitable HidTransport::Send(DeviceId device, std::vector<uint8_t> payload)
    {
        auto operation     = std::make_shared<Operation>();
        operation->kind    = OperationKind::Send;
        operation->device  = device;
        operation->payload = std::move(payload);

        return SendAwaitable(*this, std::move(operation));
    }

    void HidTransport::Close(DeviceId device)
    {
        auto operation    = std::make_shared<Operation>();
        operation->kind   = OperationKind::Close;
        operation->device = device;

        // No continuation: nothing awaits a close. After shutdown this lands in the
        // completed queue and Pump() drops it, and the worker closes every device it
        // still holds on its way out regardless.
        Submit(std::move(operation));
    }

    void HidTransport::Pump()
    {
        // Drain repeatedly, not once: a coroutine resumed below may submit more work,
        // and after shutdown Submit() completes it immediately. A single pass would
        // leave that completion stranded, and Shutdown()'s Pump() is the last one.
        for (int pass = 0; pass < c_MaxPumpPasses; ++pass)
        {
            std::deque<OperationPtr> ready;

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                ready.swap(m_Completed);
            }

            if (ready.empty())
                return;

            // Resume outside the lock: a resumed coroutine may submit more work, which
            // would deadlock if we still held the mutex.
            for (OperationPtr& operation : ready)
            {
                if (operation->continuation)
                {
                    std::coroutine_handle<> continuation = operation->continuation;
                    operation->continuation = {};
                    continuation.resume();
                }
            }
        }

        // Only reachable if coroutines resubmit without ever settling. Bail out rather
        // than spin forever inside one frame.
        std::fprintf(stderr, "HidTransport::Pump() still had work after %d passes; giving up this frame\n",
                     c_MaxPumpPasses);
    }
}
