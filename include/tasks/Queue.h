#ifndef SYSTEM_LOGGER_THREADSAFE_QUEUE_H
#define SYSTEM_LOGGER_THREADSAFE_QUEUE_H

#include "Allocator.h"
#include <atomic>
#include <cassert>
#include <future>
#include <type_traits>
#include <vector>

namespace tasks
{
    namespace threadsafe
    {
        template <typename TCallableReturnType, int64_t TMaxSize, size_t TMemoryPoolSize>
        class Queue;
    }

    template <size_t N>
    struct IsPowerOfTwo
    {
        constexpr static bool value = (N > 0) && (N & (N - 1)) == 0;
    };

}

template <typename TCallableReturnType, int64_t TMaxSize, size_t TMemoryPoolSize>
class tasks::threadsafe::Queue final
{
    using task_type   = std::packaged_task<TCallableReturnType()>;
    using future_type = std::future<TCallableReturnType>;

    std::atomic<int64_t> m_readIdx{0};
    std::atomic<int64_t> m_read{0};
    alignas(64) char m_padToAvoidFalseSharing[64 - 2 * sizeof(int64_t)];
    std::atomic<int64_t> m_writeIdx{0};
    std::atomic<int64_t> m_written{0};
    alignas(64) char m_padToAvoidFalseSharing2[64 - 2 * sizeof(int64_t)];

    memory::Allocator<void, TMemoryPoolSize> m_allocator;
    std::vector<task_type> m_buffer;
    static constexpr size_t m_bitmask{TMaxSize - 1};

public:
    using task_return_type = TCallableReturnType;
    Queue() noexcept(false)
    : m_buffer(TMaxSize)
    {
        static_assert(IsPowerOfTwo<TMaxSize>::value, "Queue max size must be a power of two");
    }
    Queue(const Queue&)     = delete;
    Queue(Queue&&) noexcept = delete;
    Queue& operator=(const Queue&) = delete;
    Queue& operator=(Queue&&) noexcept = delete;
    ~Queue()                           = default;

    bool try_call_next()
    {
        auto rIdx       = m_read.load(std::memory_order_acquire);
        const auto wIdx = m_written.load(std::memory_order_acquire);

        if (rIdx >= wIdx)
            return false;

        while (!m_readIdx.compare_exchange_weak(
            rIdx, rIdx + 1, std::memory_order_release, std::memory_order_relaxed))
        {
            if (rIdx >= wIdx)
                return false;
        }
        if (rIdx >= wIdx)
            return false;

        auto& x = m_buffer[rIdx & m_bitmask];
        x();
        m_read.fetch_add(1, std::memory_order_release);
        return true;
    }
    template <typename TCallable, typename... Args>
    future_type try_push(TCallable&& func, Args&&... args)
    {
        auto task =
            task_type(std::allocator_arg,
                      m_allocator,
                      std::bind(std::forward<TCallable>(func), std::forward<Args>(args)...));
        auto future = task.get_future();

        auto wIdx       = m_written.load(std::memory_order_acquire);
        const auto rIdx = m_read.load(std::memory_order_acquire);

        if (wIdx - rIdx >= TMaxSize - 1)
        {
            // buffer is full
            assert(false);
            return future_type{};
        }

        while (!m_writeIdx.compare_exchange_weak(
            wIdx, wIdx + 1, std::memory_order_release, std::memory_order_relaxed))
        {
            if (wIdx - rIdx >= TMaxSize - 1)
            {
                assert(false);
                return future_type{};
            }
        }

        m_buffer[wIdx & m_bitmask] = std::move(task);
        m_written.fetch_add(1, std::memory_order_release);
        return future;
    }
};

#endif // SYSTEM_LOGGER_THREADSAFE_QUEUE_H
