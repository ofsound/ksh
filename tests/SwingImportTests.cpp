#include <PluginEditor.h>
#include <engine/KshMath.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ksh;
using namespace ksh::ui;

namespace
{
    void writeMidi (const juce::File& file, int amount, int target, int stride = 1)
    {
        constexpr int ticksPerBeat = 960;
        juce::MidiMessageSequence chords, percussion;
        chords.addEvent (juce::MidiMessage::tempoMetaEvent (400000));
        for (int step = 0; step < 32; step += stride)
        {
            const auto time = std::round ((step + swingDelayStepsForPosition (step, amount, target)) * ticksPerBeat / 4.0);
            for (const auto pitch : { 36, 60, 63, 67 })
            {
                auto& track = pitch == 36 ? percussion : chords;
                auto on = juce::MidiMessage::noteOn (1, pitch, juce::uint8 { 90 });
                auto off = juce::MidiMessage::noteOff (1, pitch);
                on.setTimeStamp (time);
                off.setTimeStamp (time + 24);
                track.addEvent (on);
                track.addEvent (off);
            }
        }
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ticksPerBeat);
        midi.addTrack (chords);
        midi.addTrack (percussion);
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (midi.writeTo (*stream, 1));
    }

    void writeWave (const juce::File& file, double bpm, int amount, int target, int steps, int stride = 1, bool tagTempo = false)
    {
        constexpr double sampleRate = 22050.0;
        constexpr double leadIn = 0.071;
        const auto stepSeconds = 15.0 / bpm;
        juce::AudioBuffer<float> audio (2, juce::roundToInt ((leadIn + stepSeconds * steps) * sampleRate));
        audio.clear();
        for (int step = 0; step < steps; step += stride)
        {
            const auto time = leadIn + (step + swingDelayStepsForPosition (step, amount, target)) * stepSeconds;
            const auto start = juce::roundToInt (time * sampleRate);
            const auto frequency = step % 4 == 0 ? 120.0 : (step % 2 == 0 ? 260.0 : 1800.0);
            const auto amplitude = step % 4 == 0 ? 0.85 : 0.38;
            for (int i = 0; i < 700 && start + i < audio.getNumSamples(); ++i)
            {
                const auto t = i / sampleRate;
                const auto sample = static_cast<float> (amplitude * std::cos (juce::MathConstants<double>::twoPi * frequency * t) * std::exp (-t * 170.0));
                audio.addSample (step % 2, start + i, sample);
            }
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (16);
        if (tagTempo)
            options = options.withMetadataValues ({ { juce::WavAudioFormat::acidTempo, juce::String (bpm) },
                                                     { juce::WavAudioFormat::acidBeats, juce::String (steps / 4) } });
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    }
}

TEST_CASE ("MIDI swing import fits KSH's grouping in beat space across tracks", "[swing-import]")
{
    const auto target = GENERATE (0, 1, 2);
    const auto amount = GENERATE (0, 46, 73);
    const juce::TemporaryFile file (".MIDI");
    writeMidi (file.getFile(), amount, target);
    REQUIRE (swingFileType (file.getFile()) == SwingFileType::midi);
    const auto result = analyseSwingFile (file.getFile(), SwingFileType::midi, 0.25, 120);
    CHECK (result.error.isEmpty());
    CHECK_FALSE (result.bpm.has_value());
    REQUIRE (result.swing.has_value());
    CHECK (result.swing->amount == Catch::Approx (amount).margin (1));
    if (amount > 0)
        CHECK (result.swing->target == target);
}

TEST_CASE ("WAV groove import finds tempo and swing from stereo hits after a lead-in", "[swing-import]")
{
    const auto bpm = GENERATE (91.0, 137.5);
    const auto target = GENERATE (0, 1, 2);
    const auto amount = target == 2 ? 34 : 56;
    CAPTURE (bpm, target);
    const juce::TemporaryFile file (".wave");
    writeWave (file.getFile(), bpm, amount, target, 32);
    REQUIRE (swingFileType (file.getFile()) == SwingFileType::wave);
    const auto result = analyseSwingFile (file.getFile(), SwingFileType::wave, 0.25, 120);
    CHECK (result.error.isEmpty());
    REQUIRE (result.bpm.has_value());
    CHECK (*result.bpm == Catch::Approx (bpm).margin (1.0));
    REQUIRE (result.swing.has_value());
    CHECK (result.swing->target == target);
    CHECK (result.swing->amount == Catch::Approx (amount).margin (6));
}

TEST_CASE ("WAV tempo stays tied to the file when the project grid is coarser", "[swing-import]")
{
    const juce::TemporaryFile file (".wav");
    writeWave (file.getFile(), 127, 60, 0, 16);
    const auto result = analyseSwingFile (file.getFile(), SwingFileType::wave, 0.5, 120);
    REQUIRE (result.bpm.has_value());
    CHECK (*result.bpm == Catch::Approx (127).margin (1));
    REQUIRE (result.swing.has_value());
    CHECK (result.swing->target == 0);
    CHECK (result.swing->amount == Catch::Approx (60).margin (6));
}

TEST_CASE ("Overlapping WAV attacks can leave swing ambiguous even with a reliable tempo", "[swing-import]")
{
    const juce::TemporaryFile file (".wav");
    writeWave (file.getFile(), 137.5, 56, 2, 32);
    const auto result = analyseSwingFile (file.getFile(), SwingFileType::wave, 0.25, 120);
    REQUIRE (result.bpm.has_value());
    CHECK (*result.bpm == Catch::Approx (137.5).margin (1));
    CHECK_FALSE (result.swing.has_value());
}

TEST_CASE ("Quarter-note-only files do not provide swing evidence", "[swing-import]")
{
    const juce::TemporaryFile midi (".mid");
    writeMidi (midi.getFile(), 0, 0, 4);
    const auto midiResult = analyseSwingFile (midi.getFile(), SwingFileType::midi, 0.25, 120);
    CHECK_FALSE (midiResult.swing.has_value());

    const juce::TemporaryFile wave (".wav");
    writeWave (wave.getFile(), 113, 0, 0, 32, 4);
    const auto waveResult = analyseSwingFile (wave.getFile(), SwingFileType::wave, 0.25, 120);
    CHECK_FALSE (waveResult.swing.has_value());
    REQUIRE (waveResult.bpm.has_value());
    CHECK (*waveResult.bpm == Catch::Approx (113).margin (0.5));
}

TEST_CASE ("WAV loop metadata gives a tempo for a clip with too few attacks", "[swing-import]")
{
    const juce::TemporaryFile file (".wav");
    writeWave (file.getFile(), 177.5, 0, 0, 16, 16, true);
    const auto result = analyseSwingFile (file.getFile(), SwingFileType::wave, 0.25, 120);
    REQUIRE (result.bpm.has_value());
    CHECK (*result.bpm == Catch::Approx (177.5));
    CHECK_FALSE (result.swing.has_value());
}

TEST_CASE ("A WAV drop changes standalone tempo and swing in a single undoable edit", "[swing-import][native-ui]")
{
    const juce::TemporaryFile file (".wav");
    writeWave (file.getFile(), 131, 68, 1, 32);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 12 }));
    const auto initial = processor.editorDocument();
    PluginEditor editor (processor);
    const juce::StringArray files { file.getFile().getFullPathName() };
    REQUIRE (editor.isInterestedInFileDrag (files));
    CHECK_FALSE (editor.isInterestedInFileDrag ({ "/path/test.txt" }));
    editor.filesDropped (files, 100, 130);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (processor.enginePlaybackSnapshot().swing == 12 && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    const auto current = processor.editorDocument();
    CHECK (current.settings.swing == Catch::Approx (68).margin (6));
    CHECK (current.settings.swingSubdivisionIndex == 1);
    CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (131).margin (1));
    CHECK (current.sources == initial.sources);
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    REQUIRE (view->controller.canUndo());
    view->controller.undo();
    CHECK (processor.editorDocument() == initial);
    view->controller.redo();
    CHECK (processor.editorDocument() == current);
}
