#include <RealtimeMailbox.h>
#include <catch2/catch_test_macros.hpp>

#include <thread>

TEST_CASE ("realtime mailbox coalesces bursts and preserves the held snapshot", "[realtime][mailbox]")
{
    RealtimeMailbox<int> mailbox;
    REQUIRE (mailbox.current() == nullptr);
    mailbox.publish (1);
    const auto* held = mailbox.current();
    REQUIRE (*held == 1);

    for (int value = 2; value <= 10000; ++value)
        mailbox.publish (value);

    REQUIRE (*held == 1);
    REQUIRE (*mailbox.current() == 10000);
    mailbox.drainRetired();
    REQUIRE (*mailbox.current() == 10000);

    for (int value = 10001; value <= 10100; ++value)
    {
        mailbox.publish (value);
        REQUIRE (*mailbox.current() == value);
    }
}

TEST_CASE ("realtime mailbox publishes complete snapshots under concurrent consumption", "[realtime][mailbox]")
{
    struct Snapshot
    {
        uint64_t sequence = 0;
        std::array<uint64_t, 64> values {};
    };
    RealtimeMailbox<Snapshot> mailbox;
    std::atomic<bool> finished { false };
    bool valid = true;
    uint64_t latest = 0;
    constexpr uint64_t count = 100000;

    std::thread consumer ([&] {
        bool done = false;
        do
        {
            done = finished.load (std::memory_order_acquire);
            if (const auto* snapshot = mailbox.current())
            {
                if (snapshot->sequence < latest)
                    valid = false;
                latest = snapshot->sequence;
                for (const auto value : snapshot->values)
                    if (value != latest)
                        valid = false;
            }
        }
        while (!done);
    });
    for (uint64_t sequence = 1; sequence <= count; ++sequence)
    {
        Snapshot snapshot;
        snapshot.sequence = sequence;
        snapshot.values.fill (sequence);
        mailbox.publish (snapshot);
    }
    finished.store (true, std::memory_order_release);
    consumer.join();
    REQUIRE (valid);
    REQUIRE (latest == count);
}
