#ifndef TASKS_FUNCTION_H
#define TASKS_FUNCTION_H

// Move-only, allocator-aware, type-erased callable.
//
// Holds a heterogeneous callable (lambda, function pointer, bind result, ...) in storage obtained
// from a user-supplied allocator. The callable is destroyed and deallocated through that same
// allocator when the Function is destroyed.
//
// The interface is small (valid(), operator()); the leverage comes from the allocator threading,
// the move-only semantics, and the fact that callers can store one signature in one slot without
// caring what concrete callable is inside.

#include <memory>
#include <type_traits>
#include <utility>

namespace tasks
{
    template <typename Signature>
    class Function;
}

template <typename R, typename... Args>
class tasks::Function<R(Args...)> final
{
    struct ICallable
    {
        virtual ~ICallable()            = default;
        virtual R    invoke(Args... args) = 0;
        virtual void destroy() noexcept   = 0;
    };

    template <typename F, typename Alloc>
    struct Callable final : ICallable
    {
        F     f;
        Alloc alloc;

        template <typename G>
        Callable(G&& g, const Alloc& a)
        : f(std::forward<G>(g))
        , alloc(a)
        {
        }

        R invoke(Args... args) override
        {
            if constexpr (std::is_void_v<R>)
                f(std::forward<Args>(args)...);
            else
                return f(std::forward<Args>(args)...);
        }

        void destroy() noexcept override
        {
            using Rebound = typename std::allocator_traits<Alloc>::template rebind_alloc<Callable>;
            Rebound a(alloc);
            std::allocator_traits<Rebound>::destroy(a, this);
            std::allocator_traits<Rebound>::deallocate(a, this, 1);
        }
    };

    ICallable* m_callable{nullptr};

public:
    Function() noexcept = default;

    template <typename Alloc, typename F>
    Function(std::allocator_arg_t, const Alloc& alloc, F&& f)
    {
        using Decayed   = std::decay_t<F>;
        using CallableT = Callable<Decayed, Alloc>;
        using Rebound   = typename std::allocator_traits<Alloc>::template rebind_alloc<CallableT>;

        Rebound a(alloc);
        auto*   p = std::allocator_traits<Rebound>::allocate(a, 1);
        try
        {
            std::allocator_traits<Rebound>::construct(a, p, std::forward<F>(f), alloc);
        }
        catch (...)
        {
            std::allocator_traits<Rebound>::deallocate(a, p, 1);
            throw;
        }
        m_callable = p;
    }

    Function(const Function&)            = delete;
    Function& operator=(const Function&) = delete;

    Function(Function&& other) noexcept
    : m_callable(other.m_callable)
    {
        other.m_callable = nullptr;
    }

    Function& operator=(Function&& other) noexcept
    {
        if (this != &other)
        {
            if (m_callable)
                m_callable->destroy();
            m_callable       = other.m_callable;
            other.m_callable = nullptr;
        }
        return *this;
    }

    ~Function()
    {
        if (m_callable)
            m_callable->destroy();
    }

    bool valid() const noexcept { return m_callable != nullptr; }

    R operator()(Args... args)
    {
        return m_callable->invoke(std::forward<Args>(args)...);
    }
};

#endif // TASKS_FUNCTION_H
