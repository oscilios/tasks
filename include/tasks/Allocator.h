#ifndef TASKS_ALLOCATOR_H
#define TASKS_ALLOCATOR_H

// Based on Howard Hinant's short_alloc: http://howardhinnant.github.io/short_alloc.h
// Modifications were done in order to be thread safe and have a fixed-size memory pool. Class and
// var names have been changed too.

// The MIT License (MIT)
//
// Copyright (c) 2015 Howard Hinnant
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>

namespace tasks
{
    namespace memory
    {
        template <std::size_t N, std::size_t alignment = alignof(std::max_align_t)>
        class MemoryPool;

        template <class T, class PoolT>
        class Allocator;
    }
}

// A pool owns its storage. Anything referencing the pool (Allocators, Queues) must not outlive it.
template <std::size_t N, std::size_t alignment>
class tasks::memory::MemoryPool
{
    alignas(alignment) char m_buf[N];
    std::atomic<char*> m_ptr;

    static std::size_t align_up(std::size_t n) noexcept
    {
        return (n + (alignment - 1)) & ~(alignment - 1);
    }

    bool pointer_in_buffer(const char* const p) noexcept
    {
        return m_buf <= p && p <= m_buf + N;
    }

public:
    template <class T>
    using allocator_type = Allocator<T, MemoryPool>;

    ~MemoryPool()
    {
        m_ptr = nullptr;
    }
    MemoryPool() noexcept
    : m_ptr(m_buf)
    {
    }
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&)      = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    template <std::size_t ReqAlign>
    char* allocate(std::size_t n)
    {
        static_assert(ReqAlign <= alignment, "alignment is too small for this memory pool");
        auto const aligned_n = align_up(n);

        char* old_ptr = m_ptr.load(std::memory_order_relaxed);
        char* new_ptr = nullptr;
        char* r       = nullptr;

        if (static_cast<decltype(aligned_n)>(m_buf + N - old_ptr) >= aligned_n)
        {
            do
            {
                assert(pointer_in_buffer(old_ptr) && "Allocator has outlived memory pool");
                if (static_cast<decltype(aligned_n)>(m_buf + N - old_ptr) >= aligned_n)
                {
                    r       = old_ptr;
                    new_ptr = old_ptr + aligned_n;
                }
                else
                {
                    r = nullptr;
                    break;
                }
            } while (!m_ptr.compare_exchange_weak(
                old_ptr, new_ptr, std::memory_order_release, std::memory_order_relaxed));
        }

        if (!r)
        {
            throw std::bad_alloc{};
        }
        return r;
    }

    void deallocate(char* p, std::size_t n) noexcept
    {
        n = align_up(n);
        assert(pointer_in_buffer(m_ptr) && "Allocator has outlived memory pool");
        assert(pointer_in_buffer(p) && "deallocating a pointer this pool did not allocate");
        char* old_ptr = m_ptr.load(std::memory_order_relaxed);
        char* new_ptr = old_ptr;
        do
        {
            if (p + n == old_ptr)
                new_ptr = p;
        } while (!m_ptr.compare_exchange_weak(
            old_ptr, new_ptr, std::memory_order_release, std::memory_order_relaxed));
    }

    static constexpr std::size_t size() noexcept
    {
        return N;
    }
    std::size_t used() const noexcept
    {
        return static_cast<std::size_t>(m_ptr - m_buf);
    }
    void reset() noexcept
    {
        m_ptr = m_buf;
    }
};

template <class T, class PoolT>
class tasks::memory::Allocator
{
public:
    using value_type       = T;
    using memory_pool_type = PoolT;

private:
    PoolT* m_pool;

    template <class U, class P>
    friend class Allocator;

public:
    explicit Allocator(PoolT& pool) noexcept
    : m_pool(&pool)
    {
    }

    template <class U>
    Allocator(const Allocator<U, PoolT>& other) noexcept
    : m_pool(other.m_pool)
    {
    }

    Allocator(const Allocator&)            = default;
    Allocator(Allocator&&) noexcept        = default;
    Allocator& operator=(const Allocator&) = default;
    Allocator& operator=(Allocator&&) noexcept = default;
    ~Allocator()                           = default;

    template <class U>
    struct rebind
    {
        using other = Allocator<U, PoolT>;
    };

    T* allocate(std::size_t n)
    {
        return reinterpret_cast<T*>(
            m_pool->template allocate<alignof(T)>(n * sizeof(T))); // NOLINT
    }

    void deallocate(T* p, std::size_t n) noexcept
    {
        m_pool->deallocate(reinterpret_cast<char*>(p), n * sizeof(T)); // NOLINT
    }

    bool operator==(const Allocator& other) const noexcept
    {
        return m_pool == other.m_pool;
    }

    bool operator!=(const Allocator& other) const noexcept
    {
        return !(*this == other);
    }
};

#endif // TASKS_ALLOCATOR_H
