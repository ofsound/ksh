#pragma once

#include <readerwriterqueue.h>

#include <array>
#include <atomic>
#include <cassert>
#include <utility>

/** Single-producer (message thread) -> single-consumer (audio thread) handoff of the latest
    immutable value. Storage is preallocated; the consumer only swaps raw pointers, so it never
    allocates, frees, or blocks on a lock.

    The consumer keeps the most recently published value until a newer one arrives. Superseded
    values are returned to the producer to be recycled via {@link drainRetired}. */
template <typename T>
class RealtimeMailbox
{
public:
    RealtimeMailbox() = default;

    ~RealtimeMailbox()
    {
        T* ptr = nullptr;

        while (toProducer.try_dequeue (ptr))
        {
        }
    }

    RealtimeMailbox (const RealtimeMailbox&) = delete;
    RealtimeMailbox& operator= (const RealtimeMailbox&) = delete;

    /** Producer thread: publish a new value, recycling any update not yet consumed.
        Producer calls (including drainRetired) must be serialized by the caller. */
    void publish (T value)
    {
        drainRetired();

        // At most three slots are held by the consumer, in transit, or pending.
        // The fourth remains available after retired slots have been drained.
        assert (freeSlotCount > 0);
        auto* ptr = freeSlots[--freeSlotCount];
        *ptr = std::move (value);

        if (auto* superseded = pending.exchange (ptr, std::memory_order_acq_rel))
            freeSlots[freeSlotCount++] = superseded;
    }

    /** Producer thread: free any values the consumer has finished with. */
    void drainRetired()
    {
        T* ptr = nullptr;

        while (toProducer.try_dequeue (ptr))
        {
            if (freeSlotCount < freeSlots.size())
                freeSlots[freeSlotCount++] = ptr;
        }
    }

    /** Consumer (audio) thread: most recent published value, or nullptr if nothing published yet. */
    const T* current()
    {
        if (auto* ptr = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (held != nullptr)
            {
                [[maybe_unused]] const bool retired = toProducer.try_enqueue (held);
                assert (retired);
            }

            held = ptr;
        }

        return held;
    }

private:
    static constexpr size_t slotCount = 4;

    static_assert (std::atomic<T*>::is_always_lock_free);
    std::atomic<T*> pending { nullptr };
    moodycamel::ReaderWriterQueue<T*> toProducer { slotCount * 2 };
    std::array<T, slotCount> storage {};
    std::array<T*, slotCount> freeSlots { &storage[0], &storage[1], &storage[2], &storage[3] };
    size_t freeSlotCount = slotCount; // producer-owned
    T* held = nullptr; // consumer-owned
};
