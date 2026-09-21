// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HidTransport - asynchronous sugar over hidapi, shaped after WebHID.
//
// The transport is a dumb pipe: it moves opaque byte buffers and knows nothing about
// VIA, Vial or XAP. Correlation belongs in the protocol layer above. See
// docs/research_material/client-architecture.md, "Transport and threading".
//
// Threading contract:
//   - hidapi calls block on a worker thread owned by this class.
//   - Completions are queued, and Pump() drains them ON THE CALLING (main) THREAD.
//   - Therefore coroutines awaiting this transport always resume on the main thread,
//     and protocol and UI code never need a lock.
//
// Under Emscripten this class would be reimplemented over WebHID with no thread at
// all; the interface, Pump() and the awaiting protocol code stay identical.

#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace nazg
{
    struct HidDeviceInfo
    {
        std::string path;
        std::string manufacturer;
        std::string product;
        std::string serialNumber;
        uint16_t    vendorId        = 0;
        uint16_t    productId       = 0;
        uint16_t    usagePage       = 0;
        uint16_t    usage           = 0;
        uint16_t    releaseNumber   = 0;
        int         interfaceNumber = -1;
    };

    class HidTransportError : public std::runtime_error
    {
    public:
        explicit HidTransportError(const std::string& message) : std::runtime_error(message) {}
    };

    class HidTransport
    {
    public:
        HidTransport();
        ~HidTransport();

        HidTransport(const HidTransport&)            = delete;
        HidTransport& operator=(const HidTransport&) = delete;

        // Enumerate connected HID devices. Pass a usagePage/usage of 0 for no filter.
        // The returned object is awaited, never stored:
        //     auto devices = co_await transport.Enumerate(0xFF60, 0x61);
        class EnumerateOperation;
        [[nodiscard]] EnumerateOperation Enumerate(uint16_t usagePage = 0, uint16_t usage = 0);

        // Call once per frame from the main thread. Resumes coroutines whose work
        // finished. Nothing else in this class may be called from another thread.
        void Pump();

        // Fails every outstanding operation and resumes it, so awaiting coroutines
        // throw and unwind rather than leaking their frames. Called by the destructor;
        // call it explicitly if you need the unwinding to happen earlier.
        void Shutdown();

        bool IsRunning() const noexcept { return m_Running.load(); }

    private:
        struct Request
        {
            uint16_t                   usagePage = 0;
            uint16_t                   usage     = 0;
            std::vector<HidDeviceInfo> devices;
            std::string                error;          // empty means success
            std::coroutine_handle<>    continuation;
        };

        using RequestPtr = std::shared_ptr<Request>;

        void WorkerLoop();
        void Submit(RequestPtr request);

        std::thread             m_Worker;
        std::atomic<bool>       m_Running{ false };

        std::mutex              m_Mutex;
        std::condition_variable m_WorkAvailable;
        std::deque<RequestPtr>  m_Pending;      // submitted, not yet serviced
        std::deque<RequestPtr>  m_Completed;    // serviced, awaiting Pump()

    public:
        // The awaitable returned by Enumerate(). Suspends the calling coroutine,
        // hands the work to the worker thread, and resumes from Pump().
        class EnumerateOperation
        {
        public:
            EnumerateOperation(HidTransport& transport, RequestPtr request)
                : m_Transport(transport), m_Request(std::move(request)) {}

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting)
            {
                // Set the continuation before submitting. Both this and Pump() run on
                // the main thread, so the worker can never resume us prematurely.
                m_Request->continuation = awaiting;
                m_Transport.Submit(m_Request);
            }

            std::vector<HidDeviceInfo> await_resume()
            {
                if (!m_Request->error.empty())
                    throw HidTransportError(m_Request->error);

                return std::move(m_Request->devices);
            }

        private:
            HidTransport& m_Transport;
            RequestPtr    m_Request;
        };
    };
}
