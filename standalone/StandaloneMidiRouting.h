#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../source/platform/StandaloneMidiOutput.h"

namespace ksh
{
   #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
    class StandaloneCoreMidiOutput final : public StandaloneMidiOutput
    {
    public:
        static bool isAvailable();
        StandaloneCoreMidiOutput();
        ~StandaloneCoreMidiOutput() override;
        // Open/replace only while detached from the processor.
        juce::String open (const juce::MidiDeviceInfo&, bool virtualSource);
        bool matches (const juce::MidiDeviceInfo&, bool virtualSource) const;
        void prepare (double sampleRate, int maximumBlockSize) override;
        void send (const juce::MidiBuffer&, juce::Optional<juce::uint64> hostTimeNs, int numSamples,
                   juce::Optional<juce::uint64> presentationTimeNs) override;
        void stop() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

   #endif
   #if JUCE_IOS
    juce::MidiDeviceInfo enableStandaloneNetworkMidi();

    // Sends everything published on a local CoreMIDI source to the network session.
    class StandaloneNetworkMidiForwarder
    {
    public:
        StandaloneNetworkMidiForwarder();
        ~StandaloneNetworkMidiForwarder();
        // Returns an empty string on success, otherwise the failing step.
        juce::String start (const juce::MidiDeviceInfo& source);
        void stop();
        bool isActive() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
   #endif
    // The network session is offered as its own route, so hide any JUCE-listed copy.
    inline juce::Array<juce::MidiDeviceInfo> withoutStandaloneNetworkDestination (juce::Array<juce::MidiDeviceInfo> available,
                                                                               const juce::String& networkIdentifier)
    {
        if (networkIdentifier.isNotEmpty())
            available.removeIf ([&] (const juce::MidiDeviceInfo& device) { return device.identifier == networkIdentifier; });
        return available;
    }
    inline bool isStandaloneMidiOutputSource (const juce::MidiDeviceInfo& input, const juce::MidiDeviceInfo& output)
    {
        // CoreMIDI publishes a virtual output as an input source. Never open it
        // for MIDI thru. The name also excludes another KSH instance's output.
        return (output.identifier.isNotEmpty() && input.identifier == output.identifier)
               || (output.name.isNotEmpty() && input.name == output.name);
    }

    /** Message-thread device reconciliation. Preserve manual choices for existing
        external sources, while excluding KSH Output even if a saved setting enabled it. */
    template <typename SetEnabled>
    void updateStandaloneMidiInputs (const juce::Array<juce::MidiDeviceInfo>& available,
                                     juce::Array<juce::MidiDeviceInfo>& previous,
                                     const juce::MidiDeviceInfo& output,
                                     bool autoOpenExternalInputs,
                                     SetEnabled&& setEnabled)
    {
        for (const auto& oldInput : previous)
            if (!available.contains (oldInput))
                setEnabled (oldInput.identifier, false);

        for (const auto& input : available)
        {
            if (isStandaloneMidiOutputSource (input, output))
                setEnabled (input.identifier, false);
            else if (autoOpenExternalInputs && !previous.contains (input))
                setEnabled (input.identifier, true);
        }
        previous = available;
    }
}
