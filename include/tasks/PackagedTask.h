#ifndef TASKS_PACKAGED_TASK_H
#define TASKS_PACKAGED_TASK_H

// PackagedTask = Function + std::promise + exception capture.
//
// Allocator-aware variant of std::packaged_task: the callable's storage and the promise's shared
// state both come from the user-supplied allocator. Calling operator() runs the callable, captures
// the result into the promise (or stores any exception thrown), and is one-shot.

#include "Function.h"
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
    Function<R(Args...)>           m_callable;
    std::optional<std::promise<R>> m_promise;

public:
    PackagedTask() noexcept = default;

    template <typename Alloc, typename F>
    PackagedTask(std::allocator_arg_t, const Alloc& alloc, F&& f)
    : m_callable(std::allocator_arg, alloc, std::forward<F>(f))
    {
        m_promise.emplace(std::allocator_arg, alloc);
    }

    PackagedTask(const PackagedTask&)            = delete;
    PackagedTask& operator=(const PackagedTask&) = delete;

    PackagedTask(PackagedTask&& other) noexcept
    : m_callable(std::move(other.m_callable))
    , m_promise(std::move(other.m_promise))
    {
        other.m_promise.reset();
    }

    PackagedTask& operator=(PackagedTask&& other) noexcept
    {
        if (this != &other)
        {
            m_callable = std::move(other.m_callable);
            m_promise  = std::move(other.m_promise);
            other.m_promise.reset();
        }
        return *this;
    }

    ~PackagedTask() = default;

    bool valid() const noexcept { return m_callable.valid(); }

    std::future<R> get_future() { return m_promise->get_future(); }

    void operator()(Args... args)
    {
        try
        {
            if constexpr (std::is_void_v<R>)
            {
                m_callable(std::forward<Args>(args)...);
                m_promise->set_value();
            }
            else
            {
                m_promise->set_value(m_callable(std::forward<Args>(args)...));
            }
        }
        catch (...)
        {
            m_promise->set_exception(std::current_exception());
        }
    }
};

#endif // TASKS_PACKAGED_TASK_H
