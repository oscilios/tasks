#ifndef SYSTEM_LOGGER_ALLOCATOR_H
#define SYSTEM_LOGGER_ALLOCATOR_H

// Based on Howard Hinant's short_alloc: http://howardhinnant.github.io/short_alloc.h
// Modifications were done in order to be thread safe and have static memory pool. Class and var
// names have been changed too.

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
#include <utility>

namespace tasks
{
    namespace memory
    {
        template <std::size_t N, std::size_t alignment = alignof(std::max_align_t)>
        class MemoryPool;
        template <class T, std::size_t N, std::size_t Align = alignof(std::max_align_t)>
        class Allocator;
        template <class T, std::size_t N, std::size_t Align = alignof(std::max_align_t)>
        class AllocatorWithInternalMemory;
    }
}

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
    ~MemoryPool()
    {
        m_ptr = nullptr;
    }
    MemoryPool() noexcept
    : m_ptr(m_buf)
    {
    }
    MemoryPool(const MemoryPool& other)
    : MemoryPool()
    {
        memcpy(m_buf, other.m_buf, sizeof(char) * N);
    }
    MemoryPool(MemoryPool&& other) noexcept
    : MemoryPool()
    {
        std::swap(m_buf, other.m_buf);
    }
    MemoryPool& operator=(const MemoryPool& other)
    {
        memcpy(m_buf, other.m_buf, sizeof(char) * N);
        return *this;
    }
    MemoryPool& operator=(MemoryPool&& other) noexcept
    {
        std::swap(m_buf, other.m_buf);
        return *this;
    }

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
            static_assert(alignment <= alignof(std::max_align_t),
                          "you've chosen an "
                          "alignment that is larger than alignof(std::max_align_t), and "
                          "cannot be guaranteed by normal operator new");
            assert(false && "Memory pool exhausted");
            return static_cast<char*>(::operator new(n));
        }
        return r;
    }

    void deallocate(char* p, std::size_t n) noexcept
    {
        n = align_up(n);
        assert(pointer_in_buffer(m_ptr) && "Allocator has outlived memory pool");
        if (pointer_in_buffer(p))
        {
            char* old_ptr = m_ptr.load(std::memory_order_relaxed);
            char* new_ptr = old_ptr;
            do
            {
                if (p + n == old_ptr)
                    new_ptr = p;
            } while (!m_ptr.compare_exchange_weak(
                old_ptr, new_ptr, std::memory_order_release, std::memory_order_relaxed));
        }
        else
        {
            ::operator delete(p);
        }
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

template <class T, std::size_t N, std::size_t Align>
class tasks::memory::Allocator
{
public:
    using value_type                = T;
    static auto constexpr alignment = Align;
    static auto constexpr size      = N;
    using memory_pool_type          = MemoryPool<size, alignment>;

private:
    memory_pool_type& m_memory;
    static memory_pool_type& getStaticMemory()
    {
        static memory_pool_type memory;
        return memory;
    }

public:
    explicit Allocator() noexcept
    : m_memory(getStaticMemory())
    {
        static_assert(size % alignment == 0, "size N needs to be a multiple of alignment Align");
    }
    template <class U>
    explicit Allocator(const Allocator<U, N, alignment>& a) noexcept
    : m_memory(a.m_memory)
    {
    }
    Allocator(const Allocator&)     = default;
    Allocator(Allocator&&) noexcept = default;
    ~Allocator()                    = default;
    Allocator& operator=(const Allocator& other) = default;
    Allocator& operator=(Allocator&& other) noexcept = default;
    template <class _Up>
    struct rebind
    {
        using other = Allocator<_Up, N, alignment>;
    };

    T* allocate(std::size_t n)
    {
        return reinterpret_cast<T*>(
            m_memory.template allocate<alignof(T)>(n * sizeof(T))); // NOLINT
    }
    void deallocate(T* p, std::size_t n) noexcept
    {
        m_memory.deallocate(reinterpret_cast<char*>(p), n * sizeof(T)); // NOLINT
    }

    template <class T1, std::size_t N1, std::size_t A1, class U, std::size_t M, std::size_t A2>
    friend bool
    operator==(const Allocator<T1, N1, A1>& x, const Allocator<U, M, A2>& y) noexcept; // NOLINT

    template <class U, std::size_t M, std::size_t A>
    friend class Allocator;
};

namespace tasks
{
    namespace memory
    {
        template <class T, std::size_t N, std::size_t A1, class U, std::size_t M, std::size_t A2>
        inline bool operator==(const Allocator<T, N, A1>& x, const Allocator<U, M, A2>& y) noexcept
        {
            return N == M && A1 == A2 && &x.m_memory == &y.m_memory;
        }

        template <class T, std::size_t N, std::size_t A1, class U, std::size_t M, std::size_t A2>
        inline bool operator!=(const Allocator<T, N, A1>& x, const Allocator<U, M, A2>& y) noexcept
        {
            return !(x == y);
        }
    }
}

#endif // SYSTEM_LOGGER_ALLOCATOR_H
