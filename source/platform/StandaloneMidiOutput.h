#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ksh
{
    // The standalone holder owns this sink. Replacement and destruction are
    // serialised with AudioProcessorPlayer using the processor callback lock.
    class StandaloneMidiOutput
    {
    public:
        virtual ~StandaloneMidiOutput() = default;
        virtual void prepare (double sampleRate, int maximumBlockSize) = 0;
        virtual void send (const juce::MidiBuffer&, juce::Optional<juce::uint64> hostTimeNs, int numSamples,
                           juce::Optional<juce::uint64> presentationTimeNs) = 0;
        virtual void stop() = 0;
    };
}
