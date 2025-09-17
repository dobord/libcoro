#pragma once

#include "coro/detail/tsan.hpp"
#include "coro/expected.hpp"
#include "coro/mutex.hpp"
#include "coro/task.hpp"
// Explicitly include awaiter list helpers for push/pop operations
#include "coro/detail/awaiter_list.hpp"

#include <array>
#include <atomic>
#include <coroutine>
#include <mutex>
#include <optional>

namespace coro
{
namespace ring_buffer_result
{
enum class produce
{
    produced,
    stopped
};

enum class consume
{
    stopped
};
} // namespace ring_buffer_result

/**
 * @tparam element The type of element the ring buffer will store.  Note that this type should be
 *         cheap to move if possible as it is moved into and out of the buffer upon produce and
 *         consume operations.
 * @tparam num_elements The maximum number of elements the ring buffer can store, must be >= 1.
 */
template<typename element, size_t num_elements>
class ring_buffer
{
private:
    enum running_state_t
    {
        /// @brief The ring buffer is still running.
        running,
        /// @brief The ring buffer is draining all elements, produce is no longer allowed.
        draining,
        /// @brief The ring buffer is fully shutdown, all produce and consume tasks will be woken up with
        /// result::stopped.
        stopped,
    };

public:
    /**
     * static_assert If `num_elements` == 0.
     */
    ring_buffer() { static_assert(num_elements != 0, "num_elements cannot be zero"); }

    ~ring_buffer()
    {
        // Wake up anyone still using the ring buffer.
        coro::sync_wait(shutdown());
    }

    ring_buffer(const ring_buffer<element, num_elements>&) = delete;
    ring_buffer(ring_buffer<element, num_elements>&&)      = delete;

    auto operator=(const ring_buffer<element, num_elements>&) noexcept -> ring_buffer<element, num_elements>& = delete;
    auto operator=(ring_buffer<element, num_elements>&&) noexcept -> ring_buffer<element, num_elements>&      = delete;

    struct produce_operation
    {
        produce_operation(ring_buffer<element, num_elements>& rb, element e) : m_rb(rb), m_e(std::move(e)) {}

        auto await_ready() noexcept -> bool
        {
            auto& mutex = m_rb.m_mutex;

            // Produce operations can only proceed if running.
            if (m_rb.m_running_state.load(std::memory_order::acquire) != running_state_t::running)
            {
                mutex.unlock();
                return true; // Will be awoken with produce::stopped
            }

            if (m_rb.m_used.load(std::memory_order::acquire) < num_elements)
            {
                // There is guaranteed space to store
                auto slot             = m_rb.m_front.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                m_rb.m_elements[slot] = std::move(m_e);
                m_rb.m_used.fetch_add(1, std::memory_order::release);
                mutex.unlock();
                return true; // Will be awoken with produce::produced
            }

            return false; // ring buffer full, suspend
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // Save awaiting handle first.
            m_awaiting_coroutine = awaiting_coroutine;
            // Capture stable pointers BEFORE publishing the awaiter to avoid touching 'this' after publication.
            auto* rb    = &m_rb;
            auto* mutex = &rb->m_mutex;
            auto* list  = &rb->m_produce_waiters;
            // Link into list under the mutex: no concurrent modifications while we hold it.
            // 1) Prepare next pointer in the awaiter itself.
            auto* head   = list->load(std::memory_order::relaxed);
            this->m_next = head;
            // 2) TSAN: Publish all relevant locations BEFORE making this awaiter visible and unlocking.
            //    After this release, we MUST NOT touch fields on 'this'.
            //    Publish only stable addresses to avoid touching the coroutine frame here.
            detail::tsan_release(&m_awaiting_coroutine);
            detail::tsan_release(this);
            detail::tsan_release(rb);
            // 3) Publish awaiter into the list. This does not dereference 'this'.
            list->store(this, std::memory_order::release);
            mutex->unlock();
            return true;
        }

        /**
         * @return produce_result
         */
        auto await_resume() -> ring_buffer_result::produce
        {
            // Real synchronization: pair with release in try_resume_producers()/shutdown
            (void)m_ready.load(std::memory_order::acquire);
            // TSAN hint: cover resume() internal read by pairing with release(frame.address())
            // that the resumer performs right before resume().
            detail::tsan_acquire(m_awaiting_coroutine.address());
            // TSAN: also pair with await_suspend() publisher on &m_awaiting_coroutine.
            // Avoid TSAN sync on awaiter object ('this'): lifetime ties to coroutine frame
            // Also keep acquire on the ring_buffer as a broader HB edge
            detail::tsan_acquire(&m_rb);
            return m_rb.m_running_state.load(std::memory_order::acquire) == running_state_t::running
                       ? ring_buffer_result::produce::produced
                       : ring_buffer_result::produce::stopped;
        }

        /// If the operation needs to suspend, the coroutine to resume when the element can be produced.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// Linked list of produce operations that are awaiting to produce their element.
        produce_operation* m_next{nullptr};

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class ring_buffer;

        /// The ring buffer the element is being produced into.
        ring_buffer<element, num_elements>& m_rb;
        /// The element this produce operation is producing into the ring buffer.
        std::optional<element> m_e{std::nullopt};
        /// Release/acquire handoff flag set by resumer prior to resume().
        std::atomic<int> m_ready{0};
    };

    struct consume_operation
    {
        explicit consume_operation(ring_buffer<element, num_elements>& rb) : m_rb(rb) {}

        auto await_ready() noexcept -> bool
        {
            auto& mutex = m_rb.m_mutex;

            // Consume operations proceed until stopped.
            if (m_rb.m_running_state.load(std::memory_order::acquire) == running_state_t::stopped)
            {
                mutex.unlock();
                return true;
            }

            if (m_rb.m_used.load(std::memory_order::acquire) > 0)
            {
                auto slot             = m_rb.m_back.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                m_e                   = std::move(m_rb.m_elements[slot]);
                m_rb.m_elements[slot] = std::nullopt;
                m_rb.m_used.fetch_sub(1, std::memory_order::release);
                mutex.unlock();
                return true;
            }

            return false; // ring buffer is empty, suspend.
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // Save awaiting handle first.
            m_awaiting_coroutine = awaiting_coroutine;
            // Capture stable pointers BEFORE publishing the awaiter to avoid touching 'this' after publication.
            auto* rb    = &m_rb;
            auto* mutex = &rb->m_mutex;
            auto* list  = &rb->m_consume_waiters;
            // Link into list under the mutex: no concurrent modifications while we hold it.
            // 1) Prepare next pointer in the awaiter itself.
            auto* head   = list->load(std::memory_order::relaxed);
            this->m_next = head;
            // 2) TSAN: Publish all relevant locations BEFORE making this awaiter visible and unlocking.
            //    After this release, we MUST NOT touch fields on 'this'.
            //    Publish only stable addresses to avoid touching the coroutine frame here.
            detail::tsan_release(&m_awaiting_coroutine);
            detail::tsan_release(this);
            detail::tsan_release(rb);
            // 3) Publish awaiter into the list. This does not dereference 'this'.
            list->store(this, std::memory_order::release);
            mutex->unlock();
            return true;
        }

        /**
         * @return The consumed element or ring_buffer_stopped if the ring buffer has been shutdown.
         */
        auto await_resume() -> expected<element, ring_buffer_result::consume>
        {
            // Real synchronization: pair with release in try_resume_consumers()/shutdown
            (void)m_ready.load(std::memory_order::acquire);
            // TSAN hint: cover resume() internal read by pairing with release(frame.address())
            // that the resumer performs right before resume().
            detail::tsan_acquire(m_awaiting_coroutine.address());
            // TSAN: also pair with await_suspend() publisher on &m_awaiting_coroutine.
            // Avoid TSAN sync on awaiter object ('this'): lifetime ties to coroutine frame
            // Also keep acquire on the ring_buffer as a broader HB edge
            detail::tsan_acquire(&m_rb);
            if (m_e.has_value())
            {
                return expected<element, ring_buffer_result::consume>(std::move(m_e).value());
            }
            else // state is stopped
            {
                return unexpected<ring_buffer_result::consume>(ring_buffer_result::consume::stopped);
            }
        }

        /// If the operation needs to suspend, the coroutine to resume when the element can be consumed.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// Linked list of consume operations that are awaiting to consume an element.
        consume_operation* m_next{nullptr};

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class ring_buffer;

        /// The ring buffer to consume an element from.
        ring_buffer<element, num_elements>& m_rb;
        /// The element this consume operation will consume.
        std::optional<element> m_e{std::nullopt};
        /// Release/acquire handoff flag set by resumer prior to resume().
        std::atomic<int> m_ready{0};
    };

    /**
     * Produces the given element into the ring buffer.  This operation will suspend until a slot
     * in the ring buffer becomes available.
     * @param e The element to produce.
     */
    [[nodiscard]] auto produce(element e) -> coro::task<ring_buffer_result::produce>
    {
        // TSAN: pair with detail::tsan_release(this) in try_resume_* before resuming this coroutine
        // This ensures HB before constructing awaitables below when we resume on another thread.
        detail::tsan_acquire(this);
        co_await m_mutex.lock();
        // TSAN: pair with detail::tsan_release(&m_mutex) in mutex::unlock before resuming this coroutine
        detail::tsan_acquire(&m_mutex);
        auto result = co_await produce_operation{*this, std::move(e)};
        co_await try_resume_consumers();
        co_return result;
    }

    /**
     * Consumes an element from the ring buffer.  This operation will suspend until an element in
     * the ring buffer becomes available.
     */
    [[nodiscard]] auto consume() -> coro::task<expected<element, ring_buffer_result::consume>>
    {
        // TSAN: pair with detail::tsan_release(this) in try_resume_* before resuming this coroutine
        // Ensures HB before any writes after cross-thread resume.
        detail::tsan_acquire(this);
        co_await m_mutex.lock();
        // TSAN: pair with detail::tsan_release(&m_mutex) in mutex::unlock before resuming this coroutine
        detail::tsan_acquire(&m_mutex);
        auto result = co_await consume_operation{*this};
        co_await try_resume_producers();
        co_return result;
    }

    /**
     * @return The current number of elements contained in the ring buffer.
     */
    auto size() const -> size_t { return m_used.load(std::memory_order::acquire); }

    /**
     * @return True if the ring buffer contains zero elements.
     */
    [[nodiscard]] auto empty() const -> bool { return size() == 0; }

    /**
     * @brief Wakes up all currently awaiting producers and consumers.  Their await_resume() function
     *        will return an expected consume result that the ring buffer has stopped.
     */
    auto shutdown() -> coro::task<void>
    {
        // Only wake up waiters once.
        auto expected = m_running_state.load(std::memory_order::acquire);
        if (expected == running_state_t::stopped)
        {
            co_return;
        }

        auto lk = co_await m_mutex.scoped_lock();
        // Only let one caller do the wake-ups, this can go from running or draining to stopped
        if (!m_running_state.compare_exchange_strong(
                expected, running_state_t::stopped, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }
        lk.unlock();

        co_await m_mutex.lock();
        auto* produce_waiters = m_produce_waiters.exchange(nullptr, std::memory_order::acq_rel);
        auto* consume_waiters = m_consume_waiters.exchange(nullptr, std::memory_order::acq_rel);
        m_mutex.unlock();

        while (produce_waiters != nullptr)
        {
            auto* next = produce_waiters->m_next;
            // TSAN: acquire on ring_buffer to observe await_suspend's release(&m_rb)
            detail::tsan_acquire(this);
            // TSAN: acquire on awaiter object to observe its publications before touching fields
            detail::tsan_acquire(produce_waiters);
            // Pair with await_suspend()'s publications on the stable addresses (avoid frame internals)
            // Publish wake-up to waiter prior to resume()
            produce_waiters->m_ready.store(1, std::memory_order::release);
            // TSAN: acquire on the frame address to observe publications from await_suspend()
            detail::tsan_acquire(produce_waiters->m_awaiting_coroutine.address());
            // TSAN: now release on the frame address to hand-off to await_resume()
            detail::tsan_release(produce_waiters->m_awaiting_coroutine.address());
            // Avoid TSAN edges on awaiter object (same lifetime as frame)
            // TSAN: additional release edge from ring_buffer to resumed producer coroutine
            detail::tsan_release(this);
            produce_waiters->m_awaiting_coroutine.resume();
            produce_waiters = next;
        }

        while (consume_waiters != nullptr)
        {
            auto* next = consume_waiters->m_next;
            // TSAN: acquire on ring_buffer to observe await_suspend's release(&m_rb)
            detail::tsan_acquire(this);
            // TSAN: acquire on awaiter object to observe its publications before touching fields
            detail::tsan_acquire(consume_waiters);
            // Pair with await_suspend()'s publications on the stable addresses (avoid frame internals)
            // Publish wake-up to waiter prior to resume()
            consume_waiters->m_ready.store(1, std::memory_order::release);
            // TSAN: acquire on the frame address to observe publications from await_suspend()
            detail::tsan_acquire(consume_waiters->m_awaiting_coroutine.address());
            // TSAN: now release on the frame address to hand-off to await_resume()
            detail::tsan_release(consume_waiters->m_awaiting_coroutine.address());
            // Avoid TSAN edges on awaiter object (same lifetime as frame)
            // TSAN: additional release edge from ring_buffer to resumed consumer coroutine
            detail::tsan_release(this);
            consume_waiters->m_awaiting_coroutine.resume();
            consume_waiters = next;
        }

        co_return;
    }

    template<coro::concepts::executor executor_type>
    [[nodiscard]] auto shutdown_drain(std::shared_ptr<executor_type> e) -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        // Do not allow any more produces, the state must be in running to drain.
        auto expected = running_state_t::running;
        if (!m_running_state.compare_exchange_strong(
                expected, running_state_t::draining, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }

        auto* produce_waiters = m_produce_waiters.exchange(nullptr, std::memory_order::acq_rel);
        lk.unlock();

        while (produce_waiters != nullptr)
        {
            auto* next = produce_waiters->m_next;
            // TSAN: acquire on ring_buffer to observe await_suspend's release(&m_rb)
            detail::tsan_acquire(this);
            // TSAN: acquire on awaiter object to observe its publications before touching fields
            detail::tsan_acquire(produce_waiters);
            // Pair with await_suspend()'s publications on the stable addresses (avoid frame internals)
            // Publish wake-up to waiter prior to resume()
            produce_waiters->m_ready.store(1, std::memory_order::release);
            // TSAN: acquire on the frame address to observe publications from await_suspend()
            detail::tsan_acquire(produce_waiters->m_awaiting_coroutine.address());
            // TSAN: now release on the frame address to hand-off to await_resume()
            detail::tsan_release(produce_waiters->m_awaiting_coroutine.address());
            // Avoid TSAN edges on awaiter object (same lifetime as frame)
            produce_waiters->m_awaiting_coroutine.resume();
            produce_waiters = next;
        }

        while (!empty())
        {
            co_await e->yield();
        }

        co_await shutdown();
        co_return;
    }

    /**
     * Returns true if shutdown() or shutdown_drain() have been called on this coro::ring_buffer.
     * @return True if the coro::ring_buffer has been shutdown.
     */
    [[nodiscard]] auto is_shutdown() const -> bool
    {
        return m_running_state.load(std::memory_order::acquire) != running_state_t::running;
    }

private:
    friend produce_operation;
    friend consume_operation;

    coro::mutex m_mutex{};

    std::array<std::optional<element>, num_elements> m_elements{};
    /// The current front pointer to an open slot if not full.
    std::atomic<size_t> m_front{0};
    /// The current back pointer to the oldest item in the buffer if not empty.
    std::atomic<size_t> m_back{0};
    /// The number of items in the ring buffer.
    std::atomic<size_t> m_used{0};

    /// The LIFO list of produce waiters.
    std::atomic<produce_operation*> m_produce_waiters{nullptr};
    /// The LIFO list of consume watier.
    std::atomic<consume_operation*> m_consume_waiters{nullptr};

    std::atomic<running_state_t> m_running_state{running_state_t::running};

    auto try_resume_producers() -> coro::task<void>
    {
        while (true)
        {
            auto lk = co_await m_mutex.scoped_lock();
            if (m_used.load(std::memory_order::acquire) < num_elements)
            {
                auto* op = detail::awaiter_list_pop(m_produce_waiters);
                if (op != nullptr)
                {
                    // Pair with await_suspend()'s publications on the stable addresses (avoid frame internals)
                    // TSAN: acquire on ring_buffer to observe await_suspend's release(&m_rb)
                    detail::tsan_acquire(this);
                    // TSAN: acquire on awaiter object to observe its publications before touching fields
                    detail::tsan_acquire(op);
                    auto slot        = m_front.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                    m_elements[slot] = std::move(op->m_e);
                    m_used.fetch_add(1, std::memory_order::release);

                    // Publish wake-up to waiter prior to resume()
                    op->m_ready.store(1, std::memory_order::release);
                    lk.unlock();
                    // TSAN: acquire on the frame address to observe publications from await_suspend()
                    detail::tsan_acquire(op->m_awaiting_coroutine.address());
                    // TSAN: now release on the frame address to hand-off to await_resume()
                    detail::tsan_release(op->m_awaiting_coroutine.address());
                    // Avoid TSAN edges on awaiter object (same lifetime as frame)
                    // Additional broad release from ring_buffer to resumed producer to help TSAN
                    detail::tsan_release(this);
                    op->m_awaiting_coroutine.resume();
                    continue;
                }
            }
            co_return;
        }
    }

    auto try_resume_consumers() -> coro::task<void>
    {
        while (true)
        {
            auto lk = co_await m_mutex.scoped_lock();
            if (m_used.load(std::memory_order::acquire) > 0)
            {
                auto* op = detail::awaiter_list_pop(m_consume_waiters);
                if (op != nullptr)
                {
                    // Pair with await_suspend()'s publications on the stable addresses (avoid frame internals)
                    // TSAN: acquire on ring_buffer to observe await_suspend's release(&m_rb)
                    detail::tsan_acquire(this);
                    // TSAN: acquire on awaiter object to observe its publications before touching fields
                    detail::tsan_acquire(op);
                    auto slot        = m_back.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                    op->m_e          = std::move(m_elements[slot]);
                    m_elements[slot] = std::nullopt;
                    m_used.fetch_sub(1, std::memory_order::release);
                    // Publish wake-up to waiter prior to resume()
                    op->m_ready.store(1, std::memory_order::release);
                    lk.unlock();
                    // TSAN: acquire on the frame address to observe publications from await_suspend()
                    detail::tsan_acquire(op->m_awaiting_coroutine.address());
                    // TSAN: now release on the frame address to hand-off to await_resume()
                    detail::tsan_release(op->m_awaiting_coroutine.address());
                    // Avoid TSAN edges on awaiter object (same lifetime as frame)
                    // Additional broad release from ring_buffer to resumed consumer to help TSAN
                    detail::tsan_release(this);
                    op->m_awaiting_coroutine.resume();
                    continue;
                }
            }
            co_return;
        }
    }
};

} // namespace coro
