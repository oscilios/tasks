#ifndef TASKS_MPMC_SEQUENCER_H
#define TASKS_MPMC_SEQUENCER_H

// Vyukov-style bounded MPMC sequencer.
//
// Coordinates producers and consumers around a fixed-size ring without locks. The sequencer hands
// out tickets pointing at slot indices; callers do their own access to the indexed slot, then
// publish. Invariants:
//   - try_claim_write returns nullopt when the ring is full
//   - try_claim_read  returns nullopt when the ring is empty
//   - publish_write(t) happens-before the matching try_claim_read sees slot t.slot()
//   - publish_read(t)  happens-before the next producer's claim of slot t.slot() sees it
//
// Capacity must be a power of two.
//
// Reference: D. Vyukov, "Bounded MPMC queue".
// https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace tasks
{
    template <std::size_t Capacity>
    class MPMCSequencer;
}

template <std::size_t Capacity>
class tasks::MPMCSequencer
{
    static_assert((Capacity > 0) && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    struct Ticket
    {
        std::size_t pos;
        constexpr std::size_t slot() const noexcept { return pos & (Capacity - 1); }
    };

private:
    struct alignas(64) Cell
    {
        std::atomic<std::size_t> seq;
    };

    Cell m_cells[Capacity];
    alignas(64) std::atomic<std::size_t> m_enqueue_pos{0};
    alignas(64) std::atomic<std::size_t> m_dequeue_pos{0};

public:
    MPMCSequencer() noexcept
    {
        for (std::size_t i = 0; i < Capacity; ++i)
            m_cells[i].seq.store(i, std::memory_order_relaxed);
    }

    MPMCSequencer(const MPMCSequencer&)            = delete;
    MPMCSequencer(MPMCSequencer&&)                 = delete;
    MPMCSequencer& operator=(const MPMCSequencer&) = delete;
    MPMCSequencer& operator=(MPMCSequencer&&)      = delete;

    std::optional<Ticket> try_claim_write() noexcept
    {
        std::size_t pos = m_enqueue_pos.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell&             cell = m_cells[pos & (Capacity - 1)];
            const std::size_t seq  = cell.seq.load(std::memory_order_acquire);
            const auto        diff =
                static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
            if (diff == 0)
            {
                if (m_enqueue_pos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    return Ticket{pos};
                }
            }
            else if (diff < 0)
            {
                return std::nullopt;
            }
            else
            {
                pos = m_enqueue_pos.load(std::memory_order_relaxed);
            }
        }
    }

    void publish_write(Ticket t) noexcept
    {
        m_cells[t.pos & (Capacity - 1)].seq.store(t.pos + 1, std::memory_order_release);
    }

    std::optional<Ticket> try_claim_read() noexcept
    {
        std::size_t pos = m_dequeue_pos.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell&             cell = m_cells[pos & (Capacity - 1)];
            const std::size_t seq  = cell.seq.load(std::memory_order_acquire);
            const auto        diff =
                static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0)
            {
                if (m_dequeue_pos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    return Ticket{pos};
                }
            }
            else if (diff < 0)
            {
                return std::nullopt;
            }
            else
            {
                pos = m_dequeue_pos.load(std::memory_order_relaxed);
            }
        }
    }

    void publish_read(Ticket t) noexcept
    {
        m_cells[t.pos & (Capacity - 1)].seq.store(t.pos + Capacity, std::memory_order_release);
    }
};

#endif // TASKS_MPMC_SEQUENCER_H
