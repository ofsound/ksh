#pragma once
#include "engine/KshNativePlayback.h"
#include <atomic>
#include <cstdint>
#include <functional>

/** Native message-thread notification boundary. Engine callbacks only invalidate state;
    they never call editor code while the engine mutex is held. */
class KshUiBridge
{
public:
    void emitNoteHit (const ksh::NativeHit& hit)
    {
        if (onNoteHit)
            onNoteHit (hit);
    }
    void emitCycleVisit (const ksh::NativeCycleVisit& visit)
    {
        if (onCycleVisit)
            onCycleVisit (visit);
    }
    void invalidate() { revision.fetch_add (1, std::memory_order_release); }
    void documentReplaced()
    {
        documentEpoch.fetch_add (1, std::memory_order_release);
        invalidate();
    }
    uint64_t getRevision() const { return revision.load (std::memory_order_acquire); }
    uint64_t getDocumentEpoch() const { return documentEpoch.load (std::memory_order_acquire); }
    std::function<void (const ksh::NativeHit&)> onNoteHit;
    std::function<void (const ksh::NativeCycleVisit&)> onCycleVisit;

private:
    std::atomic<uint64_t> revision { 1 }, documentEpoch { 1 };
};
