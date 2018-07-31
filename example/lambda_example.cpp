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
    for (size_t i = 0; i < N; i++)
    {
        // example passing a lambda from multiple threads
        std::thread([&queue, &result]() {
            auto f = queue.try_push([&result]() { return ++result; });
            // use future::get to synchronise/wait
            // if (f.valid())
            // f.get();
        })
            .detach();
    }
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
