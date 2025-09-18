#pragma once

#include "coro/detail/awaiter_list.hpp"
#include "coro/expected.hpp"
#include "coro/export.hpp"
#include "coro/mutex.hpp"
// TSAN hand-off annotations
#include "coro/detail/tsan.hpp"

#include <atomic>
#include <coroutine>
#include <string>
#include <thread>

namespace coro
{

enum class semaphore_acquire_result
{
    /// @brief The semaphore was acquired.
    acquired,
    /// @brief The semaphore is shutting down, it has not been acquired.
    shutdown
};

extern CORO_EXPORT std::string semaphore_acquire_result_acquired;
extern CORO_EXPORT std::string semaphore_acquire_result_shutdown;
extern CORO_EXPORT std::string semaphore_acquire_result_unknown;

auto to_string(semaphore_acquire_result result) -> const std::string&;

template<std::ptrdiff_t max_value>
class semaphore;

namespace detail
{

template<std::ptrdiff_t max_value>
class acquire_operation
{
public:
    explicit acquire_operation(semaphore<max_value>& s) : m_semaphore(s) {}

    // Heap-allocated wait node to decouple lifetime from the acquire() coroutine frame.
    struct acquire_wait_node
    {
        acquire_wait_node*      m_next{nullptr};
        std::coroutine_handle<> m_awaiting_coroutine{};
        // Handoff flag set by resumer prior to resume().
        std::atomic<int> m_ready{0};
        // Whether this wake-up is due to shutdown (true) or successful acquire (false).
        std::atomic<bool> m_shutdown{false};
        // Registration flag set by await_suspend() at the very end, to prevent
        // resumers from resuming/destroying the coroutine frame before await_suspend returns.
        std::atomic<bool> m_registered{false};
    };

    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        // If the semaphore is shutdown or a resources can be acquired without suspending release the lock and resume
        // execution.
        if (m_semaphore.m_shutdown.load(std::memory_order::acquire) || m_semaphore.try_acquire())
        {
            m_semaphore.m_mutex.unlock();
            return true;
        }

        return false;
    }

    auto await_suspend(const std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
    {
        // Check again now that we've set up the coroutine frame, the state could have changed.
        if (await_ready())
        {
            return false;
        }

        // IMPORTANT: Do not access members of this awaiter (the coroutine frame) AFTER we publish the
        // wait-node into the semaphore waiters list. The resumer may resume and destroy our frame.
        // Capture any needed external pointers now.
        auto* mutex_ptr = &m_semaphore.m_mutex;

        // Allocate and initialise wait node fully before publishing.
        m_node                       = new acquire_wait_node{};
        m_node->m_awaiting_coroutine = awaiting_coroutine;
        // TSAN: publish stable addresses. We also mark the node as registered
        // BEFORE publishing it into the waiter list, so any resumer that pops
        // it can safely resume without racing our return from await_suspend().
        coro::detail::tsan_release(&m_node->m_awaiting_coroutine);
        coro::detail::tsan_release(m_node);
        coro::detail::tsan_release(&m_semaphore);
        m_node->m_registered.store(true, std::memory_order::release);
        detail::awaiter_list_push(m_semaphore.m_acquire_waiters, m_node);
        // Unlock the mutex to allow producers to make progress and then suspend.
        mutex_ptr->unlock();
        return true;
    }

    [[nodiscard]] auto await_resume() -> semaphore_acquire_result
    {
        // If await_ready() returned true then await_suspend() was never called and m_node is null.
        // In that case simply return based on current shutdown state without touching a node.
        if (m_node == nullptr)
        {
            return m_semaphore.m_shutdown.load(std::memory_order::acquire) ? semaphore_acquire_result::shutdown
                                                                           : semaphore_acquire_result::acquired;
        }

        // TSAN: acquire edges from resumer in release()/shutdown() hand-off to this awaiting coroutine.
        // Use the stable wait-node address as the sync token instead of the coroutine frame address
        // to avoid races with frame destruction or accessing the semaphore after destruction.
        coro::detail::tsan_acquire(m_node);
        // Pair with m_ready.store(1, release) in release()/shutdown() to provide a concrete HB edge.
        (void)m_node->m_ready.load(std::memory_order::acquire);
        const bool was_shutdown = m_node->m_shutdown.load(std::memory_order::acquire);
        // Free node after consuming the handoff.
        delete m_node;
        m_node = nullptr;
        return was_shutdown ? semaphore_acquire_result::shutdown : semaphore_acquire_result::acquired;
    }

    semaphore<max_value>& m_semaphore;
    // Per-operation node published to semaphore's waiter list; owned until await_resume().
    acquire_wait_node* m_node{nullptr};
};

} // namespace detail

template<std::ptrdiff_t max_value>
class semaphore
{
public:
    explicit semaphore(const std::ptrdiff_t starting_value) : m_counter(starting_value) {}

    ~semaphore() { shutdown(); }

    semaphore(const semaphore&) = delete;
    semaphore(semaphore&&)      = delete;

    auto operator=(const semaphore&) noexcept -> semaphore& = delete;
    auto operator=(semaphore&&) noexcept -> semaphore&      = delete;

    /**
     * @brief Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will suspend and wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> coro::task<semaphore_acquire_result>
    {
        co_await m_mutex.lock();
        // TSAN: pair with tsan_release(&m_mutex) in unlock paths to cover resumption HB
        coro::detail::tsan_acquire(&m_mutex);
        co_return co_await detail::acquire_operation<max_value>{*this};
    }

    /**
     * @brief Releases a resources back to the semaphore, if the semaphore is already at value() == max() this does
     * nothing.
     * @return
     */
    [[nodiscard]] auto release() -> coro::task<void>
    {
        co_await m_mutex.lock();
        // If there are any waiters transfer resource ownership directly to the waiter.
        auto* waiter = detail::awaiter_list_pop(m_acquire_waiters);
        if (waiter != nullptr)
        {
            // Node registration is guaranteed before enqueue, so it must be set here.
            // Publish wake-up to waiter prior to resume()
            waiter->m_ready.store(1, std::memory_order::release);
            waiter->m_shutdown.store(false, std::memory_order::release);
            // Do this after unlocking to avoid resuming while holding the mutex.
            m_mutex.unlock();
            // TSAN: acquire stable addresses published by await_suspend() before resume.
            coro::detail::tsan_acquire(waiter);
            // Do not touch the coroutine frame address; the wait-node acts as the hand-off token.
            // Broad edge from semaphore to resumed waiter
            waiter->m_awaiting_coroutine.resume();
            // The waiter node will be deleted by await_resume() after handoff consumption.
        }
        else
        {
            // No waiters: increment resource if not at max.
            if (value() < max())
            {
                m_counter.fetch_add(1, std::memory_order::release);
            }
            m_mutex.unlock();
        }
    }

    /**
     * @brief Attempts to acquire a resource if there are any resources available.
     * @return True if the acquire operation was able to acquire a resource.
     */
    auto try_acquire() -> bool
    {
        auto expected = m_counter.load(std::memory_order::acquire);
        do
        {
            if (expected <= 0)
            {
                return false;
            }
        } while (!m_counter.compare_exchange_weak(
            expected, expected - 1, std::memory_order::acq_rel, std::memory_order::acquire));

        return true;
    }

    /**
     * @return The maximum number of resources the semaphore can contain.
     */
    [[nodiscard]] static constexpr auto max() noexcept -> std::ptrdiff_t { return max_value; }

    /**
     * @return The current number of resources available to acquire for this semaphore.
     */
    [[nodiscard]] auto value() const noexcept -> std::ptrdiff_t { return m_counter.load(std::memory_order::acquire); }

    /**
     * Stops the semaphore and will notify all release/acquire waiters to wake up in a failed state.
     * Once this is set it cannot be un-done and all future oprations on the semaphore will fail.
     */
    auto shutdown() noexcept -> void
    {
        bool expected{false};
        if (m_shutdown.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
        {
            auto* waiter = detail::awaiter_list_pop_all(m_acquire_waiters);
            while (waiter != nullptr)
            {
                auto* next = waiter->m_next;
                // Publish wake-up to waiter prior to resume()
                waiter->m_ready.store(1, std::memory_order::release);
                waiter->m_shutdown.store(true, std::memory_order::release);
                // TSAN: acquire stable addresses published by await_suspend() before resume.
                coro::detail::tsan_acquire(waiter);
                // Hand-off to await_resume() via the wait-node token only.
                waiter->m_awaiting_coroutine.resume();
                // The waiter node will be deleted by await_resume().
                waiter = next;
            }
        }
    }

    /**
     * @return True if this semaphore has been shutdown.
     */
    [[nodiscard]] auto is_shutdown() const -> bool { return m_shutdown.load(std::memory_order::acquire); }

private:
    friend class detail::acquire_operation<max_value>;

    /// @brief The current number of resources that are available to acquire.
    std::atomic<std::ptrdiff_t> m_counter;
    /// @brief The current list of wait nodes attempting to acquire the semaphore.
    std::atomic<typename detail::acquire_operation<max_value>::acquire_wait_node*> m_acquire_waiters{nullptr};
    /// @brief mutex used to do acquire and release operations
    coro::mutex m_mutex;
    /// @brief Flag to denote that all waiters should be woken up with the shutdown result.
    std::atomic<bool> m_shutdown{false};
};

} // namespace coro
