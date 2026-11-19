#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace ksh::ui
{
    struct DrumComputerImport
    {
        std::string projectData;
        juce::String error;
        std::optional<double> bpm;
        int populatedPatterns = 0;
        bool approximateTiming = false;
    };

    DrumComputerImport importDrumComputerPreset (const juce::File&, const juce::String& originalName, double fallbackBpm);
}
