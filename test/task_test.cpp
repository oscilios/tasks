#include "tasks/Allocator.h"
#include "tasks/Queue.h"
#include "tasks/Scheduler.h"
#include "catch.hpp"
#include <new>
#include <cmath>
#include <limits>
#include <future>
#include <memory>
#include <iostream>

// Replace new and delete just for the purpose of demonstrating that
//  they are not called.

std::size_t memory = 0;
std::size_t alloc = 0;

void* operator new(std::size_t s) /*throw() (std::bad_alloc)*/
{
    memory += s;
    ++alloc;
    return malloc(s);
}

void  operator delete(void* p) throw()
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
        return x*y;
    }
};

TEST_CASE("no memory is allocated in packaged_task", "[tasks]")
{
    tasks::memory::Allocator<void, 1024> allocator;
    alloc = 0;
    memory = 0;

    auto p = std::promise< int >( std::allocator_arg, allocator );
    std::future<int> f = p.get_future();
    p.set_value( 123 );
    f.wait();
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);

    auto task = std::packaged_task<int()>(std::allocator_arg, allocator, [](){ return 42;} );
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task();
    REQUIRE(task.get_future().get() == 42);

    task = std::packaged_task<int()>(std::allocator_arg, allocator, max_value<int>);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task();
    REQUIRE(task.get_future().get() == std::numeric_limits<int>::max());

    Foo foo;
    task = std::packaged_task<int()>(std::allocator_arg, allocator, std::bind(&Foo::getValue, foo));
    auto empty_task = std::packaged_task<int()>{};
    empty_task = std::move(task);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    empty_task();
    REQUIRE(empty_task.get_future().get() == std::numeric_limits<int>::max());

    auto task_float = std::packaged_task<float()>(std::allocator_arg, allocator, std::bind(&Foo::multiply, foo, 2.5f, 3.5f));
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);
    task_float();
    REQUIRE(task_float.get_future().get() == 8.75f);

}
TEST_CASE("no memory is allocated in queue", "[tasks]")
{
    using TaskQueue = tasks::threadsafe::Queue<int, 16, 4096>;
    TaskQueue queue;
    alloc = 0;
    memory = 0;

    int value = 0;
    auto future = queue.try_push([&value](){ value = 42; return value;});
    REQUIRE(future.valid());
    queue.try_call_next();

    REQUIRE(value == 42);
    REQUIRE(alloc == 0);
    REQUIRE(memory == 0);

    future = queue.try_push(max_value<int>);
    REQUIRE (future.valid());
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
