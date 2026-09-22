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
// Usage -- an asynchronous sequence written as straight-line code:
//
//     DeviceId device = co_await transport.Open(path);
//     std::vector<uint8_t> reply = co_await transport.Request(device, command);
//     transport.Close(device);
//
// The worker services operations one at a time, in submission order, so a request
// that waits out its timeout stalls whatever is queued behind it. That is acceptable
// while the protocols are single-outstanding (VIA) and one board is configured at a
// time; revisit it if that stops being true.
//
// Left out deliberately:
//   - Unsolicited reports (XAP broadcasts). Delivering them means continuously
//     polling every open device, which is a different worker loop, and XAP is a
//     later decision. Nothing in the API below has to change to add them.
//   - Report padding. The transport writes exactly the bytes it is handed, prefixed
//     with the report ID byte hidapi expects. QMK's 32-byte RAW_EPSIZE frame is the
//     protocol layer's business, not the pipe's.
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
#include <unordered_map>
#include <vector>

// hidapi's opaque device type, forward-declared at global scope so hidapi.h stays out
// of this header. hidapi.h declares the same type; both refer to the one type.
struct hid_device_;

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

    // An open device, as seen from the main thread. The hidapi pointer it stands for
    // never leaves the worker thread, and ids are never reused, so an id kept past a
    // Close() is reliably rejected rather than aliasing some later device.
    using DeviceId = uint64_t;

    inline constexpr DeviceId c_InvalidDevice = 0;

    // How long Request() waits for the device to answer. QMK answers a raw HID command
    // within a poll interval or two; a second is generous enough to mean "gone".
    inline constexpr uint32_t c_DefaultTimeoutMs = 1000;

    class HidTransport
    {
    public:
        HidTransport();
        ~HidTransport();

        HidTransport(const HidTransport&)            = delete;
        HidTransport& operator=(const HidTransport&) = delete;

        // Each of these returns an object that is awaited, never stored.
        class EnumerateAwaitable;
        class OpenAwaitable;
        class RequestAwaitable;

        // Enumerate connected HID devices. Pass a usagePage/usage of 0 for no filter.
        //     auto devices = co_await transport.Enumerate(0xFF60, 0x61);
        [[nodiscard]] EnumerateAwaitable Enumerate(uint16_t usagePage = 0, uint16_t usage = 0);

        // Open one device by the path from HidDeviceInfo, yielding the id every later
        // call uses. Throws HidTransportError if the device cannot be opened -- on
        // Linux that is usually a udev permissions problem, not a missing device.
        [[nodiscard]] OpenAwaitable Open(std::string path);

        // Write one report and wait for one back. The payload is sent as given; the
        // reply is whatever the device returned, with no framing added or removed.
        // Throws HidTransportError on a dead device, a timeout, or an unknown id.
        //
        // A failed request does NOT invalidate the device: the id stays usable, and
        // closing it stays the caller's job either way.
        [[nodiscard]] RequestAwaitable Request(DeviceId             device,
                                               std::vector<uint8_t> payload,
                                               uint32_t             timeoutMs = c_DefaultTimeoutMs);

        // Close a device. Not awaitable on purpose: closing cannot meaningfully fail,
        // and it has to work from a destructor -- which is where an RAII device guard
        // in the protocol layer will call it, and destructors cannot co_await.
        // Closing an id that is unknown or already closed does nothing.
        void Close(DeviceId device);

        // Call once per frame from the main thread. Resumes coroutines whose work
        // finished. Nothing else in this class may be called from another thread.
        void Pump();

        // Fails every outstanding operation and resumes it, so awaiting coroutines
        // throw and unwind rather than leaking their frames. Closes any device still
        // open. Called by the destructor; call it explicitly if you need the
        // unwinding to happen earlier.
        void Shutdown();

        bool IsRunning() const noexcept { return m_Running.load(); }

    private:
        enum class OperationKind
        {
            Enumerate,
            Open,
            Request,
            Close
        };

        // One struct for all four operations rather than a class hierarchy: the worker
        // loop stays a flat switch, and everything an operation carries is visible in
        // one place. Inputs are filled in by the caller, outputs by the worker.
        struct Operation
        {
            OperationKind kind = OperationKind::Enumerate;

            uint16_t             usagePage = 0;                    // Enumerate
            uint16_t             usage     = 0;                    // Enumerate
            std::string          path;                             // Open
            DeviceId             device    = c_InvalidDevice;      // Request, Close
            std::vector<uint8_t> payload;                          // Request
            uint32_t             timeoutMs = c_DefaultTimeoutMs;   // Request

            std::vector<HidDeviceInfo> devices;                       // Enumerate
            DeviceId                   opened = c_InvalidDevice;      // Open
            std::vector<uint8_t>       reply;                         // Request
            std::string                error;                         // empty means success

            std::coroutine_handle<> continuation;   // none for Close
        };

        using OperationPtr = std::shared_ptr<Operation>;

        void WorkerLoop();
        void Submit(OperationPtr operation);

        // Worker thread only, all four. m_Devices and m_NextDeviceId are touched
        // nowhere else, which is why neither needs the mutex.
        void Execute(Operation& operation);
        void ExecuteEnumerate(Operation& operation);
        void ExecuteOpen(Operation& operation);
        void ExecuteRequest(Operation& operation);
        void ExecuteClose(Operation& operation);
        void CloseAllDevices();

        std::unordered_map<DeviceId, ::hid_device_*> m_Devices;
        DeviceId                                     m_NextDeviceId = c_InvalidDevice + 1;

        std::thread             m_Worker;
        std::atomic<bool>       m_Running{ false };

        std::mutex              m_Mutex;
        std::condition_variable m_WorkAvailable;
        std::deque<OperationPtr> m_Pending;     // submitted, not yet serviced
        std::deque<OperationPtr> m_Completed;   // serviced, awaiting Pump()

    public:
        // Suspends the calling coroutine, hands the operation to the worker thread,
        // and is resumed from Pump(). Only await_resume() differs per operation, so
        // the rest lives here once.
        class Awaitable
        {
        public:
            Awaitable(HidTransport& transport, OperationPtr operation)
                : m_Transport(transport), m_Operation(std::move(operation)) {}

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting)
            {
                // Set the continuation before submitting. Both this and Pump() run on
                // the main thread, so the worker can never resume us prematurely.
                m_Operation->continuation = awaiting;
                m_Transport.Submit(m_Operation);
            }

        protected:
            void ThrowIfFailed() const
            {
                if (!m_Operation->error.empty())
                    throw HidTransportError(m_Operation->error);
            }

            HidTransport& m_Transport;
            OperationPtr  m_Operation;
        };

        class EnumerateAwaitable : public Awaitable
        {
        public:
            using Awaitable::Awaitable;

            std::vector<HidDeviceInfo> await_resume()
            {
                ThrowIfFailed();
                return std::move(m_Operation->devices);
            }
        };

        class OpenAwaitable : public Awaitable
        {
        public:
            using Awaitable::Awaitable;

            DeviceId await_resume()
            {
                ThrowIfFailed();
                return m_Operation->opened;
            }
        };

        class RequestAwaitable : public Awaitable
        {
        public:
            using Awaitable::Awaitable;

            std::vector<uint8_t> await_resume()
            {
                ThrowIfFailed();
                return std::move(m_Operation->reply);
            }
        };
    };
}
