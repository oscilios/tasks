#ifndef TASKS_SCHEDULER_H
#define TASKS_SCHEDULER_H

#include "ThreadUtilities.h"
#include <atomic>
#include <future>
#include <memory>
#include <vector>

namespace tasks
{
    template <typename TTaskQueue>
    class Scheduler;
}

template <typename TTaskQueue>
class tasks::Scheduler
{
    using future_type = std::future<typename TTaskQueue::task_return_type>;
    std::atomic<bool> m_done{false};
    TTaskQueue& m_queue;
    std::vector<std::thread> m_threads;

    void workerThread()
    {
        flushDenormalsToZero();
        while (!m_done)
        {
            while (m_queue.try_call_next())
            {
            }
            std::this_thread::yield();
        }
    }

public:
    Scheduler(TTaskQueue& queue, int threadCount = -1, bool realtimePriority = false)
    : m_queue(queue)
    {
        if (threadCount <= 0)
        {
            threadCount = std::thread::hardware_concurrency();
        }
        else
        {
            threadCount = std::min<int>(threadCount, std::thread::hardware_concurrency());
        }

        try
        {
            for (int i = 0; i < threadCount; i++)
            {
                m_threads.emplace_back(std::thread(&Scheduler::workerThread, this));
                if (realtimePriority)
                {
                    setRealtimePriority(&m_threads[i]);
                }
            }
        }
        catch (...)
        {
            m_done = true;
            throw;
        }
    }
    ~Scheduler()
    {
        m_done = true;
        for (auto& thread : m_threads)
        {
            if (thread.joinable())
                thread.join();
        }
    }
};

#endif // TASKS_SCHEDULER_H
