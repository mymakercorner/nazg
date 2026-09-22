// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Covers Open/Request/Close -- the error paths that need no keyboard plugged in:
// opening a path that is not there, using a device id the transport never handed out,
// closing an id it does not know, and doing any of it after shutdown. Every one of
// them has to surface as a HidTransportError the awaiting coroutine can catch, and
// must leave the transport usable.
//
// NOT covered here, because they need a real device that misbehaves on cue:
//   - a request that times out waiting for an answer
//   - a device unplugged mid-sequence, failing the write or the read
// Both paths exist in ExecuteRequest(); verifying them needs either hardware or a
// substitutable device layer, which the transport deliberately does not have.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "async/NazgTask.h"
#include "transport/NazgHidTransport.h"

#include "TestSupport.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using nazg::DeviceId;
using nazg::HidTransport;
using nazg::HidTransportError;
using nazg::Task;

namespace
{
    // No device has this path on any platform, so hid_open_path() fails for the
    // ordinary reason rather than for a permissions or busy-device reason.
    constexpr char c_NoSuchPath[] = "nazg-no-such-device";

    // An id the transport cannot have handed out: ids start at 1 and only ever go up.
    constexpr DeviceId c_NeverOpened = 4242;

    std::string g_openError;
    std::string g_requestError;
    std::string g_enumerateError;
    int         g_resolved = 0;

    // These cases run against the real worker thread, so they have to wait for it
    // rather than assume a timing. Shutting the transport down instead would resolve
    // the operation with the WRONG error -- "shut down before it was serviced" --
    // which is exactly what these cases must not see.
    //
    // A task still suspended when the wait expires would trip the assert in ~Task(),
    // so the caller shuts the transport down before reporting the failure.
    bool PumpUntilDone(HidTransport& transport, const Task<void>& task)
    {
        constexpr int c_MaxWaitMs = 2000;
        constexpr int c_StepMs    = 5;

        for (int elapsed = 0; elapsed < c_MaxWaitMs && !task.IsDone(); elapsed += c_StepMs)
        {
            transport.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(c_StepMs));
        }

        transport.Pump();
        return task.IsDone();
    }

    // The shape protocol code will have. Open failing has to skip the rest of the
    // sequence without leaving a device behind -- there is nothing to close, because
    // the open never produced an id.
    Task<void> OpenAndRequest(HidTransport& transport, std::string path)
    {
        DeviceId device = nazg::c_InvalidDevice;

        try
        {
            device = co_await transport.Open(std::move(path));
        }
        catch (const HidTransportError& failure)
        {
            g_openError = failure.what();
        }

        if (device == nazg::c_InvalidDevice)
        {
            ++g_resolved;
            co_return;
        }

        try
        {
            co_await transport.Request(device, std::vector<uint8_t>(32, 0));
        }
        catch (const HidTransportError& failure)
        {
            g_requestError = failure.what();
        }

        transport.Close(device);
        ++g_resolved;
    }

    Task<void> RequestOnly(HidTransport& transport, DeviceId device)
    {
        try
        {
            co_await transport.Request(device, std::vector<uint8_t>(32, 0));
        }
        catch (const HidTransportError& failure)
        {
            g_requestError = failure.what();
        }

        ++g_resolved;
    }

    Task<void> EnumerateOnly(HidTransport& transport)
    {
        try
        {
            co_await transport.Enumerate();
        }
        catch (const HidTransportError& failure)
        {
            g_enumerateError = failure.what();
        }

        ++g_resolved;
    }

    void TestOpenFailureReachesTheCaller()
    {
        std::printf("open failure reaches the caller\n");

        g_openError.clear();
        g_requestError.clear();
        g_resolved = 0;

        HidTransport transport;
        Task<void>   task = OpenAndRequest(transport, c_NoSuchPath);

        const bool completed = PumpUntilDone(transport, task);
        transport.Shutdown();

        Check(completed, "the coroutine was resumed and unwound");
        Check(g_resolved == 1, "the sequence resolved exactly once");
        Check(!g_openError.empty(), "the open failure surfaced as an exception");
        Check(g_openError.find(c_NoSuchPath) != std::string::npos,
              "the message names the path that could not be opened");
        Check(g_requestError.empty(), "the request was skipped after the failed open");
    }

    // A stale id -- one from a device that was closed, or never opened at all -- must
    // be rejected. Ids are never reused, so this can never silently hit some other
    // device that happens to be open now.
    void TestRequestForUnknownDevice()
    {
        std::printf("request for an unknown device\n");

        g_requestError.clear();
        g_resolved = 0;

        HidTransport transport;
        Task<void>   task = RequestOnly(transport, c_NeverOpened);

        const bool completed = PumpUntilDone(transport, task);
        transport.Shutdown();

        Check(completed, "the coroutine was resumed and unwound");
        Check(g_resolved == 1, "the request resolved exactly once");
        Check(g_requestError.find("not open") != std::string::npos,
              "the id was rejected as not open, rather than reaching a device");
    }

    // Close is fire-and-forget, so a wrong id has no way to report anything -- it must
    // simply do nothing, and above all must not take the worker down with it.
    void TestCloseOfUnknownDeviceIsHarmless()
    {
        std::printf("close of an unknown device is harmless\n");

        g_enumerateError.clear();
        g_resolved = 0;

        HidTransport transport;

        transport.Close(c_NeverOpened);
        transport.Close(nazg::c_InvalidDevice);

        // The transport still works afterwards: enumeration is queued behind those two
        // closes, so it only succeeds if the worker serviced them and carried on.
        Task<void> task = EnumerateOnly(transport);

        const bool completed = PumpUntilDone(transport, task);
        transport.Shutdown();

        Check(completed, "the coroutine was resumed and unwound");
        Check(g_enumerateError.empty(), "the transport kept working after two bogus closes");
    }

    // Everything submitted after Shutdown() has to fail immediately rather than queue
    // for a worker that is gone. Close has no continuation, so it must be dropped
    // without anyone trying to resume it.
    void TestOperationsAfterShutdown()
    {
        std::printf("operations after shutdown\n");

        g_openError.clear();
        g_requestError.clear();
        g_resolved = 0;

        HidTransport transport;
        transport.Shutdown();

        Task<void> opened    = OpenAndRequest(transport, c_NoSuchPath);
        Task<void> requested = RequestOnly(transport, c_NeverOpened);
        transport.Close(c_NeverOpened);

        transport.Pump();

        Check(opened.IsDone() && requested.IsDone(), "both coroutines were resumed and unwound");
        Check(g_resolved == 2, "each sequence resolved exactly once");
        Check(!g_openError.empty(), "the open failed instead of queueing forever");
        Check(!g_requestError.empty(), "the request failed instead of queueing forever");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestOpenFailureReachesTheCaller();
    TestRequestForUnknownDevice();
    TestCloseOfUnknownDeviceIsHarmless();
    TestOperationsAfterShutdown();

    return TestResult();
}
