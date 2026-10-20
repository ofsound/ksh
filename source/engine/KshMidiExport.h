#pragma once

#include "KshNativePlayback.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ksh
{
/** Message-thread only. Renders one source-pattern pass with a fresh cycle/random phase.
    Writes a single-track SMF0 in musical time, without changing the destination tempo. */
[[nodiscard]] juce::Result writePatternMidiFile (const PlaybackSnapshot&, int source,
                                                const juce::File&, const juce::String& trackName);
}
