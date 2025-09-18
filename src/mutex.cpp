#include "coro/mutex.hpp"
#include "coro/detail/awaiter_list.hpp"
#include "coro/detail/tsan.hpp"

#include <atomic>
#include <stdexcept>

namespace coro
{
namespace detail
{
auto lock_operation_base::await_ready() const noexcept -> bool
{
    // Establish an acquire edge for TSAN on fast-path lock checks
    ::coro::detail::tsan_acquire(&m_mutex);
    bool r = m_mutex.try_lock();
    return r;
}

auto lock_operation_base::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    // Publish the awaiting coroutine and awaiter state via stable addresses
    // BEFORE making this awaiter visible to the unlocker. This avoids a race
    // where the unlocker could pop this awaiter and call resume() before we
    // had a chance to issue the release on the coroutine frame address.
    ::coro::detail::tsan_release(&m_mutex);
    ::coro::detail::tsan_release(&m_awaiting_coroutine);
    ::coro::detail::tsan_release(this);
    // Also publish the coroutine frame address so that the unlocker can
    // acquire() it before resume() and observe all prior writes, including
    // coroutine frame initialization.
    ::coro::detail::tsan_release(m_awaiting_coroutine.address());
    auto&       state          = m_mutex.state_ref();
    void*       current        = state.load(std::memory_order::acquire);
    const void* unlocked_value = m_mutex.unlocked_value();
    do
    {
        // While trying to suspend the lock can become available, if so attempt to grab it and then don't suspend.
        // If the lock never becomes available then we place ourself at the head of the waiter list and suspend.

        if (current == unlocked_value)
        {
            // The lock has become available, try and lock.
            if (state.compare_exchange_weak(current, nullptr, std::memory_order::acq_rel, std::memory_order::acquire))
            {
                // We've acquired the lock, don't suspend.
                m_awaiting_coroutine = nullptr;
                // Pair with unlock() release for TSAN HB relation
                ::coro::detail::tsan_acquire(&m_mutex);
                return false;
            }
        }
        else // if (current == nullptr || current is of type lock_operation_base*)
        {
            // The lock is still owned, attempt to add ourself as a waiter.
            m_next = static_cast<lock_operation_base*>(current);
            if (state.compare_exchange_weak(
                    current, static_cast<void*>(this), std::memory_order::acq_rel, std::memory_order::acquire))
            {
                // We've successfully added ourself to the waiter queue.
                // All necessary publications were performed before making the
                // awaiter visible to other threads.
                return true;
            }
        }
    } while (true);
}

} // namespace detail

scoped_lock::~scoped_lock()
{
    unlock();
}

auto scoped_lock::unlock() -> void
{
    if (m_mutex != nullptr)
    {
        std::atomic_thread_fence(std::memory_order::acq_rel);
        // Release happens-before subsequent lock acquisition
        detail::tsan_release(m_mutex);
        m_mutex->unlock();
        m_mutex = nullptr;
    }
}

auto mutex::try_lock() -> bool
{
    void* expected = const_cast<void*>(unlocked_value());
    bool  ok =
        state_ref().compare_exchange_strong(expected, nullptr, std::memory_order::acq_rel, std::memory_order::relaxed);
    return ok;
}

auto mutex::unlock() -> void
{
    void* current = state_ref().load(std::memory_order::acquire);
    do
    {
        // Sanity check that the mutex isn't already unlocked.
        if (current == const_cast<void*>(unlocked_value()))
        {
            throw std::runtime_error{"coro::mutex is already unlocked"};
        }

        // There are no current waiters, attempt to set the mutex as unlocked.
        if (current == nullptr)
        {
            if (state_ref().compare_exchange_weak(
                    current,
                    const_cast<void*>(unlocked_value()),
                    std::memory_order::acq_rel,
                    std::memory_order::acquire))
            {
                // We've successfully unlocked the mutex, return since there are no current waiters.
                std::atomic_thread_fence(std::memory_order::acq_rel);
                // Synchronize-with next acquirer on fast path
                detail::tsan_release(this);
                return;
            }
            else
            {
                // This means someone has added themselves as a waiter, we need to try again with our updated current
                // state. assert(m_state now holds a lock_operation_base*)
                continue;
            }
        }
        else
        {
            // There are waiters, lets wake the first one up. This will set the state to the next waiter, or nullptr (no
            // waiters but locked).
            std::atomic<detail::lock_operation_base*>* casted =
                reinterpret_cast<std::atomic<detail::lock_operation_base*>*>(&state_ref());
            auto* waiter = detail::awaiter_list_pop<detail::lock_operation_base>(*casted);
            // assert waiter != nullptr, nobody else should be unlocking this mutex.
            // Directly transfer control to the waiter, they are now responsible for unlocking the mutex.
            std::atomic_thread_fence(std::memory_order::acq_rel);
            // Before reading any fields from the awaiter (like the coroutine handle),
            // establish an acquire edge pairing with await_suspend()'s publications.
            ::coro::detail::tsan_acquire(&waiter->m_awaiting_coroutine);
            ::coro::detail::tsan_acquire(waiter);
            // Establish synchronization before handing over execution to waiter coroutine.
            // Provide TSAN HB edges:
            // 1) Acquire on the coroutine frame address to cover resume()'s internal read,
            //    paired with tsan_release(handle.address()) in await_suspend().
            // 2) Release on the mutex object to order unlock() with the resumed coroutine's
            //    await_resume() which does tsan_acquire(&m_mutex).
            // 3) Release on the awaiter object to pair with tsan_acquire(this) in
            //    lock_operation<>::await_resume() for precise hand-off.
            // First acquire on the coroutine frame address to synchronize with
            // await_suspend()'s publication and ensure frame initialization is visible
            // before resume() performs its internal read.
            ::coro::detail::tsan_acquire(waiter->m_awaiting_coroutine.address());
            // Then release on the coroutine frame address to hand-off to await_resume()
            // which acquires the same address.
            ::coro::detail::tsan_release(waiter->m_awaiting_coroutine.address());
            ::coro::detail::tsan_release(this);
            ::coro::detail::tsan_release(waiter);
            waiter->m_awaiting_coroutine.resume();
            return;
        }
    } while (true);
}

} // namespace coro
