#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace ksh
{
   #if JUCE_IOS
    void enableStandaloneNetworkMidi();
   #endif
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
