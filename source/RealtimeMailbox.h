#pragma once

#include <readerwriterqueue.h>

#include <utility>

/** Single-producer (message thread) -> single-consumer (audio thread) handoff of the latest
    immutable value. Heap allocation and destruction stay on the producer thread; the consumer
    only swaps raw pointers, so it never allocates, frees, or blocks on a lock.

    The consumer keeps the most recently published value until a newer one arrives. Superseded
    values are returned to the producer to be freed via {@link drainRetired}. */
template <typename T>
class RealtimeMailbox
{
public:
    RealtimeMailbox() = default;

    ~RealtimeMailbox()
    {
        T* ptr = nullptr;

        while (toConsumer.try_dequeue (ptr))
            delete ptr;

        while (toProducer.try_dequeue (ptr))
            delete ptr;

        delete held;
    }

    RealtimeMailbox (const RealtimeMailbox&) = delete;
    RealtimeMailbox& operator= (const RealtimeMailbox&) = delete;

    /** Producer thread: publish a new value (copied to the heap). */
    void publish (T value)
    {
        drainRetired();

        auto* ptr = new T (std::move (value));

        if (! toConsumer.try_enqueue (ptr))
            delete ptr; // queue full: drop this update; the consumer keeps the previous value
    }

    /** Producer thread: free any values the consumer has finished with. */
    void drainRetired()
    {
        T* ptr = nullptr;

        while (toProducer.try_dequeue (ptr))
            delete ptr;
    }

    /** Consumer (audio) thread: most recent published value, or nullptr if nothing published yet. */
    const T* current()
    {
        T* ptr = nullptr;

        while (toConsumer.try_dequeue (ptr))
        {
            if (held != nullptr && ! toProducer.try_enqueue (held))
                delete held; // retire queue full (pathological): fall back to freeing here

            held = ptr;
        }

        return held;
    }

private:
    moodycamel::ReaderWriterQueue<T*> toConsumer { 512 };
    moodycamel::ReaderWriterQueue<T*> toProducer { 512 };
    T* held = nullptr; // consumer-owned
};
