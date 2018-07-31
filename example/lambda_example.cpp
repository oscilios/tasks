#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace std::chrono_literals;
    using TaskQueue = tasks::threadsafe::Queue<int, 512, 65536>;
    TaskQueue queue;
    tasks::Scheduler<TaskQueue> scheduler(queue);

    const int N = 256;
    std::atomic<int> result{0};
    std::future<int> f;
    for (size_t i = 0; i < N; i++)
    {
        // example passing a lambda from multiple threads
        std::thread([&f, &queue, &result]() {
            f = queue.try_push([&result]() { return ++result; });
            assert(f.valid());
        })
            .detach();
    }
    // wait for the last future to be processed.
    f.wait();
    if (result == N)
    {
        std::cout << "Passed" << std::endl;
    }
    else
    {
        std::cout << "Failed\n\tresult: " << result << "\n\texpected: " << N << std::endl;
    }
    return 0;
}
