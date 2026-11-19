#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace ksh::ui
{
    struct ImportedSwing
    {
        int amount = 0;
        int target = 0;
    };

    struct SwingImportResult
    {
        std::optional<ImportedSwing> swing;
        std::optional<double> bpm;
        juce::String error;
    };

    enum class SwingFileType { midi, wave };
    std::optional<SwingFileType> swingFileType (const juce::File&);
    SwingImportResult analyseSwingFile (const juce::File&, SwingFileType, double beatsPerStep, double referenceBpm);

    struct AudioDrumImport
    {
        std::string projectData;
        juce::String error;
        double bpm = 120.0;
        int bars = 0;
        int hits = 0;
        bool estimatedTempo = false;
    };

    bool isDrumAudioFile (const juce::File&);
    AudioDrumImport importDrumAudio (const juce::File&, const juce::String& originalName, double referenceBpm);
}
