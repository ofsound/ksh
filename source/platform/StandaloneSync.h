#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ksh
{
    struct StandaloneLinkStatus
    {
        bool enabled = false;
        int peers = 0;
    };

    struct StandaloneSyncBlock
    {
        juce::Optional<juce::uint64> presentationTimeNs;
        double ppq = 0.0;
        double bpm = 0.0;
        int firstPlaybackSample = 0;
        bool playing = false;
        bool waiting = false;
        bool reset = false;
    };

    // The standalone holder owns the clock. It attaches and detaches it under
    // the processor callback lock. The audio callback captures one block at a time.
    class StandaloneSync
    {
    public:
        virtual ~StandaloneSync() = default;
        virtual StandaloneSyncBlock process (juce::Optional<juce::uint64> hostTimeNs, double sampleRate,
                                             int numSamples, double localTempo, bool localPlaying) = 0;
        virtual bool requestPlaying (bool shouldPlay, bool shareWithPeers) = 0;
        virtual bool requestTempo (double) = 0;
        virtual double sessionTempo() const = 0;
    };
}
