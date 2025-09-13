#include "catch_amalgamated.hpp"

#ifdef LIBCORO_EXPERIMENTAL_WORK_STEALING

    #include <atomic>
    #include <numeric>
    #include <optional>
    #include <thread>
    #include <vector>

    #include "coro/detail/cache_pool.hpp"
    #include "coro/detail/work_stealing_deque.hpp"

using coro::detail::cache_pool;
using coro::detail::work_stealing_deque;

namespace
{

// A tiny coroutine type that never runs; used to manufacture stable coroutine_handles with ids.
struct id_task
{
    struct promise_type
    {
        int     id{-1};
        id_task get_return_object() noexcept
        {
            return id_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_never  final_suspend() const noexcept { return {}; }
        void                return_void() noexcept {}
        void                unhandled_exception() { std::terminate(); }
    };
    std::coroutine_handle<promise_type> h;
};

id_task make_id_task(int id)
{
    co_await std::suspend_always{}; // never resumes in tests
    (void)id;                       // silence unused if ever resumed
}

inline std::coroutine_handle<> to_generic(std::coroutine_handle<id_task::promise_type> h)
{
    return std::coroutine_handle<>::from_address(h.address());
}

inline int get_id(std::coroutine_handle<> h)
{
    auto th = std::coroutine_handle<id_task::promise_type>::from_address(h.address());
    return th.promise().id;
}

} // namespace

TEST_CASE("work_stealing_deque single-thread push/pop LIFO", "[ws-deque]")
{
    cache_pool<work_stealing_deque::block> pool;
    work_stealing_deque                    dq(pool, /*initial_capacity*/ 8);

    constexpr int        N = 100;
    std::vector<id_task> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        auto t           = make_id_task(i);
        t.h.promise().id = i;
        tasks.emplace_back(t);
        dq.push_bottom(to_generic(t.h));
    }

    // Pop LIFO order
    for (int i = N - 1; i >= 0; --i)
    {
        auto v = dq.pop_bottom();
        REQUIRE(v.has_value());
        REQUIRE(get_id(*v) == i);
    }
    // Deque is empty now
    REQUIRE_FALSE(dq.pop_bottom().has_value());

    // Cleanup: destroy tasks
    for (auto& t : tasks)
    {
        if (t.h)
            t.h.destroy();
    }
}

TEST_CASE("work_stealing_deque concurrent steals", "[ws-deque]")
{
    cache_pool<work_stealing_deque::block> pool;
    work_stealing_deque                    dq(pool, /*initial_capacity*/ 64);

    constexpr int N       = 4000;
    constexpr int Thieves = 4;

    std::vector<id_task> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        auto t           = make_id_task(i);
        t.h.promise().id = i;
        tasks.emplace_back(t);
        dq.push_bottom(to_generic(t.h));
    }

    std::vector<std::thread>                          threads;
    std::atomic<int>                                  stolen_total{0};
    std::vector<std::vector<std::coroutine_handle<>>> stolen(Thieves);
    std::vector<std::atomic<bool>>                    seen(N);
    for (auto& b : seen)
        b.store(false, std::memory_order::relaxed);

    for (int t = 0; t < Thieves; ++t)
    {
        threads.emplace_back(
            [&dq, &stolen, &stolen_total, &seen, t]()
            {
                while (true)
                {
                    if (stolen_total.load(std::memory_order::acquire) >= N)
                        break;
                    auto val = dq.steal_top();
                    if (val && *val)
                    {
                        int id = get_id(*val);
                        seen[id].store(true, std::memory_order::release);
                        stolen[t].push_back(*val);
                        stolen_total.fetch_add(1, std::memory_order::acq_rel);
                    }
                    else
                    {
                        // brief pause
                        std::this_thread::yield();
                    }
                }
            });
    }

    for (auto& th : threads)
        th.join();

    REQUIRE(stolen_total.load(std::memory_order::acquire) == N);
    for (int i = 0; i < N; ++i)
    {
        REQUIRE(seen[i].load(std::memory_order::acquire));
    }

    // Cleanup destroyed by us: destroy all stolen coroutine handles
    for (auto& bucket : stolen)
    {
        for (auto h : bucket)
        {
            if (h)
            {
                auto th = std::coroutine_handle<id_task::promise_type>::from_address(h.address());
                th.destroy();
            }
        }
    }
}

TEST_CASE("work_stealing_deque grow and mixed consume", "[ws-deque]")
{
    cache_pool<work_stealing_deque::block> pool;
    // small initial capacity to force growth
    work_stealing_deque dq(pool, /*initial_capacity*/ 8);

    constexpr int N       = 1024;
    constexpr int Thieves = 2;

    std::vector<id_task> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        auto t           = make_id_task(i);
        t.h.promise().id = i;
        tasks.emplace_back(t);
        dq.push_bottom(to_generic(t.h));
    }

    std::vector<std::atomic<bool>> seen(N);
    for (auto& b : seen)
        b.store(false, std::memory_order::relaxed);

    std::atomic<int>                                  total{0};
    std::vector<std::thread>                          threads;
    std::vector<std::vector<std::coroutine_handle<>>> stolen(Thieves);
    for (int t = 0; t < Thieves; ++t)
    {
        threads.emplace_back(
            [&dq, &seen, &total, &stolen, t]()
            {
                while (true)
                {
                    if (total.load(std::memory_order::acquire) >= N)
                        break;
                    auto v = dq.steal_top();
                    if (v && *v)
                    {
                        int id = get_id(*v);
                        if (!seen[id].exchange(true, std::memory_order::acq_rel))
                        {
                            stolen[t].push_back(*v);
                            total.fetch_add(1, std::memory_order::acq_rel);
                        }
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            });
    }

    // Owner pops concurrently until all consumed
    std::vector<std::coroutine_handle<>> popped;
    while (total.load(std::memory_order::acquire) < N)
    {
        auto v = dq.pop_bottom();
        if (v && *v)
        {
            int id = get_id(*v);
            if (!seen[id].exchange(true, std::memory_order::acq_rel))
            {
                popped.push_back(*v);
                total.fetch_add(1, std::memory_order::acq_rel);
            }
        }
        else
        {
            std::this_thread::yield();
        }
    }

    for (auto& th : threads)
        th.join();

    REQUIRE(total.load(std::memory_order::acquire) == N);
    for (int i = 0; i < N; ++i)
    {
        REQUIRE(seen[i].load(std::memory_order::acquire));
    }

    // Cleanup: destroy all collected handles (stolen and popped)
    for (auto& bucket : stolen)
    {
        for (auto h : bucket)
        {
            if (h)
            {
                auto th = std::coroutine_handle<id_task::promise_type>::from_address(h.address());
                th.destroy();
            }
        }
    }
    for (auto h : popped)
    {
        if (h)
        {
            auto th = std::coroutine_handle<id_task::promise_type>::from_address(h.address());
            th.destroy();
        }
    }
}

#else

TEST_CASE("work_stealing_deque disabled", "[ws-deque]")
{
    SUCCEED("LIBCORO_EXPERIMENTAL_WORK_STEALING is OFF; tests skipped.");
}

#endif // LIBCORO_EXPERIMENTAL_WORK_STEALING
