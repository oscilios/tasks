#ifndef TASKS_PACKAGED_TASK_H
#define TASKS_PACKAGED_TASK_H

#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace tasks
{
    template <typename Signature>
    class PackagedTask;
}

template <typename R, typename... Args>
class tasks::PackagedTask<R(Args...)> final
{
    struct ICallable
    {
        virtual ~ICallable()              = default;
        virtual R invoke(Args... args)    = 0;
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

    std::optional<std::promise<R>> m_promise;
    ICallable*                     m_callable{nullptr};

public:
    PackagedTask() noexcept = default;

    template <typename Alloc, typename F>
    PackagedTask(std::allocator_arg_t, const Alloc& alloc, F&& f)
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
        m_promise.emplace(std::allocator_arg, alloc);
    }

    PackagedTask(const PackagedTask&) = delete;
    PackagedTask& operator=(const PackagedTask&) = delete;

    PackagedTask(PackagedTask&& other) noexcept
    : m_promise(std::move(other.m_promise))
    , m_callable(other.m_callable)
    {
        other.m_callable = nullptr;
        other.m_promise.reset();
    }

    PackagedTask& operator=(PackagedTask&& other) noexcept
    {
        if (this != &other)
        {
            if (m_callable)
                m_callable->destroy();
            m_promise        = std::move(other.m_promise);
            m_callable       = other.m_callable;
            other.m_callable = nullptr;
            other.m_promise.reset();
        }
        return *this;
    }

    ~PackagedTask()
    {
        if (m_callable)
            m_callable->destroy();
    }

    bool valid() const noexcept
    {
        return m_callable != nullptr;
    }

    std::future<R> get_future()
    {
        return m_promise->get_future();
    }

    void operator()(Args... args)
    {
        try
        {
            if constexpr (std::is_void_v<R>)
            {
                m_callable->invoke(std::forward<Args>(args)...);
                m_promise->set_value();
            }
            else
            {
                m_promise->set_value(m_callable->invoke(std::forward<Args>(args)...));
            }
        }
        catch (...)
        {
            m_promise->set_exception(std::current_exception());
        }
    }
};

#endif // TASKS_PACKAGED_TASK_H
