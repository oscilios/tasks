#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <cassert>
#include <iostream>

struct Op
{
    int sum(int x, int y) const
    {
        return x + y;
    }
};

int main()
{
    using namespace std::chrono_literals;
    using TaskQueue = tasks::threadsafe::Queue<int, 512, 65536>;
    TaskQueue queue;
    tasks::Scheduler<TaskQueue> scheduler(queue, -1);

    const int N = 256;
    Op op;
    std::vector<std::future<int> > futures;
    futures.reserve(N);
    for (size_t i = 0; i < N; i++)
    {
        futures.emplace_back(queue.try_push(&Op::sum, op, i, i * 2));
    }

    int result = 0;
    for (auto& f : futures)
    {
        result += f.get();
    }

    int expected = 0;
    for (size_t i = 0; i < N; i++)
    {
        expected += op.sum(i, i * 2);
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
