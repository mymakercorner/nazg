// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgHidTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

#include "hidapi.h"

namespace nazg
{
    namespace
    {
        // Guards against a coroutine that resubmits work on every resume.
        constexpr int c_MaxPumpPasses = 64;

        // A pending read is broken into slices of this length so the worker notices a
        // shutdown request without waiting out the whole timeout first. Long enough
        // that the polling costs nothing, short enough that exit feels immediate.
        constexpr uint32_t c_ReadSliceMs = 50;

        // Read buffer size. 64 bytes is the Full Speed interrupt maximum, so no report
        // from a QMK device can exceed it (RAW_EPSIZE is half that).
        constexpr size_t c_MaxReportSize = 64;

        // Cap on the stale reports discarded at once, so a device that streams input cannot
        // hold the worker in that loop.
        constexpr int c_MaxStaleReports = 64;

        // Throws away every report already waiting to be read. With strict request/response
        // pairing, ONE report nobody asked for shifts every later reply by one command -- and
        // hidapi keeps them: on Windows it asks the driver to buffer 64 input reports. Two
        // sources are known:
        //   - whoever held the device before: a fresh run read the previous run's last reply
        //     and reported "reply 0x12 does not match command 0x01";
        //   - a firmware answering twice. Keychron's Hall-effect boards answer
        //     id_switch_matrix_state from their own code (send_analog_matrix(), from the
        //     analog matrix), then let QMK's via.c answer it again (matrix_get_row()). The
        //     live test, asking every 20 ms, fell 64 replies behind: a second late, with
        //     every other reading from the second matrix (a K10 HE, 2026-10-13).
        // WebHID drops a report nobody waits for, which is why the VIA app never sees either.
        //
        // A zero timeout makes each read a non-blocking poll, so this costs nothing when the
        // queue is empty, which is the normal case.
        void DiscardWaitingReports(hid_device* device)
        {
            unsigned char discarded[c_MaxReportSize];
            for (int i = 0; i < c_MaxStaleReports; ++i)
            {
                if (hid_read_timeout(device, discarded, sizeof(discarded), 0) <= 0)
                    break;
            }
        }

        // hidapi returns wchar_t strings whose width differs per platform (16-bit on
        // Windows, 32-bit elsewhere). Encode the code points as UTF-8 by hand rather
        // than depending on the C locale, which is not reliably UTF-8 on Windows.
        std::string ToUtf8(const wchar_t* text)
        {
            std::string out;
            if (text == nullptr)
                return out;

            constexpr uint32_t c_ReplacementCharacter = 0xFFFD;
            constexpr uint32_t c_MaxCodePoint          = 0x10FFFF;

            for (const wchar_t* p = text; *p != 0; ++p)
            {
                uint32_t codePoint = static_cast<uint32_t>(*p);

                // Where wchar_t is 16 bits (Windows), anything outside the Basic
                // Multilingual Plane arrives as a surrogate PAIR that has to be
                // recombined; encoding the halves separately would emit two invalid
                // sequences. Where wchar_t is 32 bits, code points arrive whole and
                // none of this triggers.
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                {
                    // Safe at the end of the string: the terminator is not a low
                    // surrogate, so this reads the terminator and stops.
                    const uint32_t lowSurrogate = static_cast<uint32_t>(*(p + 1));

                    if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF)
                    {
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
                        ++p;
                    }
                    else
                    {
                        codePoint = c_ReplacementCharacter;   // unpaired high surrogate
                    }
                }
                else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                {
                    codePoint = c_ReplacementCharacter;       // stray low surrogate
                }

                if (codePoint > c_MaxCodePoint)
                    codePoint = c_ReplacementCharacter;

                if (codePoint < 0x80)
                {
                    out.push_back(static_cast<char>(codePoint));
                }
                else if (codePoint < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else if (codePoint < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
            }

            return out;
        }

        // hidapi's per-device error string, or its global one when no device is open.
        std::string LastError(hid_device* device)
        {
            const std::string message = ToUtf8(hid_error(device));
            return message.empty() ? "no further detail" : message;
        }
    }

    HidTransport::HidTransport()
    {
        m_Running.store(true);
        m_Worker = std::thread(&HidTransport::WorkerLoop, this);
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

    void HidTransport::Submit(OperationPtr operation)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            if (!m_Running.load())
            {
                // Already shut down: fail immediately so the coroutine still unwinds.
                operation->error = "transport is shut down";
                m_Completed.push_back(std::move(operation));
                return;
            }

            m_Pending.push_back(std::move(operation));
        }

        m_WorkAvailable.notify_one();
    }

    void HidTransport::WorkerLoop()
    {
        hid_init();

        for (;;)
        {
            OperationPtr operation;

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WorkAvailable.wait(lock, [this] { return !m_Pending.empty() || !m_Running.load(); });

                // Stop as soon as shutdown is requested, leaving anything still
                // queued for Shutdown() to fail. Draining it here would block exit on
                // a blocking hidapi call per queued operation, and would mean the
                // fail-all-pending path never ran.
                if (!m_Running.load())
                    break;

                operation = std::move(m_Pending.front());
                m_Pending.pop_front();
            }

            // Blocking hidapi work happens here, off the main thread.
            Execute(*operation);

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Completed.push_back(std::move(operation));
            }
        }

        // Whatever the protocol layer left open, plus anything whose queued Close was
        // abandoned above. Must happen on this thread: the handles belong to it.
        CloseAllDevices();

        hid_exit();
    }

    void HidTransport::Execute(Operation& operation)
    {
        switch (operation.kind)
        {
        case OperationKind::Enumerate: ExecuteEnumerate(operation); break;
        case OperationKind::Open:      ExecuteOpen(operation);      break;
        case OperationKind::Request:   ExecuteRequest(operation);   break;
        case OperationKind::Close:     ExecuteClose(operation);     break;
        }
    }

    void HidTransport::ExecuteEnumerate(Operation& operation)
    {
        hid_device_info* enumerated = hid_enumerate(0, 0);

        if (enumerated == nullptr)
        {
            // hid_enumerate returns null both for "no devices" and for failure.
            // Treat it as an empty list; a genuine failure surfaces on open.
            operation.devices.clear();
            return;
        }

        for (hid_device_info* current = enumerated; current != nullptr; current = current->next)
        {
            const bool matchesUsage =
                (operation.usagePage == 0 || current->usage_page == operation.usagePage) &&
                (operation.usage     == 0 || current->usage      == operation.usage);

            if (!matchesUsage)
                continue;

            HidDeviceInfo info;
            info.path            = current->path != nullptr ? current->path : "";
            info.manufacturer    = ToUtf8(current->manufacturer_string);
            info.product         = ToUtf8(current->product_string);
            info.serialNumber    = ToUtf8(current->serial_number);
            info.vendorId        = current->vendor_id;
            info.productId       = current->product_id;
            info.usagePage       = current->usage_page;
            info.usage           = current->usage;
            info.releaseNumber   = current->release_number;
            info.interfaceNumber = current->interface_number;

            operation.devices.push_back(std::move(info));
        }

        hid_free_enumeration(enumerated);
    }

    void HidTransport::ExecuteOpen(Operation& operation)
    {
        hid_device* device = hid_open_path(operation.path.c_str());

        if (device == nullptr)
        {
            operation.error = "could not open " + operation.path + ": " + LastError(nullptr);
            return;
        }

        // A report queued before we opened cannot be an answer to a request we have not
        // sent yet.
        DiscardWaitingReports(device);

        // Ids come from a counter that is never reset and never reuses a value, so an
        // id kept past its Close() fails cleanly instead of hitting a later device.
        const DeviceId id = m_NextDeviceId++;
        m_Devices.emplace(id, device);
        operation.opened = id;
    }

    void HidTransport::ExecuteRequest(Operation& operation)
    {
        const auto entry = m_Devices.find(operation.device);
        if (entry == m_Devices.end())
        {
            operation.error = "request for a device that is not open";
            return;
        }

        hid_device* device = entry->second;

        // Nothing waiting can be the answer to this request, which is not sent yet: a second
        // answer to the last one, or a report nobody asked for.
        DiscardWaitingReports(device);

        // hidapi wants the report ID as the first byte on every platform. QMK's raw
        // HID interface uses unnumbered reports, so that byte is 0 and the firmware
        // never sees it.
        std::vector<uint8_t> framed;
        framed.reserve(operation.payload.size() + 1);
        framed.push_back(0x00);
        framed.insert(framed.end(), operation.payload.begin(), operation.payload.end());

        if (hid_write(device, framed.data(), framed.size()) < 0)
        {
            operation.error = "write failed: " + LastError(device);
            return;
        }

        // A deadline rather than a count of empty reads: a report dropped by the check below
        // takes time too, and a board streaming them must not stretch the wait forever.
        using Clock = std::chrono::steady_clock;
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(operation.timeoutMs);

        unsigned char buffer[c_MaxReportSize];
        int           dropped = 0;

        for (;;)
        {
            const Clock::time_point now       = Clock::now();
            const uint32_t          remaining = now < deadline
                ? static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count())
                : 0;
            const uint32_t slice     = std::min(remaining, c_ReadSliceMs);
            const int      bytesRead = hid_read_timeout(device, buffer, sizeof(buffer),
                                                        static_cast<int>(slice));

            if (bytesRead < 0)
            {
                operation.error = "read failed: " + LastError(device);
                return;
            }

            if (bytesRead > 0)
            {
                operation.reply.assign(buffer, buffer + bytesRead);
                if (!operation.isReply || operation.isReply(operation.reply))
                    return;

                // Not the reply -- a report nobody asked for, arrived while waiting. Dropped.
                operation.reply.clear();
                ++dropped;
            }

            // A zero timeout polls once and gives up, which falls out of the order
            // here: the deadline has passed by the time this first pass gets here.
            if (Clock::now() >= deadline)
            {
                operation.error = "device did not answer within " +
                                  std::to_string(operation.timeoutMs) + " ms";
                if (dropped > 0)
                    operation.error += " (" + std::to_string(dropped) + " report(s) that were not the reply dropped)";
                return;
            }

            if (!m_Running.load())
            {
                operation.error = "transport shut down while waiting for the device";
                return;
            }
        }
    }

    void HidTransport::ExecuteClose(Operation& operation)
    {
        const auto entry = m_Devices.find(operation.device);
        if (entry == m_Devices.end())
            return;   // unknown or already closed: nothing to do

        hid_close(entry->second);
        m_Devices.erase(entry);
    }

    void HidTransport::CloseAllDevices()
    {
        for (const auto& entry : m_Devices)
            hid_close(entry.second);

        m_Devices.clear();
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

    void HidTransport::Shutdown()
    {
        if (!m_Running.exchange(false))
            return;

        m_WorkAvailable.notify_all();

        if (m_Worker.joinable())
            m_Worker.join();

        // Fail everything the worker never got to, then resume those coroutines so
        // they throw and unwind instead of leaking their frames. This is the rule that
        // keeps a disconnect from presenting as a frozen UI.
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (OperationPtr& operation : m_Pending)
            {
                operation->error = "transport shut down before the operation was serviced";
                m_Completed.push_back(std::move(operation));
            }
            m_Pending.clear();
        }

        Pump();
    }
}
