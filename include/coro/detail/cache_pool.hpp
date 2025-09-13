#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

// A very small typed cache pool for fixed-size nodes using a lock-free free-list (no ABA protection).
// Intended to be used for a single node type (e.g., deque chunk) shared by all workers.

namespace coro::detail
{

struct free_node
{
    free_node* next;
};

template<typename Node>
class cache_pool
{
public:
    cache_pool()  = default;
    ~cache_pool() = default;

    cache_pool(const cache_pool&)            = delete;
    cache_pool& operator=(const cache_pool&) = delete;

    template<typename... Args>
    Node* allocate(Args&&... args)
    {
        static_assert(sizeof(Node) >= sizeof(free_node), "Node must fit free_node");
        while (true)
        {
            free_node* head = m_head.load(std::memory_order::acquire);
            if (!head)
            {
                return ::new Node(std::forward<Args>(args)...);
            }
            free_node* next = head->next;
            if (m_head.compare_exchange_weak(head, next, std::memory_order::acq_rel, std::memory_order::acquire))
            {
                return ::new (head) Node(std::forward<Args>(args)...);
            }
        }
    }

    void deallocate(Node* p) noexcept
    {
        if (!p)
            return;
        p->~Node();
        auto* node = reinterpret_cast<free_node*>(p);
        while (true)
        {
            free_node* head = m_head.load(std::memory_order::acquire);
            node->next      = head;
            if (m_head.compare_exchange_weak(head, node, std::memory_order::acq_rel, std::memory_order::acquire))
            {
                return;
            }
        }
    }

private:
    std::atomic<free_node*> m_head{nullptr};
};

} // namespace coro::detail
