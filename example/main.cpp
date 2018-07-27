#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <cassert>
#include <iostream>

std::atomic<std::size_t> memory{0};
std::atomic<std::size_t> alloc{0};

void* operator new(std::size_t s) throw(std::bad_alloc)
{
    memory += s;
    ++alloc;
    return malloc(s);
}

void operator delete(void* p) throw()
{
    --alloc;
    free(p);
}
#define LAMDBA_EXAMPLE
#ifdef LAMDBA_EXAMPLE
int main()
{
    using namespace std::chrono_literals;
    using TaskQueue = tasks::threadsafe::Queue<int, 512, 65536>;
    TaskQueue queue;
    tasks::Scheduler<TaskQueue> scheduler(queue, -1);

    alloc  = 0;
    memory = 0;

    int n       = 0;
    const int N = 256;
    std::atomic<int> result{0};
    while (n++ < N)
    {
        // example passing a lambda
        auto f = queue.try_push([&result]() { return ++result; });

        // use future::get to synchronise/wait
        // if (f.valid())
        // f.get();
    }
    std::cout << "result: " << result << std::endl;
    std::cout << "allocations: " << alloc << " memory: " << memory << std::endl;
    if (result == N)
    {
        std::cout << "Passed" << std::endl;
    }
    else
    {
        std::cout << "Failed" << std::endl;
    }
    return 0;
}
#else
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

    alloc  = 0;
    memory = 0;

    int n       = 0;
    const int N = 256;
    std::atomic<int> result{0};
    Op op;
    while (n++ < N)
    {
        auto f = queue.try_push(&Op::sum, op, n, n * 2);
    }
    std::cout << "result: " << result << std::endl;
    std::cout << "allocations: " << alloc << " memory: " << memory << std::endl;
    return 0;
}
#endif
