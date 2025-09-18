#pragma once

#include "coro/detail/tsan.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <coroutine>
#include <mutex>
#include <utility>

namespace coro
{
class mutex;
class scoped_lock;
class condition_variable;

namespace detail
{

struct lock_operation_base
{
    explicit lock_operation_base(coro::mutex& m) : m_mutex(m) {}
    virtual ~lock_operation_base() = default;

    lock_operation_base(const lock_operation_base&)                    = delete;
    lock_operation_base(lock_operation_base&&)                         = delete;
    auto operator=(const lock_operation_base&) -> lock_operation_base& = delete;
    auto operator=(lock_operation_base&&) -> lock_operation_base&      = delete;

    auto await_ready() const noexcept -> bool;
    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;

    std::coroutine_handle<> m_awaiting_coroutine;
    lock_operation_base*    m_next{nullptr};

protected:
    friend class coro::mutex;

    coro::mutex& m_mutex;
};

template<typename return_type>
struct lock_operation : public lock_operation_base
{
    explicit lock_operation(coro::mutex& m) : lock_operation_base(m) {}
    ~lock_operation() override = default;

    lock_operation(const lock_operation&)                    = delete;
    lock_operation(lock_operation&&)                         = delete;
    auto operator=(const lock_operation&) -> lock_operation& = delete;
    auto operator=(lock_operation&&) -> lock_operation&      = delete;

    auto await_resume() noexcept -> return_type
    {
        // Establish a happens-before edge for TSAN between the unlocker (release)
        // and the resumed coroutine that just acquired the lock.
        // Pair with detail::tsan_release(waiter) in mutex::unlock() where 'waiter'
        // is this awaiter object.
        // Also acquire on the awaiting coroutine handle storage address to pair with
        // detail::tsan_release(&m_awaiting_coroutine) in await_suspend(). This covers
        // internal reads inside coroutine_handle::resume() and ties the publication
        // from the suspending side to this await_resume() execution.
        // Pair with tsan_release(frame.address()) emitted immediately before resume()
        // by mutex::unlock() on the unlocking thread, covering resume() internal reads.
        detail::tsan_acquire(this->m_awaiting_coroutine.address());
        // Also acquire the stable address of the handle storage, pairing with
        // await_suspend()'s tsan_release(&m_awaiting_coroutine) so the unlocking
        // thread's reads of the handle see our publications.
        detail::tsan_acquire(&this->m_awaiting_coroutine);
        detail::tsan_acquire(this);
        detail::tsan_acquire(&this->m_mutex);
        if constexpr (std::is_same_v<scoped_lock, return_type>)
        {
            return scoped_lock{this->m_mutex};
        }
        else
        {
            return;
        }
    }
};

} // namespace detail

/**
 * A scoped RAII lock holder similar to std::unique_lock.
 */
class scoped_lock
{
    friend class coro::mutex;
    friend class coro::condition_variable; // cv.wait() functions need to be able do unlock and re-lock

public:
    enum class lock_strategy
    {
        /// The lock is already acquired, adopt it as the new owner.
        adopt
    };

    explicit scoped_lock(class coro::mutex& m, lock_strategy strategy = lock_strategy::adopt) : m_mutex(&m)
    {
        // Future -> support acquiring the lock?  Not sure how to do that without being able to
        // co_await in the constructor.
        (void)strategy;
    }

    /**
     * Unlocks the mutex upon this shared lock destructing.
     */
    ~scoped_lock();

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&& other) noexcept : m_mutex(std::exchange(other.m_mutex, nullptr)) {}
    auto operator=(const scoped_lock&) -> scoped_lock& = delete;
    auto operator=(scoped_lock&& other) noexcept -> scoped_lock&
    {
        if (std::addressof(other) != this)
        {
            m_mutex = std::exchange(other.m_mutex, nullptr);
        }
        return *this;
    }

    /**
     * Unlocks the scoped lock prior to it going out of scope.
     */
    auto unlock() -> void;

private:
    class coro::mutex* m_mutex{nullptr};
};

class mutex
{
public:
    explicit mutex() noexcept { m_state.store(const_cast<void*>(unlocked_value()), std::memory_order::relaxed); }
    ~mutex() = default;

    mutex(const mutex&)                    = delete;
    mutex(mutex&&)                         = delete;
    auto operator=(const mutex&) -> mutex& = delete;
    auto operator=(mutex&&) -> mutex&      = delete;

    /**
     * @brief To acquire the mutex's lock co_await this function. Upon acquiring the lock it returns a coro::scoped_lock
     *        which will hold the mutex until the coro::scoped_lock destructs.
     * @return A co_await'able operation to acquire the mutex.
     */
    [[nodiscard]] auto scoped_lock() -> detail::lock_operation<scoped_lock>
    {
        return detail::lock_operation<coro::scoped_lock>{*this};
    }

    /**
     * @brief Locks the mutex.
     *
     * @return detail::lock_operation<void>
     */
    [[nodiscard]] auto lock() -> detail::lock_operation<void> { return detail::lock_operation<void>{*this}; }

    /**
     * Attempts to lock the mutex.
     * @return True if the mutex lock was acquired, otherwise false.
     */
    [[nodiscard]] auto try_lock() -> bool;

    /**
     * Releases the mutex's lock.
     */
    auto unlock() -> void;

private:
    friend struct detail::lock_operation_base;
    // In-object atomic state representing either:
    // - unlocked sentinel address (no owner),
    // - nullptr (locked with no waiters),
    // - pointer to lock_operation_base waiter (singly-linked list).
    std::atomic<void*> m_state{nullptr};

    // Stable non-null sentinel for the unlocked state; static storage to avoid any lifetime races.
    static inline char s_unlocked_sentinel{};

    /// Inactive value, this cannot be nullptr since we want nullptr to signify that the mutex
    /// is locked but there are zero waiters; using a static sentinel address is stable.
    auto unlocked_value() const noexcept -> const void* { return &s_unlocked_sentinel; }

    auto state_ref() noexcept -> std::atomic<void*>& { return m_state; }
    auto state_ref() const noexcept -> const std::atomic<void*>& { return m_state; }
};

} // namespace coro
