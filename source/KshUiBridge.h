#pragma once
#include "engine/KickSnareHatEngine.h"
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>

class PluginProcessor;

/** Native message-thread notification boundary. Engine callbacks only invalidate state;
    they never call editor code while the engine mutex is held. */
class KshUiBridge
{
public:
    explicit KshUiBridge (PluginProcessor& p) : processor (p) {}
    [[nodiscard]] bool handleCommand (const juce::String& commandJson);
    void syncAll() { invalidate(); }
    void requestState() { invalidate(); }
    void emitEngineState() { invalidate(); }
    void emitPreview (const nlohmann::json&) { invalidate(); }
    void emitStatus (const std::string&) { invalidate(); }
    void emitNoteHit (const ksh::NativeHit& hit)
    {
        if (onNoteHit)
            onNoteHit (hit);
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

private:
    PluginProcessor& processor;
    std::atomic<uint64_t> revision { 1 }, documentEpoch { 1 };
};
