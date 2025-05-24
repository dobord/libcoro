#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <thread>

// TEST_CASE("wait(lock) 1 waiter", "[condition_variable]")
// {
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::event e{}; // just used to coordinate the order in which the tasks run.

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, coro::event& e) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         e.set(); // trigger the notifier that we are waiting now that we have control of the lock
//         co_await cv.wait(lk);
//         co_return 42;
//     };

//     auto make_notifier = [](coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
//     {
//         co_await e;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, e), make_notifier(cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == 42);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait(lock predicate) 1 waiter", "[condition_variable]")
// {
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::event e{}; // just used to coordinate the order in which the tasks run.

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, coro::event& e) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         e.set(); // trigger the notifier that we are waiting now that we have control of the lock
//         co_await cv.wait(lk, []() -> bool { return true; });
//         co_return 42;
//     };

//     auto make_notifier = [](coro::condition_variable& cv, coro::event& e) -> coro::task<int64_t>
//     {
//         co_await e;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, e), make_notifier(cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == 42);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// #ifndef EMSCRIPTEN

// TEST_CASE("wait(lock stop_token predicate) 1 waiter", "[condition_variable]")
// {
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::stop_source ss{};

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::stop_source& ss) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto result = co_await cv.wait(lk, ss.get_token(), [&ss]() -> bool { return false; });
//         REQUIRE(result == false);
//         co_return 42;
//     };

//     auto make_notifier = [](coro::condition_variable& cv, std::stop_source& ss) -> coro::task<int64_t>
//     {
//         ss.request_stop();
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, ss), make_notifier(cv, ss)));
//     REQUIRE(std::get<0>(results).return_value() == 42);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// #endif

// #ifdef LIBCORO_FEATURE_NETWORKING

// TEST_CASE("wait(lock predicate) 1 waiter notify_one until predicate passes", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> counter{0};
//     std::atomic<int64_t> predicate_called{0};

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& counter,
//     std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         co_await cv.wait(lk, [&counter, &predicate_called]() -> bool
//         {
//             predicate_called++;
//             return counter == 1;
//         });
//         co_return 42;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& counter) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         co_await cv.notify_one(); // The predicate will not pass
//         counter++;
//         co_await cv.notify_one(); // The predicate will pass
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, counter, predicate_called), make_notifier(s, cv,
//     counter))); REQUIRE(std::get<0>(results).return_value() == 42); REQUIRE(std::get<1>(results).return_value() ==
//     0);

//     // The predicate is called 3 times
//     // 1) On the initial cv.wait() to see if its already satisfied.
//     // 2) On the first notify_all() when counter is still 0.
//     // 3) On the final notify_all() when the counter is now 1.
//     REQUIRE(predicate_called == 3);
// }

// TEST_CASE("wait(lock predicate) 1 waiter predicate notify_all until predicate passes", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> counter{0};
//     std::atomic<int64_t> predicate_called{0};

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& counter,
//     std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         co_await cv.wait(lk, [&counter, &predicate_called]() -> bool
//         {
//             predicate_called++;
//             return counter == 1;
//         });
//         co_return 42;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& counter) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         co_await cv.notify_all(); // The predicate will not pass
//         counter++;
//         co_await cv.notify_all(); // The predicate will pass
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(cv, m, counter, predicate_called), make_notifier(s, cv,
//     counter))); REQUIRE(std::get<0>(results).return_value() == 42); REQUIRE(std::get<1>(results).return_value() ==
//     0);

//     // The predicate is called 3 times
//     // 1) On the initial cv.wait() to see if its already satisfied.
//     // 2) On the first notify_all() when counter is still 0.
//     // 3) On the final notify_all() when the counter is now 1.
//     REQUIRE(predicate_called == 3);
// }

// TEST_CASE("wait(lock) 3 waiters notify_one", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::event e1{};
//     coro::event e2{};
//     coro::event e3{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     coro::event& e, int64_t r) -> coro::task<int64_t>
//     {
//         co_await s->schedule();
//         auto lk = co_await m.scoped_lock();
//         e.set();
//         co_await cv.wait(lk);
//         co_return r;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule_after(std::chrono::milliseconds{10});
//         co_await e;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(s, cv, m, e1, 1),
//         make_waiter(s, cv, m, e2, 2),
//         make_waiter(s, cv, m, e3, 3),
//         make_notifier(s, cv, e1),
//         make_notifier(s, cv, e1),
//         make_notifier(s, cv, e1)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 2);
//     REQUIRE(std::get<2>(results).return_value() == 3);
// }

// TEST_CASE("wait(lock predicate) 3 waiters predicate notify_one", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> e{0};

//     auto make_waiter = [](coro::condition_variable& cv, coro::mutex& m, std::atomic<int64_t>& e, int64_t r) ->
//     coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         co_await cv.wait(lk, [&e, &r]() -> bool
//         {
//             return e > 0;
//         });
//         e--;
//         co_return r;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<int64_t>& e) -> coro::task<int64_t>
//     {
//         co_await s->schedule_after(std::chrono::milliseconds{10});
//         {
//             auto lk = co_await m.scoped_lock();
//             e++;
//         }
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(cv, m, e, 1),
//         make_waiter(cv, m, e, 2),
//         make_waiter(cv, m, e, 3),
//         make_notifier(s, cv, m, e),
//         make_notifier(s, cv, m, e),
//         make_notifier(s, cv, m, e)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 2);
//     REQUIRE(std::get<2>(results).return_value() == 3);
// }

// TEST_CASE("wait(lock) 3 waiters notify_all", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::latch l{3};
//     coro::event e{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     coro::latch& l, int64_t r) -> coro::task<int64_t>
//     {
//         co_await s->schedule();
//         auto lk = co_await m.scoped_lock();
//         l.count_down();
//         co_await cv.wait(lk);
//         co_return r;
//     };

//     auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await l;
//         e.set();
//         co_return 0;
//     };

//     auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await e;
//         co_await s->schedule_after(std::chrono::milliseconds{10});
//         co_await cv.notify_all();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(s, cv, m, l, 1),
//         make_waiter(s, cv, m, l, 2),
//         make_waiter(s, cv, m, l, 3),
//         make_all_waiting(s, l, e),
//         make_notify_all(s, cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 2);
//     REQUIRE(std::get<2>(results).return_value() == 3);
// }

// TEST_CASE("wait(lock predicate) 3 waiters predicate notify_all", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::latch l{3};
//     coro::event e{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     coro::latch& l, int64_t r) -> coro::task<int64_t>
//     {
//         int64_t called{0};
//         co_await s->schedule();
//         auto lk = co_await m.scoped_lock();
//         l.count_down();
//         co_await cv.wait(lk, [&called]() -> bool { return ++called > 0; });
//         co_return r;
//     };

//     auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await l;
//         e.set();
//         co_return 0;
//     };

//     auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await e;
//         co_await s->schedule_after(std::chrono::milliseconds{10});
//         co_await cv.notify_all();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(s, cv, m, l, 1),
//         make_waiter(s, cv, m, l, 2),
//         make_waiter(s, cv, m, l, 3),
//         make_all_waiting(s, l, e),
//         make_notify_all(s, cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 2);
//     REQUIRE(std::get<2>(results).return_value() == 3);
// }

// TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate notify_one until predicate passes",
// "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> counter{0};
//     std::atomic<int64_t> predicate_called{0};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<int64_t>& counter, std::atomic<int64_t>& predicate_called) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50}, [&counter, &predicate_called]() ->
//         bool
//         {
//             predicate_called++;
//             return counter == 1;
//         });
//         REQUIRE(status == true);
//         co_return 42;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& counter) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         co_await cv.notify_one(); // The predicate will not pass
//         counter++;
//         co_await cv.notify_one(); // The predicate will pass
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, counter, predicate_called), make_notifier(s,
//     cv, counter))); REQUIRE(std::get<0>(results).return_value() == 42); REQUIRE(std::get<1>(results).return_value()
//     == 0);

//     // The predicate is called 3 times
//     // 1) On the initial cv.wait() to see if its already satisfied.
//     // 2) On the first notify_all() when counter is still 0.
//     // 3) On the final notify_all() when the counter is now 1.
//     REQUIRE(predicate_called == 3);
// }

// TEST_CASE("wait_for(s lock duration) 1 waiter no_timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) ->
//     coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50});
//         REQUIRE(status == std::cv_status::no_timeout);
//         co_return (status == std::cv_status::no_timeout) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) ->
//     coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate no_timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> c{0};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{50}, [&c]() -> bool { return c == 1; });
//         REQUIRE(status == true);
//         co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         c++;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_for(s lock stop_token duration predicate) 1 waiter predicate stop_token no_timeout",
// "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::stop_source ss{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::stop_source& ss) -> coro::task<int64_t>
//     {

//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, ss.get_token(), std::chrono::milliseconds{50}, [&ss]() -> bool {
//         return ss.stop_requested(); }); REQUIRE(status == true); co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source&
//     ss) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         ss.request_stop();
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_for(s lock duration) 1 waiter timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) ->
//     coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10});
//         REQUIRE(status == std::cv_status::timeout);
//         co_return (status == std::cv_status::no_timeout) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) ->
//     coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{100});
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_for(s lock duration predicate) 1 waiter predicate timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<uint64_t> c{0};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<uint64_t>& c) -> coro::task<int64_t>
//     {
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10}, [&c]() -> bool { return c == 1; });
//         REQUIRE(status == false);
//         co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<uint64_t>& c) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{50});
//         c++;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_for(s lock duration) 3 waiters with timeout notify_all no_timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::latch l{3};
//     coro::event e{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     coro::latch& l, int64_t r) -> coro::task<int64_t>
//     {
//         co_await s->schedule();
//         auto lk = co_await m.scoped_lock();
//         l.count_down();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{20});
//         co_return (status == std::cv_status::no_timeout) ? r : -r;
//     };

//     auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await l;
//         e.set();
//         co_return 0;
//     };

//     auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await e;
//         co_await cv.notify_all();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(s, cv, m, l, 1),
//         make_waiter(s, cv, m, l, 2),
//         make_waiter(s, cv, m, l, 3),
//         make_all_waiting(s, l, e),
//         make_notify_all(s, cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 2);
//     REQUIRE(std::get<2>(results).return_value() == 3);
// }

// TEST_CASE("wait_for(s lock duration) 3 with notify_all timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     coro::latch l{3};
//     coro::event e{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     coro::latch& l, int64_t r) -> coro::task<int64_t>
//     {
//         co_await s->schedule();
//         auto lk = co_await m.scoped_lock();
//         l.count_down();
//         auto status = co_await cv.wait_for(s, lk, std::chrono::milliseconds{10});
//         co_return (status == std::cv_status::no_timeout) ? r : -r;
//     };

//     auto make_all_waiting = [](std::shared_ptr<coro::io_scheduler> s, coro::latch& l, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await l;
//         e.set();
//         co_return 0;
//     };

//     auto make_notify_all = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::event& e) ->
//     coro::task<int64_t>
//     {
//         co_await s->schedule();
//         co_await e;
//         co_await s->yield_for(std::chrono::milliseconds{50});
//         co_await cv.notify_all();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(
//         make_waiter(s, cv, m, l, 1),
//         make_waiter(s, cv, m, l, 2),
//         make_waiter(s, cv, m, l, 3),
//         make_all_waiting(s, l, e),
//         make_notify_all(s, cv, e)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == -2);
//     REQUIRE(std::get<2>(results).return_value() == -3);
// }

// TEST_CASE("wait_until(s lock time_point) 1 waiter no_timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) ->
//     coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, tp);
//         REQUIRE(status == std::cv_status::no_timeout);
//         co_return (status == std::cv_status::no_timeout) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) ->
//     coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_until(s lock time_point) 1 waiter timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m) ->
//     coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, tp);
//         REQUIRE(status == std::cv_status::timeout);
//         co_return (status == std::cv_status::no_timeout) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv) ->
//     coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{50});
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m), make_notifier(s, cv)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_until(s lock time_point predicate) 1 waiter predicate no_timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> c{0};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, tp, [&c]() -> bool { return c == 1; });
//         REQUIRE(status == true);
//         co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         c++;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_until(s lock time_point predicate) 1 waiter predicate timeout", "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::atomic<int64_t> c{0};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, tp, [&c]() -> bool { return c == 1; });
//         REQUIRE(status == false);
//         co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv,
//     std::atomic<int64_t>& c) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{50});
//         c++;
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, c), make_notifier(s, cv, c)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_until(s lock stop_token time_point predicate) 1 waiter predicate stop_token no_timeout",
// "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::stop_source ss{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::stop_source& ss) -> coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, ss.get_token(), tp, [&ss]() -> bool { return ss.stop_requested();
//         }); REQUIRE(status == true); co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source&
//     ss) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{10});
//         ss.request_stop();
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
//     REQUIRE(std::get<0>(results).return_value() == 1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// TEST_CASE("wait_until(s lock stop_token time_point predicate) 1 waiter predicate stop_token timeout",
// "[condition_variable]")
// {
//     auto s = coro::io_scheduler::make_shared(coro::io_scheduler::options{
//             .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});
//     coro::condition_variable cv{};
//     coro::mutex m{};
//     std::stop_source ss{};

//     auto make_waiter = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, coro::mutex& m,
//     std::stop_source& ss) -> coro::task<int64_t>
//     {
//         auto tp = std::chrono::steady_clock::now() + std::chrono::milliseconds{10};
//         auto lk = co_await m.scoped_lock();
//         auto status = co_await cv.wait_until(s, lk, ss.get_token(), tp, [&ss]() -> bool { return ss.stop_requested();
//         }); REQUIRE(status == false); co_return (status) ? 1 : -1;
//     };

//     auto make_notifier = [](std::shared_ptr<coro::io_scheduler> s, coro::condition_variable& cv, std::stop_source&
//     ss) -> coro::task<int64_t>
//     {
//         co_await s->yield_for(std::chrono::milliseconds{50});
//         ss.request_stop();
//         co_await cv.notify_one();
//         co_return 0;
//     };

//     auto results = coro::sync_wait(coro::when_all(make_waiter(s, cv, m, ss), make_notifier(s, cv, ss)));
//     REQUIRE(std::get<0>(results).return_value() == -1);
//     REQUIRE(std::get<1>(results).return_value() == 0);
// }

// #endif

#include <queue>

#ifdef LIBCORO_FEATURE_NETWORKING

TEST_CASE("condition_variable single waiter", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    std::cout << "condition_variable single waiter" << std::endl;

    auto                     sched = coro::default_executor::io_executor();
    coro::mutex              m;
    coro::condition_variable cv;

    auto make_test_task = [](auto sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<void>
    {
        co_await sched->schedule();

        {
            auto ulock = co_await m.scoped_lock();

            REQUIRE(co_await cv.wait_for(sched, ulock, 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE_FALSE(co_await cv.wait_for(sched, ulock, 8ms, []() { return false; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_for(sched, ulock, 8ms, []() { return true; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(sched, ulock, system_clock::now() + 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(sched, ulock, steady_clock::now() + 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE_FALSE(co_await cv.wait_until(sched, ulock, steady_clock::now() + 8ms, []() { return false; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(sched, ulock, steady_clock::now() + 8ms, []() { return true; }));
            REQUIRE_FALSE(m.try_lock());
        }

    #if defined(__clang__) || (defined(__GNUC__) && __GNUC__ > 13)

        auto make_locked_task_1 = [](auto sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.scoped_lock();

            co_await cv.wait(ulock);

            co_return true;
        };
        auto res1 = co_await sched->schedule(make_locked_task_1(sched, m, cv), 8ms);
        REQUIRE_FALSE(res1.has_value());
        co_await cv.notify_one();

        auto make_locked_task_2 = [](auto sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.scoped_lock();

            co_await cv.wait(
                ulock,
                []()
                {
                    static bool next   = false;
                    bool        result = next;
                    next               = true;
                    return result;
                });

            co_return true;
        };

        auto res2 = co_await sched->schedule(make_locked_task_2(sched, m, cv), 8ms);
        REQUIRE_FALSE(res2.has_value());
        co_await cv.notify_one();

        auto make_unlocked_task = [](auto sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.scoped_lock();

            co_await cv.wait(ulock, []() { return true; });
            co_return true;
        };

        auto res3 = co_await sched->schedule(make_unlocked_task(sched, m, cv), 8ms);
        REQUIRE(res3.has_value());

    #endif

        co_await sched->schedule_after(256ms);
        co_return;
    };

    coro::sync_wait(make_test_task(sched, m, cv));
}

TEST_CASE("condition_variable one notifier and one waiter", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    std::cout << "condition_variable one notifier and one waiter" << std::endl;

    struct BaseParams
    {
        std::shared_ptr<coro::io_scheduler> sched = coro::default_executor::io_executor();
        coro::mutex                         m;
        coro::condition_variable            cv;
    };

    BaseParams bp;

    auto make_notifier_task = [](BaseParams& bp, milliseconds start_delay = 0ms, bool all = false) -> coro::task<void>
    {
        co_await bp.sched->schedule_after(start_delay);
        if (all)
        {
            co_await bp.cv.notify_all();
        }
        else
        {
            co_await bp.cv.notify_one();
        }
        co_return;
    };

    auto make_waiter_task =
        [](BaseParams& bp, milliseconds start_delay = 0ms, milliseconds timeout = 16ms) -> coro::task<bool>
    {
        co_await bp.sched->schedule_after(start_delay);
        auto ulock  = co_await bp.m.scoped_lock();
        bool result = co_await bp.cv.wait_for(bp.sched, ulock, timeout) == std::cv_status::no_timeout;
        co_return result;
    };

    REQUIRE_FALSE(std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp), make_waiter_task(bp, 16ms))))
                      .return_value());
    coro::sync_wait(bp.cv.notify_one());

    REQUIRE_FALSE(
        std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 0ms, true), make_waiter_task(bp, 16ms))))
            .return_value());
    coro::sync_wait(bp.cv.notify_one());

    REQUIRE(
        std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 8ms), make_waiter_task(bp)))).return_value());

    REQUIRE(std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 8ms, true), make_waiter_task(bp))))
                .return_value());
}

TEST_CASE("condition_variable notify_all", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    std::cout << "condition_variable notify_all" << std::endl;

    struct BaseParams
    {
        std::shared_ptr<coro::io_scheduler> sched = coro::default_executor::io_executor();
        coro::mutex                         m;
        coro::condition_variable            cv;
        int                                 number_of_timeouts{};
    };

    BaseParams bp;

    auto make_notifier_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule_after(32ms);
        co_await bp.cv.notify_all();
        co_return;
    };

    auto make_waiter_task = [](BaseParams& bp, milliseconds timeout = 100ms) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        auto ulock = co_await bp.m.scoped_lock();
        if (co_await bp.cv.wait_for(bp.sched, ulock, timeout) == std::cv_status::timeout)
        {
            ++bp.number_of_timeouts;
        }
        co_return;
    };

    const int                     num_tasks{32};
    std::vector<coro::task<void>> tasks{};

    for (int64_t i = 0; i < num_tasks; ++i)
    {
        tasks.emplace_back(make_waiter_task(bp));
    }

    tasks.emplace_back(make_notifier_task(bp));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(bp.number_of_timeouts == 0);
}

#endif

TEST_CASE("condition_variable for thread-safe-queue between producers and consumers", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    std::cout << "condition_variable for thread-safe-queue between producers and consumers" << std::endl;

    struct BaseParams
    {
        std::shared_ptr<coro::thread_pool> sched = coro::default_executor::executor();
        coro::mutex                        m;
        coro::condition_variable           cv;
        std::atomic_bool                   cancel{false};
        std::atomic_int32_t                next{0};
        int32_t                            max_value{10000};
        std::queue<int32_t>                q;
        std::set<int32_t>                  values_not_delivered;
        std::set<int32_t>                  values_not_produced;
        std::atomic_int32_t                producers{0};
        std::atomic_int32_t                consumers{0};
    };

    BaseParams bp;

    auto make_producer_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        while (!bp.cancel.load(std::memory_order::acquire))
        {
            {
                auto ulock = co_await bp.m.scoped_lock();
                REQUIRE_FALSE(bp.m.try_lock());
                auto value = bp.next.fetch_add(1, std::memory_order::acq_rel);

                // limit for end of test
                if (value >= bp.max_value)
                {
                    break;
                }

                bp.values_not_delivered.insert(value);
                bp.q.push(value);
            }
            co_await bp.cv.notify_one();
        }
        bp.producers.fetch_sub(1, std::memory_order::acq_rel);
        co_return;
    };

    auto make_consumer_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        while (true)
        {
            auto ulock = co_await bp.m.scoped_lock();
            co_await bp.cv.wait(
                ulock,
                [&bp]()
                {
                    REQUIRE_FALSE(bp.m.try_lock());
                    return bp.q.size() || bp.cancel.load(std::memory_order::acquire);
                });
            REQUIRE_FALSE(bp.m.try_lock());
            if (bp.cancel.load(std::memory_order::acquire))
            {
                break;
            }

            auto value = bp.q.front();
            bp.q.pop();
            auto ok = bp.values_not_delivered.erase(value);
            if (!ok)
            {
                bp.values_not_produced.insert(value);
            }
        }

        bp.consumers.fetch_sub(1, std::memory_order::acq_rel);
        co_return;
    };

    auto make_director_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        while (true)
        {
            {
                auto ulock = co_await bp.m.scoped_lock();
                REQUIRE_FALSE(bp.m.try_lock());
                if ((bp.next.load(std::memory_order::acquire) >= bp.max_value) && bp.q.empty())
                {
                    break;
                }
            }

            std::this_thread::sleep_for(16ms);
            co_await bp.sched->yield();
        }

        std::this_thread::sleep_for(64ms);
        bp.cancel.store(true, std::memory_order::release);
        co_await bp.cv.notify_all();
        co_return;
    };

    std::vector<coro::task<void>> tasks{};

    for (int64_t i = 0; i < 64; ++i)
    {
        tasks.emplace_back(make_consumer_task(bp));
        bp.consumers.fetch_add(1, std::memory_order::acq_rel);
    }

    for (int64_t i = 0; i < 16; ++i)
    {
        tasks.emplace_back(make_producer_task(bp));
        bp.producers.fetch_add(1, std::memory_order::acq_rel);
    }

    tasks.emplace_back(make_director_task(bp));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(bp.values_not_delivered.size() == 0);
    REQUIRE(bp.values_not_produced.size() == 0);

    std::cout << "condition_variable test completed" << std::endl;
}
