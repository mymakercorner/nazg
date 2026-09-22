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
// The later cases pin the rest of the lifecycle around those two: submitting once the
// transport is already down, calling Shutdown() twice (the destructor does, after
// Main.cpp already did), and Pump()'s pass limit giving up on a resubmitting sequence
// without losing it.
//
// No test framework, deliberately: that choice is still open and this needs none.
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "async/NazgTask.h"
#include "transport/NazgHidTransport.h"

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

    int g_completed      = 0;
    int g_failed         = 0;
    int g_retryDone      = 0;
    int g_resolvedAwaits = 0;

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

    // Awaits the transport over and over, which is how a protocol sequence behaves.
    // Used to walk Pump() past its pass limit within a single call.
    Task<void> Resubmitter(HidTransport& transport, int awaitCount)
    {
        for (int i = 0; i < awaitCount; ++i)
        {
            try
            {
                co_await transport.Enumerate();
            }
            catch (const HidTransportError&)
            {
            }

            ++g_resolvedAwaits;
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

    // A request submitted after shutdown must fail immediately rather than sit in a
    // queue no worker will ever service. Without this the coroutine never resumes and
    // its frame leaks -- the same freeze as an unhandled disconnect.
    void TestSubmitAfterShutdown()
    {
        std::printf("submit after shutdown\n");

        g_completed = 0;
        g_failed    = 0;

        HidTransport transport;
        transport.Shutdown();

        Check(!transport.IsRunning(), "the transport reports itself shut down");

        Task<void> probe = Probe(transport);

        Check(!probe.IsDone(), "the request is completed, but resumption waits for Pump()");

        transport.Pump();

        Check(probe.IsDone(), "the coroutine was resumed and unwound");
        Check(g_failed == 1 && g_completed == 0, "the request failed exactly once");
    }

    // Shutdown() is called by the destructor and also explicitly from Main.cpp, so the
    // second call has to be a no-op: joining an already-joined thread terminates the
    // process, and re-failing requests would resume coroutines twice.
    void TestShutdownIsIdempotent()
    {
        std::printf("shutdown is idempotent\n");

        g_completed = 0;
        g_failed    = 0;

        Task<void> probe;

        {
            HidTransport transport;
            probe = Probe(transport);

            transport.Shutdown();
            transport.Shutdown();   // must return immediately, not join or fail twice
            transport.Pump();       // must find nothing left to resume
        }

        Check(probe.IsDone(), "the request was resolved");
        Check(g_completed + g_failed == 1, "the coroutine was resumed exactly once");
    }

    // Pump() caps its passes so one frame cannot spin forever on a coroutine that
    // resubmits on every resume. The cap must BAIL OUT, not drop work: the next Pump()
    // has to pick the sequence up where it stopped. Shutting the transport down first
    // makes every completion immediate, so the whole case is deterministic.
    //
    // This case trips the cap on purpose, so the "still had work after N passes" errors
    // SDL logs during the run are expected output, not a failure.
    void TestPumpGuardBailsOutWithoutLosingWork()
    {
        std::printf("pump guard bails out without losing work\n");

        g_resolvedAwaits = 0;

        // Comfortably more than Pump()'s internal pass limit, without depending on its
        // exact value.
        constexpr int c_AwaitCount = 500;

        HidTransport transport;
        transport.Shutdown();

        Task<void> task = Resubmitter(transport, c_AwaitCount);

        transport.Pump();

        const int afterFirstPump = g_resolvedAwaits;

        Check(!task.IsDone(), "one Pump() gave up instead of spinning until the sequence ended");
        Check(afterFirstPump > 0, "it still made progress before giving up");

        int pumps = 1;
        while (!task.IsDone() && pumps < 100)
        {
            transport.Pump();
            ++pumps;
        }

        Check(task.IsDone(), "later Pump() calls resumed the sequence where it stopped");
        Check(g_resolvedAwaits == c_AwaitCount, "every await resolved exactly once, none dropped");
        Check(pumps > 1, "the sequence needed more than one frame, as the guard intends");
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
    TestSubmitAfterShutdown();
    TestShutdownIsIdempotent();
    TestPumpGuardBailsOutWithoutLosingWork();

    std::printf("%s (%d failure(s))\n", g_failureCount == 0 ? "PASSED" : "FAILED", g_failureCount);
    return g_failureCount == 0 ? 0 : 1;
}
