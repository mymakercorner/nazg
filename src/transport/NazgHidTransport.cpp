// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgHidTransport.h"

#include <utility>

#include <SDL3/SDL_log.h>

#include "hidapi.h"

namespace nazg
{
    namespace
    {
        // Guards against a coroutine that resubmits work on every resume.
        constexpr int c_MaxPumpPasses = 64;

        // hidapi returns wchar_t strings whose width differs per platform (16-bit on
        // Windows, 32-bit elsewhere). Encode the code points as UTF-8 by hand rather
        // than depending on the C locale, which is not reliably UTF-8 on Windows.
        std::string ToUtf8(const wchar_t* text)
        {
            std::string out;
            if (text == nullptr)
                return out;

            for (const wchar_t* p = text; *p != 0; ++p)
            {
                const uint32_t codePoint = static_cast<uint32_t>(*p);

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

    HidTransport::EnumerateOperation HidTransport::Enumerate(uint16_t usagePage, uint16_t usage)
    {
        auto request       = std::make_shared<Request>();
        request->usagePage = usagePage;
        request->usage     = usage;

        return EnumerateOperation(*this, std::move(request));
    }

    void HidTransport::Submit(RequestPtr request)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            if (!m_Running.load())
            {
                // Already shut down: fail immediately so the coroutine still unwinds.
                request->error = "transport is shut down";
                m_Completed.push_back(std::move(request));
                return;
            }

            m_Pending.push_back(std::move(request));
        }

        m_WorkAvailable.notify_one();
    }

    void HidTransport::WorkerLoop()
    {
        hid_init();

        for (;;)
        {
            RequestPtr request;

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WorkAvailable.wait(lock, [this] { return !m_Pending.empty() || !m_Running.load(); });

                // Stop as soon as shutdown is requested, leaving anything still
                // queued for Shutdown() to fail. Draining it here would block exit on
                // a blocking hidapi call per queued request, and would mean the
                // fail-all-pending path never ran.
                if (!m_Running.load())
                    break;

                request = std::move(m_Pending.front());
                m_Pending.pop_front();
            }

            // Blocking hidapi work happens here, off the main thread.
            hid_device_info* enumerated = hid_enumerate(0, 0);

            if (enumerated == nullptr)
            {
                // hid_enumerate returns null both for "no devices" and for failure.
                // Treat it as an empty list; a genuine failure surfaces on open.
                request->devices.clear();
            }
            else
            {
                for (hid_device_info* current = enumerated; current != nullptr; current = current->next)
                {
                    const bool matchesUsage =
                        (request->usagePage == 0 || current->usage_page == request->usagePage) &&
                        (request->usage     == 0 || current->usage      == request->usage);

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

                    request->devices.push_back(std::move(info));
                }

                hid_free_enumeration(enumerated);
            }

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Completed.push_back(std::move(request));
            }
        }

        hid_exit();
    }

    void HidTransport::Pump()
    {
        // Drain repeatedly, not once: a coroutine resumed below may submit more work,
        // and after shutdown Submit() completes it immediately. A single pass would
        // leave that completion stranded, and Shutdown()'s Pump() is the last one.
        for (int pass = 0; pass < c_MaxPumpPasses; ++pass)
        {
            std::deque<RequestPtr> ready;

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                ready.swap(m_Completed);
            }

            if (ready.empty())
                return;

            // Resume outside the lock: a resumed coroutine may submit more work, which
            // would deadlock if we still held the mutex.
            for (RequestPtr& request : ready)
            {
                if (request->continuation)
                {
                    std::coroutine_handle<> continuation = request->continuation;
                    request->continuation = {};
                    continuation.resume();
                }
            }
        }

        // Only reachable if coroutines resubmit without ever settling. Bail out rather
        // than spin forever inside one frame.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "HidTransport::Pump() still had work after %d passes; giving up this frame",
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
            for (RequestPtr& request : m_Pending)
            {
                request->error = "transport shut down before the request was serviced";
                m_Completed.push_back(std::move(request));
            }
            m_Pending.clear();
        }

        Pump();
    }
}
