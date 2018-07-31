#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <cassert>
#include <iostream>

std::atomic<int> result;

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
    using TaskQueue = tasks::threadsafe::Queue<void, 512, 65536>;
    TaskQueue queue;
    tasks::Scheduler<TaskQueue> scheduler(queue);

    const int N = 256;
    Op op;
    std::future<void> f;
    for (size_t i = 0; i < N; i++)
    {
        f = queue.try_push(&Op::sum, op, i, i * 2);
    }

    // wait for the last future to finish. Potentially we should wait for all futures to
    // have finished.
    f.get();

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
