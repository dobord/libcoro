#pragma once

#include "coro/detail/cache_pool.hpp"
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <vector>

namespace coro::detail
{

template<typename Node>
class cache_pool; // provided by cache_pool.hpp

// Experimental Chase–Lev style deque with dynamic resize performed by owner thread.
// Uses a shared cache-pool allocator for buffer blocks; blocks reclaimed in destructor.
class work_stealing_deque
{
public:
    using value_type = std::coroutine_handle<>;

    struct block
    {
        std::size_t capacity{0};
        value_type* items{nullptr};
        block(std::size_t cap)
            : capacity(cap),
              items(static_cast<value_type*>(::operator new[](sizeof(value_type) * cap)))
        {
        }
        ~block() { ::operator delete[](items); }
    };

    explicit work_stealing_deque(cache_pool<block>& pool, std::size_t initial_capacity = 256) noexcept : m_pool(pool)
    {
        m_block = m_pool.allocate(initial_capacity);
        m_blocks_owned.push_back(m_block);
        m_top.store(0, std::memory_order::relaxed);
        m_bottom.store(0, std::memory_order::relaxed);
        m_resizing.store(false, std::memory_order::relaxed);
    }

    ~work_stealing_deque()
    {
        for (auto* b : m_blocks_owned)
        {
            m_pool.deallocate(b);
        }
    }

    // Owner-only
    void push_bottom(value_type v) noexcept
    {
        while (true)
        {
            if (m_resizing.load(std::memory_order::acquire))
            {
                continue;
            }
            std::size_t b = m_bottom.load(std::memory_order::relaxed);
            std::size_t t = m_top.load(std::memory_order::acquire);
            if (b - t < m_block->capacity)
            {
                m_block->items[b % m_block->capacity] = v;
                m_bottom.store(b + 1, std::memory_order::release);
                return;
            }
            grow();
        }
    }

    // Owner-only
    std::optional<value_type> pop_bottom() noexcept
    {
        while (true)
        {
            if (m_resizing.load(std::memory_order::acquire))
            {
                continue;
            }
            // Read current bounds; early-exit if empty to avoid underflow artifacts.
            std::size_t b = m_bottom.load(std::memory_order::relaxed);
            std::size_t t = m_top.load(std::memory_order::acquire);
            if (t >= b)
            {
                return std::nullopt; // empty
            }
            b = b - 1;
            m_bottom.store(b, std::memory_order::relaxed);
            std::atomic_thread_fence(std::memory_order::seq_cst);
            t = m_top.load(std::memory_order::relaxed);
            if (t <= b)
            {
                value_type v = m_block->items[b % m_block->capacity];
                if (t == b)
                {
                    if (!m_top.compare_exchange_strong(
                            t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
                    {
                        v = value_type{}; // stolen
                    }
                    m_bottom.store(b + 1, std::memory_order::relaxed);
                }
                return v;
            }
            else
            {
                m_bottom.store(b + 1, std::memory_order::relaxed);
                return std::nullopt;
            }
        }
    }

    // Thief-only
    std::optional<value_type> steal_top() noexcept
    {
        while (true)
        {
            if (m_resizing.load(std::memory_order::acquire))
            {
                return std::nullopt;
            }
            std::size_t t = m_top.load(std::memory_order::acquire);
            std::atomic_thread_fence(std::memory_order::seq_cst);
            std::size_t b = m_bottom.load(std::memory_order::acquire);
            if (t >= b)
            {
                return std::nullopt;
            }
            value_type v = m_block->items[t % m_block->capacity];
            if (m_top.compare_exchange_strong(t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
            {
                return v;
            }
            // lost race; retry
        }
    }

private:
    void grow() noexcept
    {
        bool expected = false;
        if (!m_resizing.compare_exchange_strong(expected, true, std::memory_order::acq_rel))
        {
            return; // someone else growing; owner will retry
        }
        // owner-only path
        block*      old     = m_block;
        std::size_t old_cap = old->capacity;
        std::size_t t       = m_top.load(std::memory_order::relaxed);
        std::size_t b       = m_bottom.load(std::memory_order::relaxed);
        std::size_t size    = b - t;
        std::size_t new_cap = old_cap * 2;
        block*      nb      = m_pool.allocate(new_cap);
        m_blocks_owned.push_back(nb);
        for (std::size_t i = 0; i < size; ++i)
        {
            nb->items[i] = old->items[(t + i) % old_cap];
        }
        m_block = nb;
        m_top.store(0, std::memory_order::release);
        m_bottom.store(size, std::memory_order::release);
        m_resizing.store(false, std::memory_order::release);
        // Note: old block kept until destructor to avoid races with thieves
    }

private:
    cache_pool<block>&       m_pool;
    block*                   m_block{nullptr};
    std::vector<block*>      m_blocks_owned{}; // reclaimed at destructor
    std::atomic<std::size_t> m_top{0};
    std::atomic<std::size_t> m_bottom{0};
    std::atomic<bool>        m_resizing{false};
};

} // namespace coro::detail
