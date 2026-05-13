#ifndef TASKS_THREADSAFE_QUEUE_H
#define TASKS_THREADSAFE_QUEUE_H

#include "Allocator.h"
#include "MPMCSequencer.h"
#include "PackagedTask.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <future>
#include <new>
#include <vector>

namespace tasks
{
    namespace threadsafe
    {
        template <typename TCallableReturnType, int64_t TMaxSize, typename TPool>
        class Queue;
    }
}

template <typename TCallableReturnType, int64_t TMaxSize, typename TPool>
class tasks::threadsafe::Queue final
{
    using task_type      = tasks::PackagedTask<TCallableReturnType()>;
    using future_type    = std::future<TCallableReturnType>;
    using allocator_type = memory::Allocator<void, TPool>;
    using sequencer_type = MPMCSequencer<static_cast<std::size_t>(TMaxSize)>;

    sequencer_type         m_seq;
    allocator_type         m_allocator;
    std::vector<task_type> m_buffer;

    alignas(64) std::atomic<std::uint64_t> m_push_count{0};
    alignas(64) std::atomic<std::uint64_t> m_complete_count{0};
    alignas(64) std::atomic<std::uint64_t> m_work_seq{0};

public:
    using task_return_type = TCallableReturnType;
    using pool_type        = TPool;

    explicit Queue(TPool& pool) noexcept(false)
    : m_allocator(pool)
    , m_buffer(TMaxSize)
    {
    }

    Queue(const Queue&)            = delete;
    Queue(Queue&&)                 = delete;
    Queue& operator=(const Queue&) = delete;
    Queue& operator=(Queue&&)      = delete;
    ~Queue()                       = default;

    bool try_call_next()
    {
        const auto ticket = m_seq.try_claim_read();
        if (!ticket)
            return false;
        auto& slot = m_buffer[ticket->slot()];
        assert(slot.valid() && "sequencer handed out an unfilled slot");
        slot();
        m_seq.publish_read(*ticket);
        m_complete_count.fetch_add(1, std::memory_order_release);
        return true;
    }

    std::uint64_t work_seq() const noexcept
    {
        return m_work_seq.load(std::memory_order_acquire);
    }

    void wait_for_work(std::uint64_t seen) noexcept
    {
        m_work_seq.wait(seen, std::memory_order_acquire);
    }

    void wake_all_waiters() noexcept
    {
        m_work_seq.fetch_add(1, std::memory_order_release);
        m_work_seq.notify_all();
    }

    bool is_idle() const noexcept
    {
        return m_push_count.load(std::memory_order_acquire) ==
               m_complete_count.load(std::memory_order_acquire);
    }

    template <typename TCallable, typename... Args>
    future_type try_push(TCallable&& func, Args&&... args)
    {
        task_type task;
        try
        {
            task = task_type(std::allocator_arg,
                             m_allocator,
                             std::bind(std::forward<TCallable>(func),
                                       std::forward<Args>(args)...));
        }
        catch (const std::bad_alloc&)
        {
            return future_type{};
        }
        auto future = task.get_future();

        const auto ticket = m_seq.try_claim_write();
        if (!ticket)
            return future_type{};

        m_buffer[ticket->slot()] = std::move(task);
        m_seq.publish_write(*ticket);
        m_push_count.fetch_add(1, std::memory_order_release);
        m_work_seq.fetch_add(1, std::memory_order_release);
        m_work_seq.notify_one();
        return future;
    }
};

#endif // TASKS_THREADSAFE_QUEUE_H
