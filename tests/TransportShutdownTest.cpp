// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins HidTransport's shutdown contract: every outstanding request must be FAILED and
// its coroutine resumed exactly once, so awaiting coroutines unwind rather than leaking
// their frames. A frozen UI on device disconnect is the symptom when this breaks.
//
// Both cases below were real bugs, found in review before the code was first committed:
//
//   1. The worker drained the queue after shutdown was requested instead of abandoning
//      it, so the fail-all-pending path was unreachable and exit blocked on one
//      blocking hidapi call per queued request.
//
//   2. Pump() drained a single batch, so a coroutine that submitted new work while
//      being resumed inside Pump() was never resumed again. Shutdown() calls Pump()
//      last, so nothing would ever pick that work up.
//
// No test framework, deliberately: that choice is still open and this needs none.
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "async/Task.h"
#include "transport/HidTransport.h"

#include <cstdio>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

using nazg::HidTransport;
using nazg::HidTransportError;
using nazg::Task;

namespace
{
    int g_failureCount = 0;

    void Check(bool condition, const char* description)
    {
        std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", description);
        if (!condition)
            ++g_failureCount;
    }

    int g_completed = 0;
    int g_failed    = 0;
    int g_retryDone = 0;

    Task<void> Probe(HidTransport& transport)
    {
        try
        {
            co_await transport.Enumerate();
            ++g_completed;
        }
        catch (const HidTransportError&)
        {
            ++g_failed;
        }
    }

    // Note the shape: co_await is ILLEGAL inside a catch block ([expr.await]), so the
    // handler only records the failure and the retry happens after it.
    Task<void> RetryProbe(HidTransport& transport)
    {
        bool firstFailed = false;

        try
        {
            co_await transport.Enumerate();
        }
        catch (const HidTransportError&)
        {
            firstFailed = true;
        }

        if (firstFailed)
        {
            try
            {
                co_await transport.Enumerate();   // submitted from inside Pump()
            }
            catch (const HidTransportError&)
            {
                ++g_retryDone;                    // only reached if resumed a second time
            }
        }
    }

    // Shutdown must fail queued work rather than service it, and must leave nothing
    // suspended. How many of the eight were already in flight depends on timing, so
    // this asserts the properties that do not: nothing leaks, and the fail path runs.
    void TestShutdownFailsQueuedRequests()
    {
        std::printf("shutdown fails queued requests\n");

        g_completed = 0;
        g_failed    = 0;

        constexpr int c_ProbeCount = 8;
        std::vector<Task<void>> probes;

        {
            HidTransport transport;
            for (int i = 0; i < c_ProbeCount; ++i)
                probes.push_back(Probe(transport));

            transport.Shutdown();
        }

        int unresolved = 0;
        for (const Task<void>& probe : probes)
            if (!probe.IsDone())
                ++unresolved;

        Check(unresolved == 0, "every coroutine was resumed and unwound");
        Check(g_completed + g_failed == c_ProbeCount, "every request was resolved exactly once");
        Check(g_failed > 0, "queued requests were failed, not serviced");
    }

    // Pump() must keep draining while resumed coroutines queue more work. With the
    // transport already shut down, Submit() completes immediately, so a single Pump()
    // has to resolve both awaits. No worker thread is involved: fully deterministic.
    void TestPumpResolvesWorkQueuedWhileResuming()
    {
        std::printf("pump resolves work queued while resuming\n");

        g_retryDone = 0;

        HidTransport transport;
        transport.Shutdown();

        Task<void> retry = RetryProbe(transport);

        transport.Pump();

        Check(retry.IsDone(), "coroutine that awaited again from inside Pump() completed");
        Check(g_retryDone == 1, "the second await was resumed exactly once");
    }
}

int main()
{
#ifdef _MSC_VER
    // A failing assert (Task::Destroy() has one) must fail the test, not open a modal
    // dialog that blocks the run forever on CI or on a developer's machine.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (int reportType : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
    }
#endif

    TestShutdownFailsQueuedRequests();
    TestPumpResolvesWorkQueuedWhileResuming();

    std::printf("%s (%d failure(s))\n", g_failureCount == 0 ? "PASSED" : "FAILED", g_failureCount);
    return g_failureCount == 0 ? 0 : 1;
}
