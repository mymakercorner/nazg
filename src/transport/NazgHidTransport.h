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
// Reports nobody asked for are read past, not delivered. A request first discards whatever
// is waiting -- a report nobody asked for, or a firmware's second answer to the last
// request -- then keeps reading until a report passes the caller's ReplyCheck, dropping the
// rest: a Keychron 2025q3 board sends [0xA3, default layer, highest layer] on every layer
// change, which can arrive between a request and its reply. The protocol supplies the check,
// so the pipe stays protocol-neutral; it is the hook a continuous reader would reuse, and
// XAP's token comparison fits the same place.
//
// Left out deliberately:
//   - Delivering unsolicited reports (Keychron's layer report, XAP broadcasts). It means
//     continuously reading every open device, which is a different worker loop and a
//     device kept open, so it waits for a feature that consumes them.
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
#include <functional>
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

    // The device is there, but the system does not let Nazg open it: on Linux, a /dev/hidraw*
    // entry no udev rule gives the user (build-and-release.md, "Permissions: the udev rule").
    // Elsewhere never thrown.
    class HidAccessDenied : public HidTransportError
    {
    public:
        using HidTransportError::HidTransportError;
    };

    // An open device, as seen from the main thread. The hidapi pointer it stands for
    // never leaves the worker thread, and ids are never reused, so an id kept past a
    // Close() is reliably rejected rather than aliasing some later device.
    using DeviceId = uint64_t;

    inline constexpr DeviceId c_InvalidDevice = 0;

    // How long Request() waits for the device to answer. QMK answers a raw HID command
    // within a poll interval or two; a second is generous enough to mean "gone".
    inline constexpr uint32_t c_DefaultTimeoutMs = 1000;

    // Is this report the reply to my request? Supplied by the protocol layer, which knows
    // what its replies look like -- VIA's echo its command id -- and empty for "the first
    // report is the reply". It runs on the worker thread, so it looks at the bytes and at
    // what it captured by value, nothing else, and does not throw.
    using ReplyCheck = std::function<bool(const std::vector<uint8_t>& report)>;

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
        class SeriesAwaitable;
        class SendAwaitable;

        // Enumerate connected HID devices. Pass a usagePage/usage of 0 for no filter.
        //     auto devices = co_await transport.Enumerate(0xFF60, 0x61);
        [[nodiscard]] EnumerateAwaitable Enumerate(uint16_t usagePage = 0, uint16_t usage = 0);

        // Open one device by the path from HidDeviceInfo, yielding the id every later
        // call uses. Throws HidTransportError if the device cannot be opened, and
        // HidAccessDenied when that is because Linux refuses it.
        [[nodiscard]] OpenAwaitable Open(std::string path);

        // Write one report and wait for one back. The payload is sent as given; the
        // reply is whatever the device returned, with no framing added or removed.
        // With an isReply check, reports that fail it are dropped and the reading goes on
        // until one passes or the timeout runs out; without one, the first report is it.
        // Throws HidTransportError on a dead device, a timeout, or an unknown id.
        //
        // A failed request does NOT invalidate the device: the id stays usable, and
        // closing it stays the caller's job either way.
        [[nodiscard]] RequestAwaitable Request(DeviceId             device,
                                               std::vector<uint8_t> payload,
                                               uint32_t             timeoutMs = c_DefaultTimeoutMs,
                                               ReplyCheck           isReply   = {});

        // Several requests, each written once the reply to the one before it is in, all on the
        // worker: what Request() does for each, without a return to the frame loop between them --
        // Pump() resumes only what has completed, so a request after a request waits for the next
        // frame. A Leyden Jar level reading is its sweep then eighteen column reads: 37 ms on the
        // wire, some 300 ms one per frame (ui-design.md, "Plugins", "The pace"). One check per
        // payload, or none for "the first report"; the replies in order. The first failure throws,
        // and nothing after it is written.
        [[nodiscard]] SeriesAwaitable RequestSeries(DeviceId                          device,
                                                    std::vector<std::vector<uint8_t>> payloads,
                                                    uint32_t                          timeoutMs = c_DefaultTimeoutMs,
                                                    std::vector<ReplyCheck>           checks    = {});

        // A report written when the device is closed -- by Close(), or by Shutdown() at exit -- its
        // answer read and dropped: what puts back a setting the firmware keeps in RAM on every path
        // that closes the device, the Leyden Jar's key output (ui-design.md, "Plugins"). An empty
        // payload takes it back. Not awaitable, as Close(); nothing if the device is not open.
        void SetFarewell(DeviceId device, std::vector<uint8_t> payload);

        // Write one report and wait for nothing back: a command the firmware does not answer,
        // because it restarts -- the Leyden Jar's bootloader and EEPROM erase. Throws
        // HidTransportError on a dead device or an unknown id.
        [[nodiscard]] SendAwaitable Send(DeviceId device, std::vector<uint8_t> payload);

        // Close a device. Not awaitable on purpose: closing cannot meaningfully fail,
        // and it has to work from a destructor -- HidDeviceChannel's, which closes the
        // device it opened, and destructors cannot co_await.
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
            Series,
            Send,
            Farewell,
            Close
        };

        // One struct for all seven operations rather than a class hierarchy: the worker
        // loop stays a flat switch, and everything an operation carries is visible in
        // one place. Inputs are filled in by the caller, outputs by the worker.
        struct Operation
        {
            OperationKind kind = OperationKind::Enumerate;

            uint16_t             usagePage = 0;                    // Enumerate
            uint16_t             usage     = 0;                    // Enumerate
            std::string          path;                             // Open
            DeviceId             device    = c_InvalidDevice;      // Request, Series, Send, Farewell, Close
            std::vector<uint8_t> payload;                          // Request, Send, Farewell
            uint32_t             timeoutMs = c_DefaultTimeoutMs;   // Request, Series
            ReplyCheck           isReply;                          // Request, may be empty

            std::vector<std::vector<uint8_t>> payloads;   // Series
            std::vector<ReplyCheck>           checks;     // Series, may be shorter
            std::vector<std::vector<uint8_t>> replies;    // Series

            std::vector<HidDeviceInfo> devices;                       // Enumerate
            DeviceId                   opened = c_InvalidDevice;      // Open
            std::vector<uint8_t>       reply;                         // Request
            std::string                error;                         // empty means success
            bool                       isDenied = false;              // Open: refused by the system

            std::coroutine_handle<> continuation;   // none for Close
        };

        using OperationPtr = std::shared_ptr<Operation>;

        void WorkerLoop();
        void Submit(OperationPtr operation);

        // Worker thread only, all of them. m_Devices, m_Farewells and m_NextDeviceId are
        // touched nowhere else, which is why none needs the mutex.
        void Execute(Operation& operation);
        void ExecuteEnumerate(Operation& operation);
        void ExecuteOpen(Operation& operation);
        void ExecuteRequest(Operation& operation);
        void ExecuteSeries(Operation& operation);
        void ExecuteSend(Operation& operation);
        void ExecuteFarewell(Operation& operation);
        void ExecuteClose(Operation& operation);
        void CloseAllDevices();

        // One report written and its reply read: Request()'s work, false with `error` set.
        bool Exchange(::hid_device_* device, const std::vector<uint8_t>& payload, uint32_t timeoutMs,
                      const ReplyCheck& isReply, std::vector<uint8_t>& reply, std::string& error);

        // The device's farewell, if it has one, written and answered before it is closed.
        void SayFarewell(DeviceId id, ::hid_device_* device);

        std::unordered_map<DeviceId, ::hid_device_*>       m_Devices;
        std::unordered_map<DeviceId, std::vector<uint8_t>> m_Farewells;
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
                if (m_Operation->isDenied)
                    throw HidAccessDenied(m_Operation->error);
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

        class SeriesAwaitable : public Awaitable
        {
        public:
            using Awaitable::Awaitable;

            std::vector<std::vector<uint8_t>> await_resume()
            {
                ThrowIfFailed();
                return std::move(m_Operation->replies);
            }
        };

        class SendAwaitable : public Awaitable
        {
        public:
            using Awaitable::Awaitable;

            void await_resume() { ThrowIfFailed(); }
        };
    };
}
