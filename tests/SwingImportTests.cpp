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

    void writeDrumRecording (const juce::File& file, double bpm, int swing, int bars, bool tagTempo)
    {
        constexpr double sampleRate = 12000.0, leadIn = 0.064;
        const auto stepSeconds = 15.0 / bpm;
        juce::AudioBuffer<float> audio (1, juce::roundToInt ((leadIn + 16.0 * bars * stepSeconds) * sampleRate));
        audio.clear();

        const auto strike = [&] (int step, double subdivision, double level, const std::array<double, 2>& frequencies)
        {
            const auto start = juce::roundToInt ((leadIn + (step + subdivision
                + swingDelayStepsForPosition (step, swing, 0)) * stepSeconds) * sampleRate);
            for (int i = 0; i < 330 && start + i < audio.getNumSamples(); ++i)
            {
                const auto t = i / sampleRate;
                const auto angle = juce::MathConstants<double>::twoPi * t;
                const auto wave = (std::cos (frequencies[0] * angle) + 0.5 * std::cos (frequencies[1] * angle)) / 1.5;
                audio.addSample (0, start + i, static_cast<float> (level * wave * std::exp (-t * 115.0)));
            }
        };

        for (int bar = 0; bar < bars; ++bar)
        {
            const auto base = bar * 16;
            for (const auto step : { 0, 8 })
                strike (base + step, 0.0, 0.85, { 65.0, 97.0 });
            for (const auto step : { 4, 12 })
                strike (base + step, 0.0, 0.71, { 270.0, 1150.0 });
            strike (base + 4, 0.0, 0.43, { 3410.0, 5420.0 });
            for (const auto step : { 1, 3, 5, 7, 9, 11, 13, 15 })
                strike (base + step, 0.0, step == 1 ? 0.23 : 0.38, { 3410.0, 5420.0 });
            if (bar > 0)
                strike (base + 9, 0.5, 0.25, { 3410.0, 5420.0 });
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (16);
        if (tagTempo)
            options = options.withMetadataValues ({ { juce::WavAudioFormat::acidTempo, juce::String (bpm) },
                                                     { juce::WavAudioFormat::acidBeats, juce::String (bars * 4) } });
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

TEST_CASE ("A drum recording drop creates a new sequenced project with the source tempo and swing", "[drum-audio-import][native-ui]")
{
    const juce::TemporaryFile file (".wav");
    writeDrumRecording (file.getFile(), 131, 48, 2, false);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 12 }));
    processor.markDocumentClean();
    const auto epoch = processor.getUiBridge().getDocumentEpoch();
    PluginEditor editor (processor);
    const juce::StringArray files { file.getFile().getFullPathName() };
    REQUIRE (editor.isInterestedInFileDrag (files));
    CHECK_FALSE (editor.isInterestedInFileDrag ({ "/path/test.txt" }));
    editor.filesDropped (files, 100, 130);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (processor.getUiBridge().getDocumentEpoch() == epoch && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    const auto current = processor.editorDocument();
    REQUIRE (processor.getUiBridge().getDocumentEpoch() != epoch);
    CHECK (current.settings.swing == Catch::Approx (48).margin (6));
    CHECK (current.settings.swingSubdivisionIndex == 0);
    CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (131).margin (1));
    CHECK (current.settings.channelCount == 3);
    CHECK (current.settings.stepCount == 32);
    CHECK (current.settings.channels[0].note == 36);
    CHECK (current.settings.channels[1].note == 38);
    CHECK (current.settings.channels[2].note == 42);
    CHECK (current.sources[0][0][0].enabled);
    CHECK (current.sources[0][1][4].enabled);
    CHECK (current.sources[0][2][1].enabled);
    CHECK (current.sources[0][2][4].enabled);
    CHECK_FALSE (current.sources[0][2][12].enabled);
    CHECK (current.sources[0][2][1].velocity < current.sources[0][2][3].velocity);
    CHECK (current.sources[0][2][25].roll == 2);
    CHECK (processor.isDocumentModified());
}

TEST_CASE ("Drum transcription fits the first 64 bars across all 16 pattern slots", "[drum-audio-import]")
{
    const juce::TemporaryFile file (".wav");
    writeDrumRecording (file.getFile(), 140, 44, 65, true);
    const auto result = importDrumAudio (file.getFile(), "Acetate.wav", 120.0);
    REQUIRE (result.error.isEmpty());
    CHECK (result.bpm == Catch::Approx (140.0));
    CHECK (result.bars == 64);
    CHECK (result.estimatedTempo);
    const auto json = nlohmann::json::parse (result.projectData);
    CHECK (json.at ("projectName") == "Acetate");
    CHECK (json.at ("stepCount") == 64);
    auto engine = std::make_unique<KickSnareHatEngine>();
    REQUIRE (engine->deserializeForPersistence (json));
    const auto document = engine->documentState();
    CHECK (document.sources[15][0][0].enabled);
    CHECK (document.sources[15][1][60].enabled);
    CHECK (document.sources[15][2][63].enabled);
    CHECK (document.settings.sourceSettings[15].stepCount == 64);
}
