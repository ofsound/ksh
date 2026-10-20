#include "KshMidiExport.h"
#include "KshMidiPlayback.h"

namespace ksh
{
juce::Result writePatternMidiFile (const PlaybackSnapshot& state, int source,
                                   const juce::File& file, const juce::String& trackName)
{
    if (source < 0 || source >= Constants::sourceCount)
        return juce::Result::fail ("Invalid pattern.");

    // Use a private runner: exporting never advances the live transport, RNG or cycle counters.
    auto snapshot = std::make_unique<PlaybackSnapshot> (state);
    const auto& settings = snapshot->sourceSettings[static_cast<size_t> (source)];
    snapshot->staticSource = source;
    snapshot->generationMode = GenerationMode::staticSource;
    snapshot->deviceActive = true;
    snapshot->stepCount = juce::jlimit (1, Constants::maxSteps, settings.stepCount);
    snapshot->beatsPerStep = Constants::beatsForRate (settings.rate);
    snapshot->tempo = std::isfinite (snapshot->tempo) ? juce::jlimit (20.0, 300.0, snapshot->tempo) : 120.0;
    snapshot->stepIntervalMs = snapshot->beatsPerStep * 60000.0 / snapshot->tempo;

    constexpr double sampleRate = 48000.0;
    constexpr int ticksPerQuarter = 960;
    constexpr int blockSize = 512;
    const double samplesPerBeat = sampleRate * 60.0 / snapshot->tempo;
    const double lengthBeats = snapshot->stepCount * snapshot->beatsPerStep;
    const double lengthTicks = std::round (lengthBeats * ticksPerQuarter);
    const auto lengthSamples = static_cast<int64_t> (std::llround (lengthBeats * samplesPerBeat));
    const bool hasEarlyCells = snapshot->hasEarlyCells;
    auto runner = std::make_unique<MidiPlaybackRunner>();
    runner->prepare (sampleRate);
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, trackName));
    juce::MidiBuffer buffer;

    for (int64_t start = 0; start < lengthSamples; start += blockSize)
    {
        buffer.clear();
        const int count = static_cast<int> (std::min<int64_t> (blockSize, lengthSamples - start));
        // Once the last step is reached, stop looking ahead into a second pattern pass.
        snapshot->hasEarlyCells = hasEarlyCells
                                 && static_cast<double> (start + count) / samplesPerBeat < lengthBeats - snapshot->beatsPerStep;
        runner->processBlock (*snapshot, static_cast<double> (start) / samplesPerBeat,
                              snapshot->tempo, true, count, buffer);
        for (const auto event : buffer)
        {
            auto message = event.getMessage();
            const double tick = std::round (static_cast<double> (start + event.samplePosition) * ticksPerQuarter / samplesPerBeat);
            message.setTimeStamp (juce::jlimit (0.0, message.isNoteOn() ? lengthTicks - 1.0 : lengthTicks, tick));
            track.addEvent (message);
        }
    }

    buffer.clear();
    runner->releaseHeldNotes (buffer);
    for (const auto event : buffer)
    {
        auto message = event.getMessage();
        message.setTimeStamp (lengthTicks);
        track.addEvent (message);
    }
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp (lengthTicks);
    track.addEvent (end);

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (track);
    juce::FileOutputStream stream (file);
    if (!stream.openedOk())
        return stream.getStatus();
    stream.setPosition (0);
    if (const auto result = stream.truncate(); result.failed())
        return result;
    if (!midi.writeTo (stream, 0))
        return juce::Result::fail ("Could not write the MIDI file.");
    stream.flush();
    return stream.getStatus();
}
}
