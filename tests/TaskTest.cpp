// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the invariants of Task<T> and Task<void>. Nothing here touches hidapi, SDL or a
// thread: every suspension is driven by hand through ManualEvent below, so each case is
// fully deterministic and fails the same way on every platform.
//
// These double as worked examples of the coroutine mechanics in src/async/NazgTask.h.
// The invariant each case pins, and the symptom when it breaks:
//
//   eager initial_suspend     the body runs up to its first co_await on CALL, so a
//                             coroutine with no co_await is already done when it returns
//   final_suspend suspends    the frame -- and the result inside it -- stays alive after
//                             the body ends, until the Task destructor frees it. Return
//                             std::suspend_never there instead and TakeResult() reads
//                             freed memory
//   symmetric transfer        resuming the innermost coroutine of a chain must unwind
//                             the whole chain WITHOUT growing the stack; a keymap read
//                             is hundreds of chained awaits (TestDeepChainStaysFlat)
//   unhandled_exception       an exception escaping the body is stored, not thrown from
//                             resume(), and re-thrown when the caller takes the result
//   frame destroyed once      moves must leave the source empty, and move-assignment
//                             must free the frame it overwrites
//
// No test framework, deliberately -- see tests/TransportShutdownTest.cpp.
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "async/NazgTask.h"

#include <coroutine>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

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

    // ---------------------------------------------------------------------------------
    // Test doubles
    // ---------------------------------------------------------------------------------

    // Stands in for the transport: suspends whoever awaits it, keeps the handle, and
    // resumes it when the test says so. This is the same shape as
    // HidTransport::EnumerateOperation minus the thread -- await_suspend() stores the
    // continuation, and something outside resumes it later.
    class ManualEvent
    {
    public:
        struct Awaiter
        {
            ManualEvent& event;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting) const noexcept
            {
                event.m_Waiter = awaiting;
            }

            // Throwing from await_resume() is how the transport reports errors: the
            // throw happens inside the awaiting coroutine, so its locals unwind.
            int await_resume() const
            {
                if (!event.m_Error.empty())
                    throw std::runtime_error(event.m_Error);

                return event.m_Value;
            }
        };

        Awaiter Wait() noexcept { return Awaiter{ *this }; }

        bool IsWaiting() const noexcept { return static_cast<bool>(m_Waiter); }

        void ResumeWith(int value)
        {
            m_Value = value;
            Resume();
        }

        void ResumeWithError(std::string error)
        {
            m_Error = std::move(error);
            Resume();
        }

    private:
        void Resume()
        {
            // Clear before resuming: the resumed coroutine may await this event again,
            // which would otherwise overwrite a handle we are about to use.
            std::coroutine_handle<> waiter = std::exchange(m_Waiter, {});
            if (waiter)
                waiter.resume();
        }

        std::coroutine_handle<> m_Waiter;
        int                     m_Value = 0;
        std::string             m_Error;
    };

    int g_probeDestroyCount = 0;

    // Counts its own destruction, but only for the instance that still owns the "live"
    // flag, so moving one into a coroutine frame does not double-count.
    struct DestructorProbe
    {
        DestructorProbe() = default;

        DestructorProbe(DestructorProbe&& other) noexcept { other.m_Live = false; }

        DestructorProbe& operator=(DestructorProbe&& other) noexcept
        {
            if (this != &other)
                m_Live = std::exchange(other.m_Live, false);
            return *this;
        }

        DestructorProbe(const DestructorProbe&)            = delete;
        DestructorProbe& operator=(const DestructorProbe&) = delete;

        ~DestructorProbe()
        {
            if (m_Live)
                ++g_probeDestroyCount;
        }

        bool m_Live = true;
    };

    // ---------------------------------------------------------------------------------
    // Coroutines under test
    // ---------------------------------------------------------------------------------

    Task<int> Immediate(int value)
    {
        co_return value;
    }

    Task<int> Doubler(ManualEvent& event)
    {
        co_return (co_await event.Wait()) * 2;
    }

    // A local whose destructor must run while the exception from await_resume() unwinds
    // the frame -- proving the coroutine unwinds like an ordinary function.
    Task<int> UnwindsOnError(ManualEvent& event)
    {
        DestructorProbe probe;
        co_return co_await event.Wait();
    }

    Task<int> ThrowsBeforeFirstSuspend()
    {
        throw std::runtime_error("thrown before any co_await");
        co_return 0;   // unreachable; present only to make this a coroutine
    }

    // The parameter lives in the coroutine FRAME, so its destructor runs when the frame
    // is destroyed -- after the body has finished, not when it returns.
    Task<int> ConsumesProbe(DestructorProbe probe, int value)
    {
        co_return value;
    }

    Task<int> Outer(ManualEvent& event)
    {
        // The inner Task is a local of this frame: it cannot outlive the coroutine
        // awaiting it, which is the safe way to hold a Task across a suspension.
        Task<int> inner = Doubler(event);
        co_return (co_await inner) + 1;
    }

    Task<int> AwaitsEmptyTask(std::string& message)
    {
        Task<int> empty;

        // co_await is legal in a try block (it is NOT legal in a catch handler).
        try
        {
            co_await empty;
        }
        catch (const std::logic_error& error)
        {
            message = error.what();
        }

        co_return 0;
    }

    Task<void> VoidWaiter(ManualEvent& event, bool& completed)
    {
        co_await event.Wait();
        completed = true;
    }

    Task<void> VoidOuter(ManualEvent& event, bool& innerCompleted, bool& outerCompleted)
    {
        Task<void> inner = VoidWaiter(event, innerCompleted);
        co_await inner;
        outerCompleted = true;
    }

    // The two halves of the deep chain. AddOne takes a REFERENCE held across a
    // suspension, which is only safe because the caller guarantees the referent
    // outlives the chain -- see TestDeepChainStaysFlat.
    Task<int> Bottom(ManualEvent& event)
    {
        co_return co_await event.Wait();
    }

    Task<int> AddOne(Task<int>& inner)
    {
        co_return (co_await inner) + 1;
    }

    // ---------------------------------------------------------------------------------
    // Cases
    // ---------------------------------------------------------------------------------

    // Eager initial_suspend: no co_await means no suspension, so the task is finished by
    // the time the call returns. TakeResult() moves the value out, so a second call must
    // complain rather than hand back a moved-from object.
    void TestSynchronousCompletion()
    {
        std::printf("synchronous completion\n");

        Task<int> task = Immediate(7);

        Check(task.IsValid(), "the task owns a coroutine frame");
        Check(task.IsDone(), "a coroutine with no co_await is done on return");
        Check(task.TakeResult() == 7, "the result survives final_suspend");

        bool threw = false;
        try
        {
            task.TakeResult();
        }
        catch (const std::logic_error&)
        {
            threw = true;
        }

        Check(threw, "taking the result twice throws instead of returning a moved-from value");
    }

    void TestTakeResultBeforeCompletion()
    {
        std::printf("take result before completion\n");

        ManualEvent event;
        Task<int>   task = Doubler(event);

        bool threw = false;
        try
        {
            task.TakeResult();
        }
        catch (const std::logic_error&)
        {
            threw = true;
        }

        Check(threw, "TakeResult() on a suspended task throws");

        // Resume before the task goes out of scope: destroying a suspended coroutine is
        // the bug Task::Destroy() asserts on.
        event.ResumeWith(1);
        Check(task.IsDone(), "the task completed once resumed");
    }

    void TestEmptyTask()
    {
        std::printf("empty task\n");

        Task<int> empty;

        Check(!empty.IsValid(), "a default-constructed task owns nothing");
        Check(!empty.IsDone(), "an empty task is not done");

        bool threw = false;
        try
        {
            empty.TakeResult();
        }
        catch (const std::logic_error&)
        {
            threw = true;
        }

        Check(threw, "TakeResult() on an empty task throws");

        // await_ready() reports true for an empty handle, so the diagnostic has to come
        // from await_resume() rather than from a null-handle resume().
        std::string message;
        Task<int>   task = AwaitsEmptyTask(message);

        Check(task.IsDone(), "awaiting an empty task did not suspend");
        Check(!message.empty(), "co_await on an empty task throws a diagnostic");
    }

    void TestSuspendAndResume()
    {
        std::printf("suspend and resume\n");

        ManualEvent event;
        Task<int>   task = Doubler(event);

        Check(!task.IsDone(), "the task is suspended at its co_await");
        Check(event.IsWaiting(), "await_suspend() handed the handle to the awaiter");

        event.ResumeWith(21);

        Check(task.IsDone(), "resuming the awaiter ran the task to completion");
        Check(!event.IsWaiting(), "the continuation was consumed exactly once");
        Check(task.TakeResult() == 42, "the value came back through await_resume()");
    }

    // An exception thrown by an awaiter must unwind the coroutine's locals, be stored by
    // unhandled_exception(), and surface at the caller -- not at resume().
    void TestExceptionPropagation()
    {
        std::printf("exception propagation\n");

        g_probeDestroyCount = 0;

        ManualEvent event;
        Task<int>   task = UnwindsOnError(event);

        event.ResumeWithError("device went away");

        Check(g_probeDestroyCount == 1, "the coroutine's locals were destroyed by the unwind");
        Check(task.IsDone(), "a failed coroutine is done, not stuck");

        std::string message;
        try
        {
            task.TakeResult();
        }
        catch (const std::runtime_error& error)
        {
            message = error.what();
        }

        Check(message == "device went away", "the exception was re-thrown to the caller");
    }

    // Eager start means the body can throw before the caller has even been handed its
    // Task. The promise already exists, so the exception is stored, not lost.
    void TestExceptionBeforeFirstSuspend()
    {
        std::printf("exception before the first suspend\n");

        Task<int> task = ThrowsBeforeFirstSuspend();

        Check(task.IsDone(), "the task is done even though it never suspended");

        bool threw = false;
        try
        {
            task.TakeResult();
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }

        Check(threw, "the exception reached the caller");
    }

    void TestFrameDestroyedExactlyOnce()
    {
        std::printf("frame destroyed exactly once\n");

        g_probeDestroyCount = 0;
        {
            Task<int> task = ConsumesProbe(DestructorProbe{}, 5);

            Check(task.IsDone(), "the task completed");
            Check(g_probeDestroyCount == 0, "final_suspend keeps the frame alive after the body ends");
            Check(task.TakeResult() == 5, "the result is readable after the body ended");
        }
        Check(g_probeDestroyCount == 1, "the destructor freed the frame exactly once");

        // A moved-from task must be empty, or both would destroy the same frame.
        g_probeDestroyCount = 0;
        {
            Task<int> source = ConsumesProbe(DestructorProbe{}, 5);
            Task<int> moved  = std::move(source);

            Check(!source.IsValid(), "the moved-from task owns nothing");
            Check(moved.IsValid(), "the moved-to task owns the frame");
        }
        Check(g_probeDestroyCount == 1, "a move does not duplicate ownership of the frame");

        // Move-assignment overwrites a frame this task still owns; that frame has to be
        // freed, or it leaks silently.
        g_probeDestroyCount = 0;
        {
            Task<int> task = ConsumesProbe(DestructorProbe{}, 5);
            task           = Immediate(9);

            Check(g_probeDestroyCount == 1, "move-assignment destroyed the overwritten frame");
            Check(task.TakeResult() == 9, "the task now holds the assigned coroutine");
        }
        Check(g_probeDestroyCount == 1, "the overwritten frame was not destroyed twice");
    }

    // A Task is itself awaitable: awaiting one registers the awaiting coroutine as the
    // inner task's continuation, and final_suspend resumes it.
    void TestNestedAwait()
    {
        std::printf("nested await\n");

        ManualEvent event;
        Task<int>   task = Outer(event);

        Check(!task.IsDone(), "the outer task is suspended on the inner one");
        Check(event.IsWaiting(), "the suspension reached the bottom of the chain");

        event.ResumeWith(20);

        Check(task.IsDone(), "completing the inner task resumed the outer one");
        Check(task.TakeResult() == 41, "the value passed through both levels");
    }

    void TestNestedExceptionPropagation()
    {
        std::printf("nested exception propagation\n");

        ManualEvent event;
        Task<int>   task = Outer(event);

        event.ResumeWithError("inner failed");

        Check(task.IsDone(), "the whole chain unwound");

        std::string message;
        try
        {
            task.TakeResult();
        }
        catch (const std::runtime_error& error)
        {
            message = error.what();
        }

        Check(message == "inner failed", "the exception crossed the nested co_await");
    }

    // Task<void> is a separate specialisation, so every invariant above has to be
    // re-pinned for it rather than assumed.
    void TestTaskVoid()
    {
        std::printf("Task<void>\n");

        {
            ManualEvent event;
            bool        completed = false;
            Task<void>  task      = VoidWaiter(event, completed);

            Check(!task.IsDone(), "the void task is suspended");

            event.ResumeWith(0);

            Check(task.IsDone() && completed, "the void task ran to completion");
            task.TakeResult();   // must not throw
        }

        {
            ManualEvent event;
            bool        completed = false;
            Task<void>  task      = VoidWaiter(event, completed);

            event.ResumeWithError("void failed");

            bool threw = false;
            try
            {
                task.TakeResult();
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }

            Check(threw, "a failed void task re-throws at the caller");
            Check(!completed, "the body after the failed co_await did not run");
        }

        {
            ManualEvent event;
            bool        innerCompleted = false;
            bool        outerCompleted = false;
            Task<void>  task           = VoidOuter(event, innerCompleted, outerCompleted);

            Check(!task.IsDone(), "the nested void chain is suspended");

            event.ResumeWith(0);

            Check(innerCompleted && outerCompleted, "both levels of the void chain ran");
            Check(task.IsDone(), "the nested void task completed");
        }
    }

    // THE case that justifies symmetric transfer. Resuming the innermost coroutine has
    // to unwind all c_ChainDepth levels on a flat stack: FinalAwaiter::await_suspend()
    // RETURNS the continuation handle so the compiler tail-transfers to it. Change it to
    // call continuation.resume() instead and every level nests -- that overflows the
    // stack, so this case crashes rather than fails, which is the point.
    //
    // The chain is held in a reserved vector, not as nested locals, for two reasons: the
    // references AddOne() holds across its suspension stay valid, and the frames are
    // destroyed by a flat loop instead of a c_ChainDepth-deep destructor recursion.
    void TestDeepChainStaysFlat()
    {
        std::printf("deep chain stays flat\n");

        constexpr int c_ChainDepth = 20000;

        ManualEvent            event;
        std::vector<Task<int>> chain;
        chain.reserve(c_ChainDepth + 1);

        chain.push_back(Bottom(event));
        for (int level = 0; level < c_ChainDepth; ++level)
            chain.push_back(AddOne(chain.back()));

        Check(!chain.back().IsDone(), "the whole chain is suspended on one event");
        Check(event.IsWaiting(), "only the innermost coroutine is waiting");

        event.ResumeWith(0);   // one resume, c_ChainDepth + 1 frames to unwind

        Check(chain.back().IsDone(), "a single resume unwound the whole chain");
        Check(chain.back().TakeResult() == c_ChainDepth, "every level ran exactly once");
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

    TestSynchronousCompletion();
    TestTakeResultBeforeCompletion();
    TestEmptyTask();
    TestSuspendAndResume();
    TestExceptionPropagation();
    TestExceptionBeforeFirstSuspend();
    TestFrameDestroyedExactlyOnce();
    TestNestedAwait();
    TestNestedExceptionPropagation();
    TestTaskVoid();
    TestDeepChainStaysFlat();

    std::printf("%s (%d failure(s))\n", g_failureCount == 0 ? "PASSED" : "FAILED", g_failureCount);
    return g_failureCount == 0 ? 0 : 1;
}
