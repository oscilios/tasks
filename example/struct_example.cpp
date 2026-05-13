#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <cassert>
#include <iostream>

std::atomic<int> result{0};

struct Op
{
    void sum(int x, int y) const
    {
        result += x + y;
    }
};

int main()
{
    using namespace std::chrono_literals;
    using Pool      = tasks::memory::MemoryPool<65536>;
    using TaskQueue = tasks::threadsafe::Queue<void, 512, Pool>;
    Pool      pool;
    TaskQueue queue(pool);
    tasks::Scheduler<TaskQueue> scheduler(queue);

    const int N = 256;
    Op op;
    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < N; i++)
    {
        futures.emplace_back(queue.try_push(&Op::sum, op, i, i * 2));
        assert (futures.back().valid());
    }

    for (auto& f : futures)
    {
        f.wait();
    }

    int expected = 0;
    for (size_t i = 0; i < N; i++)
    {
        expected += 3*i;
    }

    if (expected == result)
    {
        std::cout << "Passed" << std::endl;
    }
    else
    {
        std::cout << "Failed\n\tresult: " << result << "\n\texpected: " << expected << std::endl;
    }
    return 0;
}
