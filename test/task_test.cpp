#include "tasks/Allocator.h"
#include "tasks/Function.h"
#include "tasks/MPMCSequencer.h"
#include "tasks/PackagedTask.h"
#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <thread>

// Replace new and delete just for the purpose of demonstrating that
//  they are not called.

std::atomic<std::size_t> memory{0};
std::atomic<std::size_t> alloc{0};

void* operator new(std::size_t s) /*throw() (std::bad_alloc)*/
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

template <typename T>
T max_value()
{
    return std::numeric_limits<T>::max();
}

struct Foo
{
    int getValue() const
    {
        return max_value<int>();
    }
    int sum(int x, int y) const
    {
        return x + y;
    }
    float multiply(float x, float y) const
    {
        return x * y;
    }
};

TEST_CASE("no memory is allocated in packaged_task", "[tasks]")
{
    tasks::memory::MemoryPool<1024> pool;
    tasks::memory::MemoryPool<1024>::allocator_type<void> allocator(pool);
    alloc  = 0;
    memory = 0;

    auto p             = std::promise<int>(std::allocator_arg, allocator);
    std::future<int> f = p.get_future();
    p.set_value(123);
    f.wait();
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);

    auto task = tasks::PackagedTask<int()>(std::allocator_arg, allocator, []() { return 42; });
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task();
    REQUIRE(task.get_future().get() == 42);

    task = tasks::PackagedTask<int()>(std::allocator_arg, allocator, max_value<int>);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task();
    REQUIRE(task.get_future().get() == std::numeric_limits<int>::max());

    Foo foo;
    task =
        tasks::PackagedTask<int()>(std::allocator_arg, allocator, std::bind(&Foo::getValue, foo));
    auto empty_task = tasks::PackagedTask<int()>{};
    empty_task      = std::move(task);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    empty_task();
    REQUIRE(empty_task.get_future().get() == std::numeric_limits<int>::max());

    auto task_float = tasks::PackagedTask<float()>(
        std::allocator_arg, allocator, std::bind(&Foo::multiply, foo, 2.5f, 3.5f));
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task_float();
    REQUIRE(task_float.get_future().get() == 8.75f);
}
TEST_CASE("no memory is allocated in queue", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<4096>;
    using TaskQueue = tasks::threadsafe::Queue<int, 16, Pool>;
    Pool      pool;
    TaskQueue queue(pool);
    alloc  = 0;
    memory = 0;

    int value   = 0;
    auto future = queue.try_push(
        [&value]()
        {
            value = 42;
            return value;
        });
    REQUIRE(future.valid());
    queue.try_call_next();

    REQUIRE(value == 42);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);

    future = queue.try_push(max_value<int>);
    REQUIRE(future.valid());
    queue.try_call_next();
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    REQUIRE(future.get() == std::numeric_limits<int>::max());

    Foo foo;
    future = queue.try_push(&Foo::getValue, foo);
    REQUIRE(future.valid());
    queue.try_call_next();
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    REQUIRE(future.get() == std::numeric_limits<int>::max());

    future = queue.try_push(&Foo::sum, foo, 2, 3);
    REQUIRE(future.valid());
    queue.try_call_next();
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    REQUIRE(future.get() == 5);
}

TEST_CASE("queues on separate pools have isolated storage", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<4096>;
    using TaskQueue = tasks::threadsafe::Queue<int, 16, Pool>;
    Pool      poolA;
    Pool      poolB;
    TaskQueue queueA(poolA);
    TaskQueue queueB(poolB);

    REQUIRE(poolA.used() == 0);
    REQUIRE(poolB.used() == 0);

    auto fA = queueA.try_push([]() { return 1; });
    REQUIRE(fA.valid());
    REQUIRE(poolA.used() > 0);
    REQUIRE(poolB.used() == 0);

    auto fB = queueB.try_push([]() { return 2; });
    REQUIRE(fB.valid());
    const auto usedAfterB = poolB.used();
    REQUIRE(usedAfterB > 0);

    auto fA2 = queueA.try_push([]() { return 3; });
    REQUIRE(fA2.valid());
    REQUIRE(poolB.used() == usedAfterB);

    queueA.try_call_next();
    queueA.try_call_next();
    queueB.try_call_next();
    REQUIRE(fA.get() == 1);
    REQUIRE(fB.get() == 2);
    REQUIRE(fA2.get() == 3);
}

TEST_CASE("two queues can share one pool", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<4096>;
    using TaskQueue = tasks::threadsafe::Queue<int, 16, Pool>;
    Pool      pool;
    TaskQueue queueA(pool);
    TaskQueue queueB(pool);

    auto fA = queueA.try_push([]() { return 10; });
    const auto usedAfterA = pool.used();
    REQUIRE(usedAfterA > 0);

    auto fB = queueB.try_push([]() { return 20; });
    REQUIRE(pool.used() > usedAfterA);

    queueA.try_call_next();
    queueB.try_call_next();
    REQUIRE(fA.get() == 10);
    REQUIRE(fB.get() == 20);
}

TEST_CASE("try_push returns an invalid future when the pool is exhausted", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<512>;
    using TaskQueue = tasks::threadsafe::Queue<int, 16, Pool>;
    Pool      pool;
    TaskQueue queue(pool);

    int  successes   = 0;
    bool got_invalid = false;
    for (int i = 0; i < 32 && !got_invalid; ++i)
    {
        auto f = queue.try_push([]() { return 42; });
        if (f.valid())
            ++successes;
        else
            got_invalid = true;
    }
    REQUIRE(successes > 0);
    REQUIRE(got_invalid);
}

TEST_CASE("queues sharing a pool can be driven concurrently from many threads", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<131072>;
    using TaskQueue = tasks::threadsafe::Queue<int, 1024, Pool>;

    Pool      pool;
    TaskQueue queueA(pool);
    TaskQueue queueB(pool);
    tasks::Scheduler<TaskQueue> schedA(queueA);
    tasks::Scheduler<TaskQueue> schedB(queueB);

    constexpr int producersPerQueue = 4;
    constexpr int tasksPerProducer  = 50;

    auto produce = [](TaskQueue& q, int seed)
    {
        std::vector<std::future<int>> futures;
        futures.reserve(tasksPerProducer);
        long long expected   = 0;
        bool      all_valid  = true;
        for (int i = 0; i < tasksPerProducer; ++i)
        {
            const int v = seed * 1000 + i;
            expected += v;
            auto f = q.try_push([v]() { return v; });
            if (!f.valid())
            {
                all_valid = false;
                continue;
            }
            futures.emplace_back(std::move(f));
        }
        long long got = 0;
        for (auto& f : futures)
            got += f.get();
        return std::make_tuple(expected, got, all_valid);
    };

    std::vector<std::future<std::tuple<long long, long long, bool>>> producers;
    producers.reserve(producersPerQueue * 2);
    for (int i = 0; i < producersPerQueue; ++i)
        producers.emplace_back(std::async(std::launch::async, produce, std::ref(queueA), i));
    for (int i = 0; i < producersPerQueue; ++i)
        producers.emplace_back(
            std::async(std::launch::async, produce, std::ref(queueB), 100 + i));

    for (auto& p : producers)
    {
        auto [expected, got, all_valid] = p.get();
        REQUIRE(all_valid);
        REQUIRE(expected == got);
    }
}

TEST_CASE("Function holds heterogeneous callables through one type", "[tasks]")
{
    using Pool       = tasks::memory::MemoryPool<1024>;
    using AllocVoid  = Pool::allocator_type<void>;

    Pool      pool;
    AllocVoid alloc(pool);

    SECTION("lambda")
    {
        tasks::Function<int()> f(std::allocator_arg, alloc, []() { return 7; });
        REQUIRE(f.valid());
        REQUIRE(f() == 7);
    }

    SECTION("function pointer")
    {
        tasks::Function<int()> f(std::allocator_arg, alloc, max_value<int>);
        REQUIRE(f() == std::numeric_limits<int>::max());
    }

    SECTION("bind result for member function")
    {
        Foo foo;
        tasks::Function<int()> f(std::allocator_arg, alloc, std::bind(&Foo::sum, foo, 2, 3));
        REQUIRE(f() == 5);
    }

    SECTION("default-constructed is not valid")
    {
        tasks::Function<int()> f;
        REQUIRE_FALSE(f.valid());
    }

    SECTION("move transfers ownership; moved-from is invalid")
    {
        tasks::Function<int()> src(std::allocator_arg, alloc, []() { return 42; });
        REQUIRE(src.valid());
        tasks::Function<int()> dst(std::move(src));
        REQUIRE_FALSE(src.valid());
        REQUIRE(dst.valid());
        REQUIRE(dst() == 42);
    }

    SECTION("void signature works")
    {
        int             side = 0;
        tasks::Function<void()> f(std::allocator_arg, alloc, [&side]() { side = 1; });
        f();
        REQUIRE(side == 1);
    }
}

TEST_CASE("Scheduler::stop is idempotent", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<4096>;
    using TaskQueue = tasks::threadsafe::Queue<int, 16, Pool>;
    Pool                        pool;
    TaskQueue                   queue(pool);
    tasks::Scheduler<TaskQueue> sched(queue, 2);

    sched.stop();
    sched.stop();
    sched.stop();
    SUCCEED("stop() called multiple times without hanging or aborting");
}

TEST_CASE("Scheduler::drain_and_stop waits for all pending tasks", "[tasks]")
{
    using Pool      = tasks::memory::MemoryPool<65536>;
    using TaskQueue = tasks::threadsafe::Queue<int, 1024, Pool>;

    Pool                        pool;
    TaskQueue                   queue(pool);
    tasks::Scheduler<TaskQueue> sched(queue, 2);

    std::atomic<int> count{0};
    constexpr int    N = 200;
    for (int i = 0; i < N; ++i)
    {
        auto f = queue.try_push([&count]() { count.fetch_add(1); return 0; });
        REQUIRE(f.valid());
    }

    sched.drain_and_stop();
    REQUIRE(queue.is_idle());
    REQUIRE(count.load() == N);
}

TEST_CASE("MPMCSequencer basic single-threaded ops", "[tasks]")
{
    tasks::MPMCSequencer<4> seq;

    REQUIRE_FALSE(seq.try_claim_read().has_value());

    auto w1 = seq.try_claim_write();
    REQUIRE(w1.has_value());
    seq.publish_write(*w1);

    auto r1 = seq.try_claim_read();
    REQUIRE(r1.has_value());
    REQUIRE(r1->slot() == w1->slot());
    seq.publish_read(*r1);

    REQUIRE_FALSE(seq.try_claim_read().has_value());

    auto a = seq.try_claim_write();
    auto b = seq.try_claim_write();
    auto c = seq.try_claim_write();
    auto d = seq.try_claim_write();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(d.has_value());
    seq.publish_write(*a);
    seq.publish_write(*b);
    seq.publish_write(*c);
    seq.publish_write(*d);

    REQUIRE_FALSE(seq.try_claim_write().has_value());
}

TEST_CASE("MPMCSequencer is safe under concurrent producers and consumers", "[tasks]")
{
    constexpr std::size_t Capacity         = 64;
    constexpr int         Producers        = 4;
    constexpr int         Consumers        = 4;
    constexpr int         ItemsPerProducer = 2000;
    constexpr int         Total            = Producers * ItemsPerProducer;

    tasks::MPMCSequencer<Capacity> seq;
    std::array<int, Capacity>      payload{};  // side channel: races here if seq is broken

    std::atomic<int>      consumed_count{0};
    std::atomic<long long> sum{0};

    auto produce = [&](int producer_id)
    {
        for (int i = 0; i < ItemsPerProducer; ++i)
        {
            const int value = producer_id * 1'000'000 + i;
            decltype(seq.try_claim_write()) t;
            while (!(t = seq.try_claim_write()).has_value())
                std::this_thread::yield();
            payload[t->slot()] = value;
            seq.publish_write(*t);
        }
    };

    auto consume = [&]()
    {
        while (consumed_count.load(std::memory_order_acquire) < Total)
        {
            auto t = seq.try_claim_read();
            if (!t)
            {
                std::this_thread::yield();
                continue;
            }
            sum.fetch_add(payload[t->slot()], std::memory_order_relaxed);
            seq.publish_read(*t);
            consumed_count.fetch_add(1, std::memory_order_release);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(Producers + Consumers);
    for (int i = 0; i < Producers; ++i)
        threads.emplace_back(produce, i);
    for (int i = 0; i < Consumers; ++i)
        threads.emplace_back(consume);
    for (auto& th : threads)
        th.join();

    long long expected = 0;
    for (int p = 0; p < Producers; ++p)
        for (int i = 0; i < ItemsPerProducer; ++i)
            expected += static_cast<long long>(p) * 1'000'000 + i;

    REQUIRE(consumed_count.load() == Total);
    REQUIRE(sum.load() == expected);
}
