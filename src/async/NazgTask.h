// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Task<T> - the coroutine return type for all asynchronous code in Nazg.
//
// This header contains NO threading and NO platform code, deliberately. A Task runs
// only on the thread that resumes it. That is what lets identical protocol code be
// driven by a worker thread natively and by WebHID promises under Emscripten, where
// std::thread is impractical. Keep it that way.
//
// Usage:
//     Task<int> ReadSomething(Transport& t)
//     {
//         auto reply = co_await t.SendReport(cmd);   // suspends, does not block
//         co_return Parse(reply);
//     }
//
// Errors propagate as exceptions: an awaiter's await_resume() may throw, the exception
// unwinds the coroutine normally (destructors run), and is re-thrown when the result is
// taken. See CLAUDE.md for why transport errors use exceptions.

#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace nazg
{
    // Internal: not used directly, only inherited by Task<T>::promise_type and
    // Task<void>::promise_type.
    //
    // Every coroutine needs a "promise" object, and the compiler requires it to provide
    // a fixed set of hooks: what to do at the start (initial_suspend), at the end
    // (final_suspend), and on an escaping exception (unhandled_exception). Those hooks
    // are identical for Task<T> and Task<void>, so they live here rather than twice.
    class TaskPromiseBase
    {
    public:
        // Resuming the awaiting coroutine by returning its handle (symmetric
        // transfer) instead of calling resume() keeps the stack flat, which matters
        // when a keymap read chains hundreds of awaits.
        struct FinalAwaiter
        {
            bool await_ready() const noexcept { return false; }

            template <typename TPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<TPromise> self) const noexcept
            {
                std::coroutine_handle<> continuation = self.promise().m_Continuation;
                return continuation ? continuation : std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        // Eager start: the body runs up to its first co_await immediately on call.
        std::suspend_never initial_suspend() const noexcept { return {}; }

        // Suspending at the end keeps the frame (and the result) alive until the
        // caller takes it. The Task destructor frees the frame.
        FinalAwaiter final_suspend() const noexcept { return {}; }

        void unhandled_exception() noexcept { m_Exception = std::current_exception(); }

        void SetContinuation(std::coroutine_handle<> continuation) noexcept
        {
            m_Continuation = continuation;
        }

    protected:
        void RethrowIfFailed() const
        {
            if (m_Exception)
                std::rethrow_exception(m_Exception);
        }

        std::coroutine_handle<> m_Continuation;
        std::exception_ptr      m_Exception;
    };

    template <typename T>
    class Task
    {
    public:
        class promise_type : public TaskPromiseBase
        {
        public:
            Task get_return_object() noexcept
            {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            template <typename TValue>
            void return_value(TValue&& value)
            {
                m_Value.emplace(std::forward<TValue>(value));
            }

            T TakeResult()
            {
                RethrowIfFailed();

                if (!m_Value)
                    throw std::logic_error("Task: result taken before the coroutine completed, or taken twice");

                // Move the value out rather than out of the optional in place, so a
                // second call throws instead of returning a moved-from object.
                std::optional<T> taken;
                taken.swap(m_Value);
                return std::move(*taken);
            }

        private:
            std::optional<T> m_Value;
        };

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> handle) noexcept : m_Handle(handle) {}

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, {})) {}

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                Destroy();
                m_Handle = std::exchange(other.m_Handle, {});
            }
            return *this;
        }

        ~Task() { Destroy(); }

        bool IsValid() const noexcept { return static_cast<bool>(m_Handle); }
        bool IsDone()  const noexcept { return m_Handle && m_Handle.done(); }

        // For non-coroutine callers (the ImGui frame loop): poll IsDone(), then take.
        T TakeResult()
        {
            if (!m_Handle)
                throw std::logic_error("Task: TakeResult() on an empty task");
            if (!m_Handle.done())
                throw std::logic_error("Task: TakeResult() before the coroutine completed");

            return m_Handle.promise().TakeResult();
        }

        auto operator co_await() noexcept
        {
            struct Awaiter
            {
                std::coroutine_handle<promise_type> handle;

                bool await_ready() const noexcept { return !handle || handle.done(); }

                void await_suspend(std::coroutine_handle<> awaiting) const noexcept
                {
                    handle.promise().SetContinuation(awaiting);
                }

                T await_resume() const
                {
                    // await_ready() reports true for an empty handle, so guard here
                    // rather than dereferencing a null coroutine handle.
                    if (!handle)
                        throw std::logic_error("Task: co_await on an empty task");

                    return handle.promise().TakeResult();
                }
            };

            return Awaiter{ m_Handle };
        }

    private:
        void Destroy() noexcept
        {
            // Destroying a task whose coroutine is still suspended frees a frame that
            // something else -- the transport -- may still hold a handle to; the next
            // resume() would then run on freed memory. This only detects the mistake:
            // making it safe needs cancellation, which belongs with SendReport().
            assert((!m_Handle || m_Handle.done()) &&
                   "destroying a Task whose coroutine is still suspended");

            if (m_Handle)
                m_Handle.destroy();
        }

        std::coroutine_handle<promise_type> m_Handle;
    };

    template <>
    class Task<void>
    {
    public:
        class promise_type : public TaskPromiseBase
        {
        public:
            Task get_return_object() noexcept
            {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            void return_void() noexcept {}

            void TakeResult() const { RethrowIfFailed(); }
        };

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> handle) noexcept : m_Handle(handle) {}

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, {})) {}

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                Destroy();
                m_Handle = std::exchange(other.m_Handle, {});
            }
            return *this;
        }

        ~Task() { Destroy(); }

        bool IsValid() const noexcept { return static_cast<bool>(m_Handle); }
        bool IsDone()  const noexcept { return m_Handle && m_Handle.done(); }

        void TakeResult()
        {
            if (!m_Handle)
                throw std::logic_error("Task: TakeResult() on an empty task");
            if (!m_Handle.done())
                throw std::logic_error("Task: TakeResult() before the coroutine completed");

            m_Handle.promise().TakeResult();
        }

        auto operator co_await() noexcept
        {
            struct Awaiter
            {
                std::coroutine_handle<promise_type> handle;

                bool await_ready() const noexcept { return !handle || handle.done(); }

                void await_suspend(std::coroutine_handle<> awaiting) const noexcept
                {
                    handle.promise().SetContinuation(awaiting);
                }

                void await_resume() const
                {
                    if (!handle)
                        throw std::logic_error("Task: co_await on an empty task");

                    handle.promise().TakeResult();
                }
            };

            return Awaiter{ m_Handle };
        }

    private:
        void Destroy() noexcept
        {
            // Destroying a task whose coroutine is still suspended frees a frame that
            // something else -- the transport -- may still hold a handle to; the next
            // resume() would then run on freed memory. This only detects the mistake:
            // making it safe needs cancellation, which belongs with SendReport().
            assert((!m_Handle || m_Handle.done()) &&
                   "destroying a Task whose coroutine is still suspended");

            if (m_Handle)
                m_Handle.destroy();
        }

        std::coroutine_handle<promise_type> m_Handle;
    };
}
