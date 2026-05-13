#ifndef TASKS_SCHEDULER_H
#define TASKS_SCHEDULER_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace tasks
{
    template <typename TTaskQueue>
    class Scheduler;
}

// Drives a TTaskQueue from a pool of worker threads. Workers block on the queue's wake counter
// when there's no work, so an idle scheduler does not burn CPU. Shutdown semantics:
//   - stop()           : signal and join; queued-but-not-started tasks are abandoned. Idempotent.
//   - drain_and_stop() : wait until is_idle (pushes == completions), then stop. Caller must have
//                        stopped producing before calling.
//   - ~Scheduler()     : calls stop().
template <typename TTaskQueue>
class tasks::Scheduler
{
    std::atomic<bool>        m_done{false};
    TTaskQueue&              m_queue;
    std::vector<std::thread> m_threads;

    void workerThread() noexcept
    {
        std::uint64_t seen = m_queue.work_seq();
        while (!m_done.load(std::memory_order_acquire))
        {
            while (m_queue.try_call_next())
            {
            }
            if (m_done.load(std::memory_order_acquire))
                break;

            const auto current = m_queue.work_seq();
            if (current == seen)
                m_queue.wait_for_work(seen);
            seen = m_queue.work_seq();
        }
    }

public:
    explicit Scheduler(TTaskQueue& queue, int threadCount = -1)
    : m_queue(queue)
    {
        const auto hardware = static_cast<int>(std::thread::hardware_concurrency());
        if (threadCount <= 0)
            threadCount = hardware;
        else
            threadCount = std::min<int>(threadCount, hardware);

        try
        {
            m_threads.reserve(static_cast<std::size_t>(threadCount));
            for (int i = 0; i < threadCount; ++i)
                m_threads.emplace_back(&Scheduler::workerThread, this);
        }
        catch (...)
        {
            m_done.store(true, std::memory_order_release);
            m_queue.wake_all_waiters();
            for (auto& t : m_threads)
                if (t.joinable())
                    t.join();
            throw;
        }
    }

    Scheduler(const Scheduler&)            = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    ~Scheduler() { stop(); }

    void stop() noexcept
    {
        bool expected = false;
        if (!m_done.compare_exchange_strong(expected,
                                            true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            return;
        m_queue.wake_all_waiters();
        for (auto& t : m_threads)
            if (t.joinable())
                t.join();
    }

    void drain_and_stop() noexcept
    {
        using namespace std::chrono_literals;
        while (!m_queue.is_idle())
            std::this_thread::sleep_for(1ms);
        stop();
    }
};

#endif // TASKS_SCHEDULER_H
