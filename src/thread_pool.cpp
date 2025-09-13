#include "coro/thread_pool.hpp"
#include "coro/detail/task_self_deleting.hpp"

namespace coro
{
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
namespace
{
thread_local thread_pool* tls_tp  = nullptr;
thread_local std::size_t  tls_idx = static_cast<std::size_t>(-1);
} // namespace
#endif
thread_pool::schedule_operation::schedule_operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{
}

auto thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_thread_pool.schedule_impl(awaiting_coroutine);
}

thread_pool::thread_pool(options&& opts, private_constructor) : m_opts(opts)
{
    m_threads.reserve(m_opts.thread_count);
}

auto thread_pool::make_shared(options opts) -> std::shared_ptr<thread_pool>
{
    auto tp = std::make_shared<thread_pool>(std::move(opts), private_constructor{});

    // Initialize once the shared pointer is constructor so it can be captured for
    // the background threads.
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
    tp->m_deques.reserve(tp->m_opts.thread_count);
    for (uint32_t i = 0; i < tp->m_opts.thread_count; ++i)
    {
        tp->m_deques.emplace_back(std::make_unique<detail::work_stealing_deque>(tp->m_cache_pool));
    }
#endif
    for (uint32_t i = 0; i < tp->m_opts.thread_count; ++i)
    {
        tp->m_threads.emplace_back([tp, i]() { tp->executor(i); });
    }

    return tp;
}

thread_pool::~thread_pool()
{
    shutdown();
}

auto thread_pool::schedule() -> schedule_operation
{
    m_size.fetch_add(1, std::memory_order::release);
    if (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        return schedule_operation{*this};
    }
    else
    {
        m_size.fetch_sub(1, std::memory_order::release);
        throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
    }
}

auto thread_pool::spawn(coro::task<void>&& task) noexcept -> bool
{
    m_size.fetch_add(1, std::memory_order::release);
    auto wrapper_task = detail::make_task_self_deleting(std::move(task));
    wrapper_task.promise().executor_size(m_size);
    return resume(wrapper_task.handle());
}

auto thread_pool::resume(std::coroutine_handle<> handle) noexcept -> bool
{
    if (handle == nullptr || handle.done())
    {
        return false;
    }

    m_size.fetch_add(1, std::memory_order::release);
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        m_size.fetch_sub(1, std::memory_order::release);
        return false;
    }

    schedule_impl(handle);
    return true;
}

auto thread_pool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        {
            // There is a race condition if we are not holding the lock with the executors
            // to always receive this.  std::jthread stop token works without this properly.
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.notify_all();
        }

        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
}

auto thread_pool::executor(std::size_t idx) -> void
{
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
    tls_tp  = this;
    tls_idx = idx;
#endif
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested.
    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
        if (m_deques.size() > 1)
        {
            // Try to pop from own deque; then steal; then fallback to global inbox
            std::optional<std::coroutine_handle<>> task;
            // short spin before sleeping
            for (int spin = 0; spin < 100; ++spin)
            {
                task = m_deques[idx]->pop_bottom();
                if (task && *task)
                    break;
                // steal from others
                for (std::size_t j = 0; j < m_deques.size(); ++j)
                {
                    if (j == idx)
                        continue;
                    auto stolen = m_deques[j]->steal_top();
                    if (stolen && *stolen)
                    {
                        task = stolen;
                        break;
                    }
                }
                if (task && *task)
                    break;
            }

            if (!task || !*task)
            {
                // check global inbox and batch-drain into local deque
                constexpr std::size_t                kBatch = 64;
                std::vector<std::coroutine_handle<>> batch;
                batch.reserve(kBatch);
                std::unique_lock<std::mutex> lk{m_wait_mutex};
                if (m_queue.empty())
                {
                    m_waiters.fetch_add(1, std::memory_order::acq_rel);
                    m_wait_cv.wait(
                        lk,
                        [&]() { return !m_queue.empty() || m_shutdown_requested.load(std::memory_order::acquire); });
                    m_waiters.fetch_sub(1, std::memory_order::acq_rel);
                }
                // If there are other waiters, be polite: grab only 1 task locally, leave the rest.
                if (!m_queue.empty() && m_waiters.load(std::memory_order::acquire) > 0)
                {
                    batch.emplace_back(m_queue.front());
                    m_queue.pop_front();
                }
                else
                {
                    while (!m_queue.empty() && batch.size() < kBatch)
                    {
                        batch.emplace_back(m_queue.front());
                        m_queue.pop_front();
                    }
                }
                lk.unlock();
                // move batch to local deque
                for (auto h : batch)
                {
                    if (h)
                    {
                        m_deques[idx]->push_bottom(h);
                    }
                }
                // try again from local deque
                task = m_deques[idx]->pop_bottom();
                if (!(task && *task))
                {
                    continue; // nothing to run (spurious wake or shutdown)
                }
            }

            // Execute task from deque
            (*task).resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
        else
        {
            // Single-thread fallback: global inbox only
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.wait(
                lk, [&]() { return !m_queue.empty() || m_shutdown_requested.load(std::memory_order::acquire); });
            if (m_queue.empty())
            {
                continue;
            }
            auto handle = m_queue.front();
            m_queue.pop_front();
            lk.unlock();
            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
#else
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        m_wait_cv.wait(lk, [&]() { return !m_queue.empty() || m_shutdown_requested.load(std::memory_order::acquire); });

        if (m_queue.empty())
        {
            continue;
        }

        auto handle = m_queue.front();
        m_queue.pop_front();
        lk.unlock();

        // Release the lock while executing the coroutine.
        handle.resume();
        m_size.fetch_sub(1, std::memory_order::release);
#endif
    }

    // Process until there are no ready tasks left.
    while (m_size.load(std::memory_order::acquire) > 0)
    {
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
        if (m_deques.size() > 1)
        {
            std::optional<std::coroutine_handle<>> task;
            task = m_deques[idx]->pop_bottom();
            if (!task || !*task)
            {
                for (std::size_t j = 0; j < m_deques.size(); ++j)
                {
                    if (j == idx)
                        continue;
                    auto stolen = m_deques[j]->steal_top();
                    if (stolen && *stolen)
                    {
                        task = stolen;
                        break;
                    }
                }
            }
            if (!task || !*task)
            {
                // flush global inbox in batch into local deque
                constexpr std::size_t                kBatch = 64;
                std::vector<std::coroutine_handle<>> batch;
                batch.reserve(kBatch);
                std::unique_lock<std::mutex> lk{m_wait_mutex};
                if (m_queue.empty())
                {
                    break;
                }
                if (m_waiters.load(std::memory_order::acquire) > 0)
                {
                    batch.emplace_back(m_queue.front());
                    m_queue.pop_front();
                }
                else
                {
                    while (!m_queue.empty() && batch.size() < kBatch)
                    {
                        batch.emplace_back(m_queue.front());
                        m_queue.pop_front();
                    }
                }
                lk.unlock();
                for (auto h : batch)
                {
                    if (h)
                    {
                        m_deques[idx]->push_bottom(h);
                    }
                }
                task = m_deques[idx]->pop_bottom();
                if (!(task && *task))
                {
                    continue;
                }
            }
            (*task).resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
        else
        {
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            if (m_queue.empty())
                break;
            auto handle = m_queue.front();
            m_queue.pop_front();
            lk.unlock();
            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
#else
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        if (m_queue.empty())
        {
            break;
        }

        auto handle = m_queue.front();
        m_queue.pop_front();
        lk.unlock();

        // Release the lock while executing the coroutine.
        handle.resume();
        m_size.fetch_sub(1, std::memory_order::release);
#endif
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
    tls_tp  = nullptr;
    tls_idx = static_cast<std::size_t>(-1);
#endif
}

auto thread_pool::schedule_impl(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr || handle.done())
    {
        return;
    }

#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING
    if (tls_tp == this && tls_idx < m_deques.size() && m_deques.size() > 1)
    {
        // Local push to this worker's deque; no need to notify condvar.
        m_deques[tls_idx]->push_bottom(handle);
        return;
    }
#endif
    {
        std::scoped_lock lk{m_wait_mutex};
        m_queue.emplace_back(handle);
    }
    m_wait_cv.notify_one();
}

} // namespace coro
