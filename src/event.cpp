#include "coro/event.hpp"
#include "coro/detail/awaiter_list.hpp"
#include "coro/thread_pool.hpp"

namespace coro
{
event::event(bool initially_set) noexcept : m_state(std::make_unique<state>())
{
    // Use the stable heap state address as the "set" sentinel instead of `this`.
    if (initially_set)
    {
        m_state->value.store(m_state.get(), std::memory_order::release);
    }
}

event::~event() noexcept
{
    // Establish HB with any setter/awaiter that published to the state sentinel.
    // This makes TSAN aware that destruction happens-after all uses paired with tsan_release(m_state.get()).
    detail::tsan_acquire(m_state.get());
}

auto event::set(resume_order_policy policy) noexcept -> void
{
    // Exchange the state to this, if the state was previously not this, then traverse the list
    // of awaiters and resume their coroutines.
    void* const set_state = m_state.get();
    void*       old_value = m_state->value.exchange(set_state, std::memory_order::acq_rel);
    if (old_value != set_state)
    {
        // TSAN: Broad release on the event state object to establish HB to awaiters' await_resume.
        detail::tsan_release(m_state.get());
        // If FIFO has been requsted then reverse the order upon resuming.
        if (policy == resume_order_policy::fifo)
        {
            old_value = reverse(static_cast<awaiter*>(old_value));
        }
        // else lifo nothing to do

        auto* waiters = static_cast<awaiter*>(old_value);
        while (waiters != nullptr)
        {
            auto* next = waiters->m_next;
            // TSAN: Establish HB to the resumed coroutine via its stable awaiting handle storage
            // and also via the awaiter object itself.
            detail::tsan_acquire(&waiters->m_awaiting_coroutine);
            detail::tsan_release(waiters);
            waiters->m_awaiting_coroutine.resume();
            waiters = next;
        }
    }
}

auto event::reverse(awaiter* curr) -> awaiter*
{
    return detail::awaiter_list_reverse(curr);
}

auto event::awaiter::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    const void* const set_state = m_event.m_state.get();

    m_awaiting_coroutine = awaiting_coroutine;
    // TSAN: Publish the awaiting handle storage so the resumer can acquire it.
    detail::tsan_release(&m_awaiting_coroutine);

    // This value will update if other threads write to it via acquire.
    void* old_value = m_event.m_state->value.load(std::memory_order::acquire);
    do
    {
        // Resume immediately if already in the set state.
        if (old_value == set_state)
        {
            // TSAN: Even on fast-path (no suspend), publish a release on the state sentinel
            // so that the event's destructor (which acquires) happens-after this access.
            detail::tsan_release(m_event.m_state.get());
            return false;
        }

        m_next = static_cast<awaiter*>(old_value);
    } while (!m_event.m_state->value.compare_exchange_weak(
        old_value, this, std::memory_order::release, std::memory_order::acquire));

    // TSAN: publish on the state sentinel so that await_resume() and the event's destructor can acquire it.
    detail::tsan_release(m_event.m_state.get());

    return true;
}

auto event::reset() noexcept -> void
{
    void* old_value = m_state.get();
    m_state->value.compare_exchange_strong(old_value, nullptr, std::memory_order::acquire);
}

} // namespace coro
