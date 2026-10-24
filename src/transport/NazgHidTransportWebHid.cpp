// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HidTransport in a browser, over WebHID (build-and-release.md, "The web build"), where hidapi
// does not exist and a thread would cost the page cross-origin isolation (client-architecture.md,
// "Web build"). Nothing blocks, so there is no worker: an operation is started when the one before
// it ends -- one at a time, in submission order, as natively -- and ended by the browser's answer,
// which arrives between two frames through WebHidEvents below. Its coroutine is resumed by the next
// Pump(), on the main thread as natively.
//
// The browser's side is the JavaScript in the EM_JS functions here, its state in
// Module.nazgWebHid: the HIDDevice objects, each under a handle that stays its own for the page's
// life, so its path -- "webhid:<handle>" -- does too. A page sees only the devices the user chose
// for it, each once, in the browser's chooser (ChooseDevice()); getDevices() lists them at every
// later visit while they are plugged in.
//
// What hidapi does and WebHID does differently:
//   - No queue of input reports: a report nobody waits for is dropped (DiscardWaitingReports()'s
//     work, natively). A report arriving while a reply is awaited still goes through the
//     protocol's ReplyCheck, as natively.
//   - The report id is sendReport()'s argument, not the first byte, and a reply comes without one:
//     QMK's raw HID reports are unnumbered, 0.
//   - No manufacturer, serial number, release number or interface number.
//
// Without WebHID -- Firefox, Safari, a phone, Node running the tests -- no device is listed and
// none opens, every answer known as the operation is submitted.

#include "NazgHidTransport.h"

#include <charconv>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

#include <emscripten.h>
#include <nlohmann/json.hpp>

// The JavaScript copies bytes through malloc() and free(), exported by CMakeLists.txt.
EM_JS_DEPS(nazg_webhid, "$stringToNewUTF8");

// Whether the browser has WebHID; the first call sets up Module.nazgWebHid.
EM_JS(int, nazg_webhid_start, (), {
    if (typeof navigator === 'undefined' || !navigator.hid)
        return 0;
    if (Module.nazgWebHid)
        return 1;

    const web = { devices: [], handles: new Map(), changes: 0 };

    // A device's handle, given the first time it is met. Its reports go to C++ from then on, for
    // as long as it is open; the transport drops what it does not wait for.
    web.handleOf = (device) => {
        let handle = web.handles.get(device);
        if (handle === undefined) {
            handle = web.devices.length;
            web.devices.push(device);
            web.handles.set(device, handle);
            device.addEventListener('inputreport', (event) => {
                const bytes = new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength);
                const at = _malloc(Math.max(bytes.length, 1));
                HEAPU8.set(bytes, at);
                _nazg_webhid_on_report(handle, at, bytes.length);
                _free(at);
            });
        }
        return handle;
    };
    // Copied at once: the caller's memory is freed when it returns, before any promise settles.
    web.bytes = (at, length) => HEAPU8.slice(at, at + length);
    web.fail = (token, error) => {
        const text = stringToNewUTF8(String((error && error.message) || error));
        _nazg_webhid_on_failed(token, text);
        _free(text);
    };

    navigator.hid.addEventListener('connect', () => { web.changes++; });
    navigator.hid.addEventListener('disconnect', () => { web.changes++; });
    Module.nazgWebHid = web;
    return 1;
});

EM_JS(int, nazg_webhid_changes, (), {
    return Module.nazgWebHid ? Module.nazgWebHid.changes : 0;
});

// The chosen devices plugged in, as JSON: one entry per HIDDevice -- one HID interface -- its
// usage the collection matching the one asked for, else its first.
EM_JS(void, nazg_webhid_enumerate, (uint32_t token, int usagePage, int usage), {
    const web = Module.nazgWebHid;
    navigator.hid.getDevices().then((devices) => {
        const list = devices.map((device) => {
            const collections = device.collections || [];
            const wanted = collections.find((c) => c.usagePage === usagePage && c.usage === usage);
            const collection = wanted || collections[0] || { usagePage: 0, usage: 0 };
            return { handle: web.handleOf(device), vendorId: device.vendorId, productId: device.productId,
                     product: device.productName || "", usagePage: collection.usagePage, usage: collection.usage };
        });
        const text = stringToNewUTF8(JSON.stringify(list));
        _nazg_webhid_on_enumerated(token, text);
        _free(text);
    }, (error) => web.fail(token, error));
});

// The browser's chooser, filtered to one usage page and usage; the path chosen, or none.
EM_JS(void, nazg_webhid_choose, (uint32_t token, int usagePage, int usage), {
    const web = Module.nazgWebHid;
    navigator.hid.requestDevice({ filters: [{ usagePage: usagePage, usage: usage }] }).then((devices) => {
        const path = devices.length > 0 ? stringToNewUTF8('webhid:' + web.handleOf(devices[0])) : 0;
        _nazg_webhid_on_chosen(token, path);
        if (path)
            _free(path);
    }, (error) => web.fail(token, error));
});

EM_JS(void, nazg_webhid_open, (uint32_t token, int handle), {
    const web = Module.nazgWebHid;
    const device = web.devices[handle];
    if (!device) {
        web.fail(token, 'no such device');
        return;
    }
    (device.opened ? Promise.resolve() : device.open()).then(() => _nazg_webhid_on_done(token),
                                                             (error) => web.fail(token, error));
});

EM_JS(void, nazg_webhid_write, (uint32_t token, int handle, const uint8_t* at, int length), {
    const web = Module.nazgWebHid;
    web.devices[handle].sendReport(0, web.bytes(at, length)).then(() => _nazg_webhid_on_done(token),
                                                                  (error) => web.fail(token, error));
});

EM_JS(void, nazg_webhid_time_out, (uint32_t token, int milliseconds), {
    setTimeout(() => _nazg_webhid_on_timeout(token), milliseconds);
});

// The farewell, if any, written and its answer waited for -- briefly -- then the device closed.
// Token 0: nobody waits for the end (Shutdown()).
EM_JS(void, nazg_webhid_close, (uint32_t token, int handle, const uint8_t* at, int length), {
    const web = Module.nazgWebHid;
    const device = web.devices[handle];
    const farewell = length > 0 ? web.bytes(at, length) : null;
    (async () => {
        try {
            if (farewell) {
                const answered = new Promise((resolve) => {
                    device.addEventListener('inputreport', resolve, { once: true });
                    setTimeout(resolve, 250);
                });
                await device.sendReport(0, farewell);
                await answered;
            }
        } catch (error) {
        }
        try {
            await device.close();
        } catch (error) {
        }
        if (token)
            _nazg_webhid_on_done(token);
    })();
});

namespace nazg
{
    namespace
    {
        // The browser's handle in a "webhid:<handle>" path, or none.
        std::optional<int> HandleOf(const std::string& path)
        {
            constexpr std::string_view c_Prefix = "webhid:";
            if (path.compare(0, c_Prefix.size(), c_Prefix) != 0)
                return std::nullopt;

            int         handle = -1;
            const char* first  = path.data() + c_Prefix.size();
            const char* last   = path.data() + path.size();
            const auto  parsed = std::from_chars(first, last, handle);
            if (parsed.ec != std::errc() || parsed.ptr != last || handle < 0)
                return std::nullopt;
            return handle;
        }
    }

    // The browser's answers, from the exported functions below. Each names the token of what it
    // answers; an answer to an operation already ended -- timed out, or shut down -- finds no
    // match and is dropped.
    struct WebHidEvents
    {
        static inline HidTransport* transport = nullptr;   // the one transport the page has

        static HidTransport::Operation* InService(uint32_t token)
        {
            if (transport == nullptr || !transport->m_InService || token != transport->m_Token)
                return nullptr;
            return transport->m_InService.get();
        }

        static void Enumerated(uint32_t token, const char* json)
        {
            HidTransport::Operation* const operation = InService(token);
            if (operation == nullptr)
                return;

            try
            {
                for (const nlohmann::json& entry : nlohmann::json::parse(json))
                {
                    HidDeviceInfo device;
                    device.path      = "webhid:" + std::to_string(entry.at("handle").get<int>());
                    device.product   = entry.at("product").get<std::string>();
                    device.vendorId  = entry.at("vendorId").get<uint16_t>();
                    device.productId = entry.at("productId").get<uint16_t>();
                    device.usagePage = entry.at("usagePage").get<uint16_t>();
                    device.usage     = entry.at("usage").get<uint16_t>();
                    operation->devices.push_back(std::move(device));
                }
            }
            catch (const std::exception& failure)
            {
                operation->devices.clear();
                operation->error = std::string("enumeration failed: ") + failure.what();
            }
            transport->Finish(transport->m_InService);
        }

        static void Chosen(uint32_t token, const char* path)
        {
            if (transport == nullptr || !transport->m_Choosing || token != transport->m_ChooseToken)
                return;

            transport->m_Choosing->chosen = path != nullptr ? path : "";
            transport->m_Completed.push_back(std::move(transport->m_Choosing));
            transport->m_Choosing.reset();
        }

        // Open: the device is open; Send, Close: done. Request and Series wait for the reply instead.
        static void Done(uint32_t token)
        {
            HidTransport::Operation* const operation = InService(token);
            if (operation == nullptr)
                return;

            switch (operation->kind)
            {
            case HidTransport::OperationKind::Open:
            {
                const DeviceId id = transport->m_NextDeviceId++;
                transport->m_WebDevices.emplace(id, *HandleOf(operation->path));
                operation->opened = id;
                transport->Finish(transport->m_InService);
                break;
            }
            case HidTransport::OperationKind::Send:
            case HidTransport::OperationKind::Close:
                transport->Finish(transport->m_InService);
                break;
            default:
                break;
            }
        }

        static void Failed(uint32_t token, const char* message)
        {
            const std::string said = message != nullptr ? message : "no further detail";

            if (transport != nullptr && transport->m_Choosing && token == transport->m_ChooseToken)
            {
                transport->m_Choosing->error = "the browser's device chooser failed: " + said;
                transport->m_Completed.push_back(std::move(transport->m_Choosing));
                transport->m_Choosing.reset();
                return;
            }

            HidTransport::Operation* const operation = InService(token);
            if (operation == nullptr)
                return;

            switch (operation->kind)
            {
            case HidTransport::OperationKind::Enumerate: operation->error = "enumeration failed: " + said; break;
            case HidTransport::OperationKind::Open: operation->error = "could not open " + operation->path + ": " + said; break;
            default:                                     operation->error = "write failed: " + said; break;
            }
            transport->Finish(transport->m_InService);
        }

        // A report from a device: the reply awaited, if it is from that device and passes the check.
        static void Report(int handle, const uint8_t* data, int length)
        {
            if (transport == nullptr || !transport->m_InService || !transport->m_IsWaiting)
                return;

            HidTransport::Operation& operation = *transport->m_InService;
            const auto               device    = transport->m_WebDevices.find(operation.device);
            if (device == transport->m_WebDevices.end() || device->second != handle)
                return;

            std::vector<uint8_t> reply(data, data + length);
            const bool           isSeries = operation.kind == HidTransport::OperationKind::Series;
            const size_t         index    = transport->m_SeriesIndex;
            const ReplyCheck&    isReply  = isSeries ? (index < operation.checks.size() ? operation.checks[index] : ReplyCheck())
                                                     : operation.isReply;
            if (isReply && !isReply(reply))
                return;   // not the reply: a report nobody asked for, arrived while waiting

            transport->m_IsWaiting = false;
            if (!isSeries)
            {
                operation.reply = std::move(reply);
                transport->Finish(transport->m_InService);
                return;
            }

            operation.replies.push_back(std::move(reply));
            if (++transport->m_SeriesIndex < operation.payloads.size())
                transport->WriteNextPayload();
            else
                transport->Finish(transport->m_InService);
        }

        static void TimedOut(uint32_t token)
        {
            HidTransport::Operation* const operation = InService(token);
            if (operation == nullptr || !transport->m_IsWaiting)
                return;

            operation->error = "device did not answer within " + std::to_string(operation->timeoutMs) + " ms";
            transport->Finish(transport->m_InService);
        }
    };
}

// What the browser's JavaScript calls, between two frames: never a coroutine resumed, nothing
// thrown -- Pump() resumes the coroutines.
extern "C"
{
    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_enumerated(uint32_t token, const char* json)
    {
        nazg::WebHidEvents::Enumerated(token, json);
    }

    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_chosen(uint32_t token, const char* path)
    {
        nazg::WebHidEvents::Chosen(token, path);
    }

    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_done(uint32_t token)
    {
        nazg::WebHidEvents::Done(token);
    }

    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_failed(uint32_t token, const char* message)
    {
        nazg::WebHidEvents::Failed(token, message);
    }

    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_report(int handle, const uint8_t* data, int length)
    {
        nazg::WebHidEvents::Report(handle, data, length);
    }

    EMSCRIPTEN_KEEPALIVE void nazg_webhid_on_timeout(uint32_t token)
    {
        nazg::WebHidEvents::TimedOut(token);
    }
}

namespace nazg
{
    HidTransport::HidTransport()
    {
        m_Running.store(true);
        m_IsAvailable          = nazg_webhid_start() != 0;
        WebHidEvents::transport = this;
    }

    bool HidTransport::IsAvailable() const noexcept
    {
        return m_IsAvailable;
    }

    uint32_t HidTransport::DeviceChangeCount() noexcept
    {
        return static_cast<uint32_t>(nazg_webhid_changes());
    }

    void HidTransport::Submit(OperationPtr operation)
    {
        if (!m_Running.load())
        {
            operation->error = "transport is shut down";   // the coroutine still unwinds
            m_Completed.push_back(std::move(operation));
            return;
        }

        // The chooser touches no device, and must be asked while the click still counts: not queued.
        if (operation->kind == OperationKind::Choose)
        {
            if (!m_IsAvailable)
                operation->error = "this browser has no WebHID";
            else if (m_Choosing)
                operation->error = "the browser's device chooser is already open";
            if (!operation->error.empty())
            {
                m_Completed.push_back(std::move(operation));
                return;
            }

            m_Choosing    = std::move(operation);
            m_ChooseToken = m_NextToken++;
            nazg_webhid_choose(m_ChooseToken, m_Choosing->usagePage, m_Choosing->usage);
            return;
        }

        m_Pending.push_back(std::move(operation));
        StartNext();
    }

    void HidTransport::StartNext()
    {
        if (m_IsStarting)
            return;   // the loop below goes on once Service() returns

        m_IsStarting = true;
        while (!m_InService && !m_Pending.empty())
        {
            m_InService = std::move(m_Pending.front());
            m_Pending.pop_front();
            Service(*m_InService);
        }
        m_IsStarting = false;
    }

    void HidTransport::Service(Operation& operation)
    {
        m_Token       = m_NextToken++;
        m_SeriesIndex = 0;
        m_IsWaiting   = false;

        const auto device = m_WebDevices.find(operation.device);
        const bool isOpen = device != m_WebDevices.end();

        switch (operation.kind)
        {
        case OperationKind::Enumerate:
            if (!m_IsAvailable)
                break;   // no device, and no error: an empty list
            nazg_webhid_enumerate(m_Token, operation.usagePage, operation.usage);
            return;

        case OperationKind::Open:
            if (!m_IsAvailable)
                operation.error = "could not open " + operation.path + ": this browser has no WebHID";
            else if (!HandleOf(operation.path))
                operation.error = "could not open " + operation.path + ": no such device";
            else
            {
                nazg_webhid_open(m_Token, *HandleOf(operation.path));
                return;
            }
            break;

        case OperationKind::Request:
        case OperationKind::Series:
            if (!isOpen)
                operation.error = "request for a device that is not open";
            else if (operation.kind == OperationKind::Request || !operation.payloads.empty())
            {
                WriteNextPayload();
                return;
            }
            break;

        case OperationKind::Send:
            if (!isOpen)
                operation.error = "send to a device that is not open";
            else
            {
                nazg_webhid_write(m_Token, device->second, operation.payload.data(), static_cast<int>(operation.payload.size()));
                return;
            }
            break;

        case OperationKind::Farewell:
            if (isOpen && operation.payload.empty())
                m_Farewells.erase(operation.device);
            else if (isOpen)
                m_Farewells[operation.device] = std::move(operation.payload);
            break;

        case OperationKind::Close:
            if (isOpen)
            {
                const int            handle   = device->second;
                std::vector<uint8_t> farewell = std::move(m_Farewells[operation.device]);
                m_Farewells.erase(operation.device);
                m_WebDevices.erase(device);
                nazg_webhid_close(m_Token, handle, farewell.data(), static_cast<int>(farewell.size()));
                return;
            }
            break;   // unknown or already closed: nothing to do

        case OperationKind::Choose:
            break;   // never queued: Submit() asks the browser
        }

        Finish(m_InService);
    }

    void HidTransport::WriteNextPayload()
    {
        Operation&                  operation = *m_InService;
        const std::vector<uint8_t>& payload   = operation.kind == OperationKind::Series ? operation.payloads[m_SeriesIndex]
                                                                                        : operation.payload;
        // A token per report, so a timer left from the one before cannot end this one.
        m_Token     = m_NextToken++;
        m_IsWaiting = true;
        nazg_webhid_time_out(m_Token, static_cast<int>(operation.timeoutMs));
        nazg_webhid_write(m_Token, m_WebDevices.at(operation.device), payload.data(), static_cast<int>(payload.size()));
    }

    void HidTransport::Finish(const OperationPtr& operation)
    {
        OperationPtr finished = operation;   // the caller's may be m_InService itself
        if (finished == m_InService)
        {
            m_InService.reset();
            m_IsWaiting = false;
        }
        m_Completed.push_back(std::move(finished));
        StartNext();
    }

    void HidTransport::Shutdown()
    {
        if (!m_Running.exchange(false))
            return;

        // Fail what is in the browser's hands and what waits behind it, then resume those
        // coroutines so they throw and unwind instead of leaking their frames.
        if (m_InService)
        {
            m_InService->error = "transport shut down while waiting for the device";
            m_Completed.push_back(std::move(m_InService));
            m_InService.reset();
        }
        if (m_Choosing)
        {
            m_Choosing->error = "transport shut down while the browser's chooser was open";
            m_Completed.push_back(std::move(m_Choosing));
            m_Choosing.reset();
        }
        for (OperationPtr& operation : m_Pending)
        {
            operation->error = "transport shut down before the operation was serviced";
            m_Completed.push_back(std::move(operation));
        }
        m_Pending.clear();

        // Whatever is still open, its farewell said -- nobody waits for the browser to finish.
        for (const auto& [id, handle] : m_WebDevices)
        {
            const auto farewell = m_Farewells.find(id);
            if (farewell != m_Farewells.end())
                nazg_webhid_close(0, handle, farewell->second.data(), static_cast<int>(farewell->second.size()));
            else
                nazg_webhid_close(0, handle, nullptr, 0);
        }
        m_WebDevices.clear();
        m_Farewells.clear();
        m_IsWaiting = false;

        if (WebHidEvents::transport == this)
            WebHidEvents::transport = nullptr;
        Pump();
    }
}
