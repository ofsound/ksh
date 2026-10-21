#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KshUiBridge.h"

#include "engine/KshPersistence.h"
#include "engine/KshEngineCommands.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 5> kMacroParameterIDs {{
    "rate",
    "swing",
    "velocity_humanize",
    "timing_humanize",
    "device_active"
}};

enum MacroParameterBit : uint32_t
{
    rateBit = 1u << 0,
    swingBit = 1u << 1,
    velocityHumanizeBit = 1u << 2,
    timingHumanizeBit = 1u << 3,
    deviceActiveBit = 1u << 4
};

bool isMacroParameterID (std::string_view id)
{
    for (const auto macroID : kMacroParameterIDs)
    {
        if (macroID == id)
            return true;
    }

    return false;
}

// Called from the audio thread for host automation: compares without allocating.
uint32_t macroParameterBit (const juce::String& id)
{
    for (size_t i = 0; i < kMacroParameterIDs.size(); ++i)
    {
        if (id == kMacroParameterIDs[i].data())
            return 1u << i;
    }

    return 0;
}

constexpr double kHostStateChangeIntervalMs = 250.0;
// Tempo ramps report a new BPM every block; each apply republishes the snapshot and resyncs the UI.
constexpr double kHostTempoApplyIntervalMs = 100.0;
constexpr int kMidiOutputReserveBytes = 32768;

bool isNoteOnBytes (const juce::uint8* data, int numBytes)
{
    return numBytes >= 3 && (data[0] & 0xf0) == 0x90 && data[2] != 0;
}

bool isNoteOffBytes (const juce::uint8* data, int numBytes)
{
    return numBytes >= 3 && ((data[0] & 0xf0) == 0x80 || ((data[0] & 0xf0) == 0x90 && data[2] == 0));
}

juce::String juceStringFromView (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

int rateIndexForValue (std::string_view rate)
{
    const auto normalized = ksh::Constants::normalizeRate (rate);

    for (size_t i = 0; i < ksh::Constants::rates.size(); ++i)
    {
        if (ksh::Constants::rates[i] == normalized)
            return static_cast<int> (i);
    }

    return 4; // 16n
}

bool isMidiPatternSelectionNoteNumber (int noteNumber)
{
    // P1-P16 use notes 0-15; the sequentially following note 16 selects M.
    return noteNumber >= 0 && noteNumber <= ksh::Constants::sourceCount;
}

int sourceForMidiPatternSelectionNoteNumber (int noteNumber)
{
    return noteNumber == ksh::Constants::sourceCount
               ? ksh::Constants::mutedStaticSource
               : noteNumber;
}

constexpr double kMaxPatternRecordTransportAgeMs = 250.0;

double recordingBeat (double beat, double offsetBeats, ksh::HostLoopRange loop)
{
    double adjusted = beat + offsetBeats;
    if (loop.active && loop.endPpq > loop.startPpq && beat >= loop.startPpq
        && (adjusted < loop.startPpq || adjusted >= loop.endPpq))
    {
        const double length = loop.endPpq - loop.startPpq;
        adjusted = loop.startPpq + std::fmod (std::fmod (adjusted - loop.startPpq, length) + length, length);
    }
    return adjusted;
}

juce::StringArray rateChoices()
{
    juce::StringArray choices;

    for (const auto rate : ksh::Constants::rates)
        choices.add (juceStringFromView (rate));

    return choices;
}

float parameterValue (juce::AudioProcessorValueTreeState& parameters, const juce::String& id)
{
    if (const auto* value = parameters.getRawParameterValue (id))
        return value->load (std::memory_order_relaxed);

    return 0.0f;
}

void setParameterValue (juce::AudioProcessorValueTreeState& parameters,
                        const juce::String& id,
                        float plainValue,
                        bool notifyHost,
                        bool insideGesture = false)
{
    auto* parameter = parameters.getParameter (id);

    if (parameter == nullptr)
        return;

    const auto normalized = parameter->convertTo0to1 (plainValue);

    if (std::abs (parameter->getValue() - normalized) < 0.000001f)
        return;

    if (notifyHost)
    {
        if (! insideGesture)
            parameter->beginChangeGesture();

        parameter->setValueNotifyingHost (normalized);

        if (! insideGesture)
            parameter->endChangeGesture();

        return;
    }

    parameter->setValue (normalized);
}

double clampStandaloneTempoBpm (double bpm)
{
    if (std::isnan (bpm))
        bpm = 120.0;

    return std::clamp (bpm, 20.0, 300.0);
}

nlohmann::json makeDefaultPersistenceState()
{
    ksh::KickSnareHatEngine defaults;
    defaults.setGenerationMode (ksh::GenerationMode::staticSource);
    defaults.setStaticSource (0);
    defaults.setStepCount (16);
    defaults.setChannelCount (ksh::Constants::defaultChannelCount);
    defaults.setRate ("16n");
    defaults.setTempo (120.0);
    return defaults.serializeForPersistence();
}
} // namespace

PluginProcessor::BusesProperties PluginProcessor::createBusesProperties()
{
  #if JucePlugin_IsMidiEffect
    return BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true);
  #else
    return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
  #endif
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using ParameterID = juce::ParameterID;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { "rate", 1 }, "Rate", rateChoices(), rateIndexForValue (ksh::Constants::defaultRate)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { "swing", 1 }, "Swing", juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { "velocity_humanize", 1 },
        "Velocity Humanize",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { "timing_humanize", 1 },
        "Timing Humanize",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParameterID { "device_active", 1 }, "Device Active", true));

    return { params.begin(), params.end() };
}

void PluginProcessor::initializeDefaultPattern()
{
    engine.setGenerationMode (ksh::GenerationMode::staticSource);
    engine.setStaticSource (0);
    engine.setStepCount (16);
    engine.setChannelCount (ksh::Constants::defaultChannelCount);
    engine.setRate ("16n");
    engine.setTempo (120.0);
}

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (createBusesProperties()),
       parameters (*this, nullptr, "KSH_PARAMETERS", createParameterLayout()),
       engine (makeEngineCallbacks())
{
    initializeDefaultPattern();
    syncMacroParametersFromEngineLocked (false);
    addMacroParameterListeners();
    publishPlaybackSnapshot();
    startTimerHz (120);
}

void PluginProcessor::publishPlaybackSnapshot()
{
    std::scoped_lock lock { engineStateMutex };
    publishPlaybackSnapshotLocked();
}

void PluginProcessor::refreshAuditionCacheFromSnapshot (const ksh::PlaybackSnapshot& snapshot)
{
    auditionChannelCount.store (snapshot.channelCount, std::memory_order_relaxed);
    auditionDeviceActive.store (snapshot.deviceActive ? 1 : 0, std::memory_order_relaxed);
    patternRecordGrids.publishWith ([&snapshot] (auto& grids)
    {
        for (int source = 0; source < ksh::Constants::sourceCount; ++source)
            grids[static_cast<size_t> (source)] = ksh::patternRecordingGrid (snapshot, source);
    });

    for (int channel = 0; channel < ksh::Constants::maxChannels; ++channel)
    {
        const int note = channel < snapshot.channelCount
                             ? snapshot.channels[static_cast<size_t> (channel)].note
                             : ksh::Constants::defaultChannelNotes[static_cast<size_t> (channel)];
        auditionChannelNotes[static_cast<size_t> (channel)].store (note, std::memory_order_relaxed);
    }
}

void PluginProcessor::publishPlaybackSnapshotLocked()
{
    playbackMailbox.publishWith ([this] (ksh::PlaybackSnapshot& snapshot)
                                 {
                                     engine.fillPlaybackSnapshot (snapshot);
                                     refreshAuditionCacheFromSnapshot (snapshot);
                                 });
    lastPublishedSnapshotVersion = engine.playbackSnapshotVersion();
}

void PluginProcessor::applyPendingMacroParametersLocked()
{
    if (const auto parameterMask = dirtyMacroParameters.exchange (0, std::memory_order_acquire); parameterMask != 0)
        applyMacroParametersToEngineLocked (parameterMask);
}

void PluginProcessor::adoptGeneratedSourcesLocked()
{
    ksh::GeneratedSourcesFrame frame;
    bool received = false;

    while (generatedSourcesForEngine.try_dequeue (frame))
        received = true;

    if (received && engine.adoptGeneratedSources (frame))
        uiBridge.invalidate();
}

void PluginProcessor::markDocumentModified()
{
    documentModified.store (true, std::memory_order_release);
    hostStateChangePending.store (true, std::memory_order_release);
    messageThreadWorkPending.store (true, std::memory_order_release);
}

void PluginProcessor::notifyHostOfStateChangeIfDue()
{
    if (! hostStateChangePending.load (std::memory_order_acquire))
        return;

    // Hosts may do real work per notification; coalesce drags into a few per second.
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    if (nowMs - lastHostStateChangeNotifyMs < kHostStateChangeIntervalMs)
    {
        messageThreadWorkPending.store (true, std::memory_order_release);
        return;
    }

    hostStateChangePending.store (false, std::memory_order_release);
    lastHostStateChangeNotifyMs = nowMs;
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PluginProcessor::publishPlaybackSnapshotIfChangedLocked()
{
    if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
        publishPlaybackSnapshotLocked();
}

ksh::EngineCallbacks PluginProcessor::makeEngineCallbacks()
{
    ksh::EngineCallbacks callbacks;

    // The native editor re-reads typed state on each revision, so status text and preview JSON are never built.
    callbacks.changed = [this]
    {
        if (suppressEngineCallbacks.load (std::memory_order_relaxed))
            return;

        uiBridge.invalidate();
    };

    callbacks.emitNote = [this] (const ksh::MidiNoteEvent& note)
    {
        if (suppressEngineCallbacks.load (std::memory_order_relaxed))
            return;

        midiPlayback.queueAuditionNote (note);
    };

    return callbacks;
}

int PluginProcessor::emitPendingNoteHitsForUi()
{
    int emitted = 0;
    ksh::NativeHit hit;

    while (noteHitsForUi.try_dequeue (hit))
    {
        uiBridge.emitNoteHit (hit);
        ++emitted;
    }

    return emitted;
}

void PluginProcessor::runMessageThreadWork()
{
    // Hits first so flashes are never delayed behind engine work. Generation runs on the
    // audio thread, so nothing here sits between a refresh boundary and its first note.
    emitPendingNoteHitsForUi();

    {
        std::scoped_lock lock { engineStateMutex };

        applyPendingMacroParametersLocked();
        drainPendingMidiPatternSelectionsLocked();
        drainPendingPatternRecordEventsLocked();
        adoptGeneratedSourcesLocked();
        applyPendingHostTempoLocked();
        publishPlaybackSnapshotIfChangedLocked();

        if (engine.isPreviewDirty())
            engine.flushPreview();

        // Keep all mailbox producer operations serialized with host state restore.
        playbackMailbox.drainRetired();
    }

    emitPendingNoteHitsForUi();
    notifyHostOfStateChangeIfDue();
}

void PluginProcessor::applyPendingHostTempoLocked()
{
    if (! hostBpmChangePending.load (std::memory_order_acquire))
        return;

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    if (nowMs - lastHostTempoApplyMs < kHostTempoApplyIntervalMs)
    {
        messageThreadWorkPending.store (true, std::memory_order_release);
        return;
    }

    hostBpmChangePending.store (false, std::memory_order_release);
    const auto bpm = clampStandaloneTempoBpm (pendingHostBpm.load (std::memory_order_relaxed));

    if (std::abs (engine.getTempo() - bpm) > 0.01)
    {
        engine.setTempo (bpm);
        lastHostTempoApplyMs = nowMs;
    }
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    removeMacroParameterListeners();
}

void PluginProcessor::timerCallback()
{
    if (messageThreadWorkPending.exchange (false, std::memory_order_acquire))
        runMessageThreadWork();
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    recordingSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    midiPlayback.prepare (recordingSampleRate);
    recordingClockHistory.fill ({});
    recordingClockHistoryIndex = 0;
    recordingClockInitialized = false;
    patternRecordTransport.publish ({});

    for (size_t i = 0; i < midiOutputBuffers.size(); ++i)
    {
        midiOutputBuffers[i].clear();
        midiOutputBuffers[i].ensureSize (kMidiOutputReserveBytes);
        reservedMidiStorage[i] = midiOutputBuffers[i].data.begin();
    }

    publishPlaybackSnapshot();
}

void PluginProcessor::releaseResources()
{
    midiPlayback.reset();
}

bool PluginProcessor::hasStandaloneTransport() const
{
    return wrapperType == wrapperType_Standalone;
}

void PluginProcessor::setStandaloneTransportPlaying (bool shouldPlay)
{
    if (! hasStandaloneTransport())
        return;

    if (shouldPlay)
    {
        if (standaloneTransportPlaying.load (std::memory_order_relaxed) != 0)
            return;

        standaloneTransportPpqPosition.store (0.0, std::memory_order_relaxed);
        standaloneTransportResetRequested.store (1, std::memory_order_release);
        currentStepForUi.store (0, std::memory_order_relaxed);
        standaloneTransportPlaying.store (1, std::memory_order_release);
    }
    else
    {
        const int wasPlaying = standaloneTransportPlaying.exchange (0, std::memory_order_relaxed);

        if (wasPlaying != 0)
            standaloneStopAllNotesRequested.store (1, std::memory_order_release);

        currentStepForUi.store (0, std::memory_order_relaxed);
    }

    requestMessageThreadWork();
}

bool PluginProcessor::isStandaloneTransportPlaying() const
{
    return standaloneTransportPlaying.load (std::memory_order_relaxed) != 0;
}

void PluginProcessor::setStandaloneTempoBpm (double bpm)
{
    if (! hasStandaloneTransport())
        return;

    {
        std::scoped_lock lock { engineStateMutex };

        bpm = clampStandaloneTempoBpm (bpm);

        if (std::abs (engine.getTempo() - bpm) > 0.01)
        {
            engine.setTempo (bpm);
            publishPlaybackSnapshotIfChangedLocked();
            markDocumentModified();
        }
    }
}

double PluginProcessor::getStandaloneTempoBpm()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    return clampStandaloneTempoBpm (engine.getTempo());
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in.isDisabled())
        return true;

    return in == out;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();

    // Read-only immutable view built on the message thread. No engine access on the audio thread.
    const auto* snapshot = playbackMailbox.current();

    if (snapshot == nullptr)
        return;

    const bool playbackReset = playbackResetRequested.exchange (false, std::memory_order_acquire);
    const bool standaloneReset = standaloneTransportResetRequested.exchange (0, std::memory_order_acquire) != 0;
    const bool stopAllNotes = standaloneStopAllNotesRequested.exchange (0, std::memory_order_acquire) != 0;

    double ppqPosition = 0.0;
    double bpm = snapshot->tempo;
    bool isPlaying = false;
    ksh::HostLoopRange hostLoop;

    if (hasStandaloneTransport())
    {
        ppqPosition = standaloneTransportPpqPosition.load (std::memory_order_relaxed);
        bpm = clampStandaloneTempoBpm (snapshot->tempo);
        isPlaying = isStandaloneTransportPlaying();
    }
    else if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto ppq = position->getPpqPosition())
                ppqPosition = *ppq;

            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            isPlaying = position->getIsPlaying();

            if (position->getIsLooping())
            {
                if (const auto loopPoints = position->getLoopPoints())
                    hostLoop = { true, loopPoints->ppqStart, loopPoints->ppqEnd };
            }
        }
    }

    double expectedRecordingPpq = recordingExpectedNextPpq;
    if (hostLoop.active && hostLoop.endPpq > hostLoop.startPpq && expectedRecordingPpq >= hostLoop.endPpq)
        expectedRecordingPpq = recordingBeat (expectedRecordingPpq, 0.0, hostLoop);
    const double clockTolerance = 64.0 * (bpm / 60.0) / recordingSampleRate;
    if (!recordingClockInitialized || playbackReset || standaloneReset || isPlaying != recordingClockWasPlaying
        || (isPlaying && std::abs (ppqPosition - expectedRecordingPpq) > clockTolerance))
    {
        ++recordingTransportGeneration;
        midiPlayback.clearRecordingMonitors();
    }
    recordingClockInitialized = true;
    recordingClockWasPlaying = isPlaying;
    recordingExpectedNextPpq = ppqPosition + numSamples * (bpm / 60.0) / recordingSampleRate;
    if (standaloneReset || playbackReset)
        recordingClockHistory.fill ({});
    consumeStandaloneRecording (*snapshot);
    publishPatternRecordTransportPosition (ppqPosition, bpm, isPlaying, hostLoop);

    // Consume input before adding generated MIDI so our note-offs are never mistaken for pattern selections.
    ksh::MidiPatternSelectionBlock midiPatternSelections;
    const bool filterSelectionNotes = consumeMidiInput (midiMessages, *snapshot, ppqPosition, bpm, isPlaying, midiPatternSelections, hostLoop);

    auto& midiOut = reservedMidiOutputBuffer();
    midiOut.clear();

    if (! filterSelectionNotes)
    {
        midiOut.data.addArray (midiMessages.data);
    }
    else
    {
        for (const auto metadata : midiMessages)
        {
            if ((isNoteOnBytes (metadata.data, metadata.numBytes) || isNoteOffBytes (metadata.data, metadata.numBytes))
                && isMidiPatternSelectionNoteNumber (metadata.data[1]))
                continue;

            midiOut.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
        }
    }

    if (playbackReset || standaloneReset)
    {
        midiPlayback.releaseHeldNotes (midiOut);
        midiPlayback.reset (false);
    }

    if (stopAllNotes)
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            const juce::uint8 allNotesOff[] { static_cast<juce::uint8> (0xb0 | (channel - 1)), 123, 0 };
            midiOut.addEvent (allNotesOff, 3, 0);
        }
    }

    applyRecordingMonitors (ppqPosition, bpm, hostLoop);
    const auto playback = midiPlayback.processBlock (*snapshot,
                                                     ppqPosition,
                                                     bpm,
                                                     isPlaying,
                                                     numSamples,
                                                     midiOut,
                                                     midiPatternSelections,
                                                     hostLoop,
                                                     recordingClockHistory[(recordingClockHistoryIndex - 1) % recordingClockHistory.size()].updatedMs);

    midiMessages.swapWith (midiOut);
    midiOut.clear();

    if (hasStandaloneTransport() && isPlaying && bpm > 0.0)
    {
        const double sampleRate = recordingSampleRate;
        const double blockEndPpq =
            ppqPosition + (static_cast<double> (numSamples) / sampleRate) * (bpm / 60.0);
        standaloneTransportPpqPosition.store (blockEndPpq, std::memory_order_relaxed);
    }

    currentStepForUi.store (playback.currentStepOneBased, std::memory_order_relaxed);
    globalStepForUi.store (playback.latestGlobalStep, std::memory_order_relaxed);

    bool notify = midiPatternSelections.count > 0;

    if (playback.generatedSourcesChanged)
        notify = generatedSourcesForEngine.try_enqueue (midiPlayback.generatedSourcesFrame()) || notify;

    // Defer host tempo changes to the message thread (engine mutation + UI emit are not RT-safe).
    // The engine clamps to 20-300 BPM, so compare clamped or out-of-range tempos never settle.
    if (isPlaying && bpm > 0.0 && std::abs (snapshot->tempo - clampStandaloneTempoBpm (bpm)) > 0.01)
    {
        pendingHostBpm.store (bpm, std::memory_order_relaxed);
        hostBpmChangePending.store (true, std::memory_order_release);
        notify = true;
    }

    bool pushedHit = false;

    for (size_t i = 0; i < playback.noteHitCount; ++i)
        pushedHit = noteHitsForUi.try_enqueue (playback.noteHits[i]) || pushedHit;

    // Drained by the message-thread timer; posting an OS message from here could block.
    if (pushedHit || notify)
        requestMessageThreadWork();
}

void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    midiPlayback.suspend (midiMessages);
    currentStepForUi.store (0, std::memory_order_relaxed);
}

juce::MidiBuffer& PluginProcessor::reservedMidiOutputBuffer()
{
    for (auto& buffer : midiOutputBuffers)
    {
        const auto* storage = buffer.data.begin();

        if (storage != nullptr && std::find (reservedMidiStorage.begin(), reservedMidiStorage.end(), storage) != reservedMidiStorage.end())
            return buffer;
    }

    // Only reached when the host hands over fresh buffers each block; this slot may then grow.
    return midiOutputBuffers.front();
}

bool PluginProcessor::consumeMidiInput (const juce::MidiBuffer& midiMessages,
                                        const ksh::PlaybackSnapshot& snapshot,
                                        double ppqPosition,
                                        double bpm,
                                        bool isPlaying,
                                        ksh::MidiPatternSelectionBlock& selections, ksh::HostLoopRange loop)
{
    bool shouldFilter = false;
    bool recordQueued = false;
    const auto recordSession = patternRecordingSession.load (std::memory_order_acquire);
    const bool recordEnabled = recordSession != 0;
    const int strength = getPatternRecordingQuantizeStrength();
    const double recordOffsetBeats = getPatternRecordingOffsetMs() * bpm / 60000.0;
    const int recordSource = ksh::clampInt (patternRecordingSource.load (std::memory_order_acquire),
                                           0,
                                           ksh::Constants::sourceCount - 1);
    const auto recordGrid = ksh::patternRecordingGrid (snapshot, recordSource);
    const double sampleRate = recordingSampleRate;
    const double quartersPerSample = bpm > 0.0 ? (bpm / 60.0) / sampleRate : 0.0;

    auto rowForNote = [&snapshot] (int noteNumber) -> int
    {
        for (int channel = 0; channel < snapshot.channelCount; ++channel)
        {
            if (snapshot.channels[static_cast<size_t> (channel)].note == noteNumber)
                return channel;
        }

        return -1;
    };

    // Raw bytes only: building a MidiMessage per event can allocate for SysEx.
    for (const auto metadata : midiMessages)
    {
        const bool noteOn = isNoteOnBytes (metadata.data, metadata.numBytes);
        const bool noteOff = isNoteOffBytes (metadata.data, metadata.numBytes);

        if (! noteOn && ! noteOff)
            continue;

        const int noteNumber = metadata.data[1];

        if (recordEnabled && noteOn && isPlaying && quartersPerSample > 0.0
            && !timestampedStandaloneMidiInput.load (std::memory_order_acquire))
        {
            if (const int row = rowForNote (noteNumber); row >= 0)
            {
                const double eventPpq = ppqPosition + static_cast<double> (metadata.samplePosition) * quartersPerSample;
                const auto position = ksh::patternRecordingPosition (recordGrid, row, recordingBeat (eventPpq, recordOffsetBeats, loop), strength);
                recordQueued = enqueuePatternRecordEvent (pendingMidiPatternRecordEvents, recordSource, row, position.step, metadata.data[2], recordSession, position.nudge)
                            || recordQueued;
                [[maybe_unused]] const bool monitored = midiRecordingMonitors.try_enqueue ({ recordSession, recordSource, row, position.step, position.onsetPpq, recordingTransportGeneration });
            }
        }

        if (! isMidiPatternSelectionNoteNumber (noteNumber))
            continue;

        shouldFilter = true;

        if (noteOn)
        {
            const int source = sourceForMidiPatternSelectionNoteNumber (noteNumber);
            [[maybe_unused]] const bool queued = pendingMidiPatternSelections.try_enqueue (source);
            selections.add (metadata.samplePosition, source);
        }
    }

    if (recordQueued)
        messageThreadWorkPending.store (true, std::memory_order_release);

    return shouldFilter;
}

void PluginProcessor::publishPatternRecordTransportPosition (double ppqPosition, double bpm, bool isPlaying, ksh::HostLoopRange loop)
{
    const PatternRecordTransport anchor { ppqPosition, bpm, juce::Time::getMillisecondCounterHiRes(), isPlaying, loop, uiBridge.getDocumentEpoch(), recordingTransportGeneration };
    recordingClockHistory[recordingClockHistoryIndex++ % recordingClockHistory.size()] = anchor;
    patternRecordTransport.publish (anchor);
}

void PluginProcessor::captureStandaloneMidiInput (const juce::MidiMessage& message)
{
    const auto session = patternRecordingSession.load (std::memory_order_acquire);
    if (session == 0 || !message.isNoteOn() || (hasStandaloneTransport() && !isStandaloneTransportPlaying()))
        return;
    const StandaloneRecordInput input { message.getTimeStamp() * 1000.0, session,
                                       patternRecordingSource.load (std::memory_order_acquire),
                                       getPatternRecordingQuantizeStrength(), getPatternRecordingOffsetMs(),
                                       message.getNoteNumber(), message.getVelocity() };
    // Multiple MIDI devices may invoke their driver callbacks concurrently. Serialize
    // producers here, never in processBlock, to preserve the queue's single producer contract.
    std::scoped_lock lock { standaloneMidiProducerMutex };
    [[maybe_unused]] const bool queued = standaloneRecordInput.try_enqueue (input);
}

void PluginProcessor::consumeStandaloneRecording (const ksh::PlaybackSnapshot& snapshot)
{
    StandaloneRecordInput input;
    while (standaloneRecordInput.try_dequeue (input))
    {
        if (!timestampedStandaloneMidiInput.load (std::memory_order_acquire)
            || input.session != patternRecordingSession.load (std::memory_order_acquire))
            continue;
        const PatternRecordTransport* anchor = nullptr;
        for (size_t offset = 0; offset < recordingClockHistory.size(); ++offset)
        {
            const auto index = (recordingClockHistoryIndex + recordingClockHistory.size() - 1 - offset) % recordingClockHistory.size();
            const auto& candidate = recordingClockHistory[index];
            if (candidate.updatedMs > 0.0 && candidate.updatedMs <= input.timestampMs)
            {
                anchor = &candidate;
                break;
            }
        }
        if (anchor == nullptr || !anchor->playing || anchor->documentEpoch != uiBridge.getDocumentEpoch()
            || input.timestampMs - anchor->updatedMs > kMaxPatternRecordTransportAgeMs)
            continue;
        const double beat = recordingBeat (anchor->ppq + (input.timestampMs - anchor->updatedMs) * anchor->bpm / 60000.0,
                                           input.offsetMs * anchor->bpm / 60000.0, anchor->loop);
        for (int channel = 0; channel < snapshot.channelCount; ++channel)
        {
            if (snapshot.channels[static_cast<size_t> (channel)].note != input.note)
                continue;
            const auto position = ksh::patternRecordingPosition (ksh::patternRecordingGrid (snapshot, input.source), channel, beat, input.strength);
            if (enqueuePatternRecordEvent (pendingMidiPatternRecordEvents, input.source, channel, position.step, input.velocity, input.session, position.nudge))
            {
                [[maybe_unused]] const bool monitored = midiRecordingMonitors.try_enqueue ({ input.session, input.source, channel, position.step, position.onsetPpq, anchor->transportGeneration });
                requestMessageThreadWork();
            }
            break;
        }
    }
}

void PluginProcessor::applyRecordingMonitors (double ppq, double bpm, ksh::HostLoopRange loop)
{
    const double samplesPerBeat = recordingSampleRate * 60.0 / std::max (1.0, bpm);
    const auto drain = [&] (auto& queue)
    {
        RecordingMonitor monitor;
        while (queue.try_dequeue (monitor))
        {
            if (monitor.session != recordingMonitorSession.load (std::memory_order_acquire)
                || monitor.transportGeneration != recordingTransportGeneration)
                continue;
            double delta = monitor.onsetPpq - ppq;
            if (loop.active && loop.endPpq > loop.startPpq)
            {
                const double length = loop.endPpq - loop.startPpq;
                delta -= std::round (delta / length) * length;
            }
            midiPlayback.suppressRecordedHit (monitor.source, monitor.channel, monitor.step,
                                              static_cast<int64_t> (std::llround (delta * samplesPerBeat)));
        }
    };
    drain (midiRecordingMonitors);
    drain (uiRecordingMonitors);
}

void PluginProcessor::drainPendingMidiPatternSelectionsLocked()
{
    int source = 0;
    bool switched = false;

    while (pendingMidiPatternSelections.try_dequeue (source))
    {
        engine.setStaticSource (source);
        switched = true;
    }

    // The newly selected pattern brings its own rate; show it on the host parameter.
    if (switched)
        syncMacroParametersFromEngineLocked (true);
}

bool PluginProcessor::enqueuePatternRecordEvent (moodycamel::ReaderWriterQueue<PatternRecordEvent>& queue,
                                                 int source, int channel, int step, int velocity, uint64_t session, int nudge)
{
    if (session == 0)
        return false;
    PatternRecordEvent event;
    event.session = session;
    event.source = ksh::clampInt (source, 0, ksh::Constants::sourceCount - 1);
    event.channel = ksh::clampInt (channel, 0, ksh::Constants::maxChannels - 1);
    event.step = ksh::clampInt (step, 0, ksh::Constants::maxSteps - 1);
    event.velocity = ksh::clampInt (velocity, 1, 127);
    event.nudge = ksh::clampInt (nudge, -100, 100);
    return queue.try_enqueue (event);
}

void PluginProcessor::recordPatternCellLocked (int source, int channel, int step, int velocity, int nudge)
{
    const auto before = engine.sourceCellAt (source, channel, step);
    engine.setCell (source, channel, step, true, velocity, 100, 1, 0, false, 1);
    engine.setCellNudge (source, channel, step, nudge);
    const auto after = engine.sourceCellAt (source, channel, step);
    if (before == after)
        return;
    auto& edits = patternRecordingEdits.cells;
    const auto existing = std::find_if (edits.begin(), edits.end(), [=] (const auto& edit)
    {
        return edit.source == source && edit.channel == channel && edit.step == step;
    });
    if (existing == edits.end())
        edits.push_back ({ source, channel, step, before, after });
    else
    {
        // A manual/external edit between loop iterations becomes the new baseline
        // for that field, rather than being swept into the recording's undo.
        const auto rebase = [] (auto& baseline, const auto& last, const auto& now)
        {
            if (last != now)
                baseline = now;
        };
        rebase (existing->expected.enabled, existing->value.enabled, before.enabled);
        rebase (existing->expected.velocity, existing->value.velocity, before.velocity);
        rebase (existing->expected.probability, existing->value.probability, before.probability);
        rebase (existing->expected.cycle, existing->value.cycle, before.cycle);
        rebase (existing->expected.cycleMask, existing->value.cycleMask, before.cycleMask);
        rebase (existing->expected.roll, existing->value.roll, before.roll);
        rebase (existing->expected.nudge, existing->value.nudge, before.nudge);
        existing->value = after;
    }
}

PluginProcessor::PatternRecordingEdits PluginProcessor::takePatternRecordingEdits()
{
    std::scoped_lock lock { engineStateMutex };
    drainPendingPatternRecordEventsLocked();
    publishPlaybackSnapshotIfChangedLocked();
    PatternRecordingEdits result { patternRecordingEdits.session, patternRecordingEdits.documentEpoch,
                                   patternRecordingEdits.armed, std::move (patternRecordingEdits.cells) };
    patternRecordingEdits.cells.clear();
    return result;
}

void PluginProcessor::drainPendingPatternRecordEventsLocked()
{
    bool changed = false;

    const auto drain = [this, &changed] (auto& queue)
    {
        PatternRecordEvent event;
        while (queue.try_dequeue (event))
        {
            if (!patternRecordingEdits.armed || event.session != patternRecordingEdits.session
                || patternRecordingEdits.documentEpoch != uiBridge.getDocumentEpoch())
                continue;
            recordPatternCellLocked (event.source, event.channel, event.step, event.velocity, event.nudge);
            changed = true;
        }
    };
    drain (pendingMidiPatternRecordEvents);
    drain (pendingUiPatternRecordEvents);

    if (changed)
    {
        uiBridge.invalidate();
        markDocumentModified();
    }
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto jsonText = enginePersistenceState().dump();
    destData.replaceAll (jsonText.data(), jsonText.size());
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    [[maybe_unused]] const bool restored = loadStateFromData (data, sizeInBytes);
}

bool PluginProcessor::loadStateFromData (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return false;

    const auto payload = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));

    if (! payload.has_value())
        return false;
    for (const auto& [key, minimum, maximum] : { std::tuple { "patternRecordQuantizeStrength", 0, 100 },
                                               std::tuple { "patternRecordOffsetMs", -100, 100 } })
    {
        const auto value = payload->find (key);
        if (value != payload->end() && (!value->is_number_integer() || *value < minimum || *value > maximum))
            return false;
    }

    {
        std::scoped_lock lock { engineStateMutex };
        const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);
        const auto previousEngineState = engine.serializeForPersistence();
        bool restored = false;

        try
        {
            restored = engine.deserializeForPersistence (*payload);
        }
        catch (...)
        {
            restored = false;
        }

        if (! restored)
        {
            try
            {
                const auto rollbackSucceeded = engine.deserializeForPersistence (previousEngineState);
                if (! rollbackSucceeded)
                    jassertfalse;
            }
            catch (...)
            {
                jassertfalse;
            }
        }

        suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);

        if (! restored)
            return false;

        patternRecordingQuantizeStrength.store (payload->value ("patternRecordQuantizeStrength", 100), std::memory_order_release);
        patternRecordingOffsetMs.store (payload->value ("patternRecordOffsetMs", 0), std::memory_order_release);
        syncMacroParametersFromEngineLocked (false);
        publishPlaybackSnapshotLocked();
        patternRecordingSession.store (0, std::memory_order_release);
        patternRecordingEnabled.store (0, std::memory_order_release);
        patternRecordingEdits.armed = false;
        patternRecordingEdits.cells.clear();
        recordingMonitorSession.store (0, std::memory_order_release);
        uiBridge.documentReplaced();
    }

    applyProjectMetadataFromState (*payload);

    if (const auto playing = payload->find ("standaloneTransportPlaying");
        playing != payload->end() && (playing->is_boolean() || playing->is_number()))
    {
        const bool shouldPlay = playing->is_boolean() ? playing->get<bool>() : playing->get<int>() != 0;
        if (hasStandaloneTransport())
            setStandaloneTransportPlaying (shouldPlay);
    }

    ksh::NativeHit drained;

    while (noteHitsForUi.try_dequeue (drained))
    {
    }

    currentStepForUi.store (0, std::memory_order_relaxed);
    requestPlaybackReset();
    markDocumentClean();
    uiBridge.invalidate();
    requestMessageThreadWork();
    return true;
}

nlohmann::json PluginProcessor::enginePersistenceState()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    publishPlaybackSnapshotIfChangedLocked();

    auto state = engine.serializeForPersistence();
    {
        std::scoped_lock metadataLock { projectMetadataMutex };
        state["projectName"] = projectName.toStdString();
        state["projectDescription"] = projectDescription.toStdString();
        state["projectAuthor"] = projectAuthor.toStdString();
        state["projectCreatedAt"] = projectCreatedAt.toStdString();
        state["projectModifiedAt"] = projectModifiedAt.toStdString();
    }
    state["standaloneTransportPlaying"] = isStandaloneTransportPlaying() ? 1 : 0;
    state["patternRecordQuantizeStrength"] = getPatternRecordingQuantizeStrength();
    state["patternRecordOffsetMs"] = getPatternRecordingOffsetMs();
    return state;
}

nlohmann::json PluginProcessor::enginePreviewState()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    publishPlaybackSnapshotIfChangedLocked();

    return engine.snapshot();
}

ksh::EngineStateSnapshot PluginProcessor::engineStateSnapshot()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    publishPlaybackSnapshotIfChangedLocked();

    return engine.stateSnapshot();
}

ksh::PlaybackSnapshot PluginProcessor::enginePlaybackSnapshot()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    publishPlaybackSnapshotIfChangedLocked();

    return engine.makePlaybackSnapshot();
}

bool PluginProcessor::applyPersistenceFromUi (const nlohmann::json& state)
{
    if (! state.is_object())
        return false;

    std::scoped_lock lock { engineStateMutex };
    const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);
    const auto previousEngineState = engine.serializeForPersistence();

    bool ok = false;

    try
    {
        ok = engine.deserializeForPersistence (state, true);
    }
    catch (...)
    {
        ok = false;
    }

    if (! ok)
    {
        try
        {
            const auto rollbackSucceeded = engine.deserializeForPersistence (previousEngineState, true);
            if (! rollbackSucceeded)
                jassertfalse;
        }
        catch (...)
        {
            jassertfalse;
        }
    }

    suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);

    if (! ok)
        return false;

    syncMacroParametersFromEngineLocked (true);
    publishPlaybackSnapshotLocked();
    markDocumentModified();
    return true;
}

bool PluginProcessor::dispatchUiEngineCommand (std::string_view selector, const nlohmann::json& args)
{
    if (selector == "pattern_record_enabled")
    {
        const bool shouldRecord = args.is_array() && ! args.empty()
                                      ? (args[0].is_boolean() ? args[0].get<bool>() : args[0].get<int>() != 0)
                                      : false;
        const int source = args.is_array() && args.size() > 1 ? args[1].get<int>() - 1 : 0;
        setPatternRecordingEnabled (shouldRecord, source);
        uiBridge.invalidate();
        return true;
    }

    if (selector == "pattern_record_row")
    {
        const int row = args.is_array() && ! args.empty() ? args[0].get<int>() - 1 : 0;
        const int velocity = args.is_array() && args.size() > 1 ? args[1].get<int>() : 100;
        const bool ok = recordPatternRowAtCurrentStep (row, velocity);
        if (ok)
            uiBridge.invalidate();
        return ok;
    }

    if (selector == "channel_audition")
    {
        const int row = args.is_array() && ! args.empty() ? args[0].get<int>() - 1 : 0;
        return queueRowAudition (row, ksh::CellDefaults::velocity);
    }

    if (selector == "standalone_transport_playing")
    {
        const bool shouldPlay = args.is_array() && ! args.empty()
                                    ? (args[0].is_boolean() ? args[0].get<bool>() : args[0].get<int>() != 0)
                                    : false;
        setStandaloneTransportPlaying (shouldPlay);
        uiBridge.invalidate();
        return hasStandaloneTransport();
    }

    if (selector == "standalone_tempo")
    {
        const double bpm = args.is_array() && ! args.empty() ? args[0].get<double>() : getStandaloneTempoBpm();
        setStandaloneTempoBpm (bpm);
        return hasStandaloneTransport();
    }

    std::scoped_lock lock { engineStateMutex };

    const bool ok = ksh::dispatchEngineCommand (engine, selector, args);
    if (ok) uiBridge.invalidate();

    if (! ok)
        return false;

    if (selector != "reset")
        markDocumentModified();

    if (isMacroParameterID (selector) || selector == "source_rate" || selector == "static_source")
        syncMacroParametersFromEngineLocked (true);

    if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
        publishPlaybackSnapshotLocked();

    return true;
}

void PluginProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused (newValue);

    if (suppressParameterCallbacks.load (std::memory_order_relaxed))
        return;

    const auto parameterBit = macroParameterBit (parameterID);

    if (parameterBit == 0)
        return;

    dirtyMacroParameters.fetch_or (parameterBit, std::memory_order_acq_rel);
    messageThreadWorkPending.store (true, std::memory_order_release);
}

void PluginProcessor::addMacroParameterListeners()
{
    for (const auto id : kMacroParameterIDs)
        parameters.addParameterListener (juceStringFromView (id), this);
}

void PluginProcessor::removeMacroParameterListeners()
{
    for (const auto id : kMacroParameterIDs)
        parameters.removeParameterListener (juceStringFromView (id), this);
}

void PluginProcessor::applyMacroParametersToEngineLocked (uint32_t parameterMask)
{
    // Only the parameters that moved: re-applying the rest would write stale values,
    // e.g. the previous pattern's rate into a pattern just selected over MIDI.
    if ((parameterMask & rateBit) != 0)
    {
        const int rateIndex = ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "rate"))),
                                            0,
                                            static_cast<int> (ksh::Constants::rates.size()) - 1);
        const auto rate = ksh::Constants::rates[static_cast<size_t> (rateIndex)];

        if (engine.getRate() != rate)
            engine.setRate (rate);
    }

    if ((parameterMask & swingBit) != 0)
    {
        const auto swing = ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "swing"))), 0, 100);

        if (engine.getSwing() != swing)
            engine.setSwing (swing);
    }

    if ((parameterMask & velocityHumanizeBit) != 0)
    {
        const auto velocityHumanize =
            ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "velocity_humanize"))), 0, 100);

        if (engine.getVelocityHumanize() != velocityHumanize)
            engine.setVelocityHumanize (velocityHumanize);
    }

    if ((parameterMask & timingHumanizeBit) != 0)
    {
        const auto timingHumanize =
            ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "timing_humanize"))), 0, 100);

        if (engine.getTimingHumanize() != timingHumanize)
            engine.setTimingHumanize (timingHumanize);
    }

    if ((parameterMask & deviceActiveBit) != 0)
    {
        const bool deviceActive = parameterValue (parameters, "device_active") >= 0.5f;

        if (engine.isDeviceActive() != deviceActive)
            engine.setDeviceActive (deviceActive);
    }
}

void PluginProcessor::syncMacroParametersFromEngineLocked (bool notifyHost)
{
    const auto previousSuppression = suppressParameterCallbacks.exchange (true, std::memory_order_acq_rel);
    const auto gestures = uiGestureParameters.load (std::memory_order_acquire);
    const auto inGesture = [gestures] (uint32_t bit) { return (gestures & bit) != 0; };

    setParameterValue (parameters, "rate", static_cast<float> (rateIndexForValue (engine.getRate())), notifyHost, inGesture (rateBit));
    setParameterValue (parameters, "swing", static_cast<float> (engine.getSwing()), notifyHost, inGesture (swingBit));
    setParameterValue (parameters, "velocity_humanize", static_cast<float> (engine.getVelocityHumanize()), notifyHost, inGesture (velocityHumanizeBit));
    setParameterValue (parameters, "timing_humanize", static_cast<float> (engine.getTimingHumanize()), notifyHost, inGesture (timingHumanizeBit));
    setParameterValue (parameters, "device_active", engine.isDeviceActive() ? 1.0f : 0.0f, notifyHost, inGesture (deviceActiveBit));

    suppressParameterCallbacks.store (previousSuppression, std::memory_order_release);
}

void PluginProcessor::setProjectUiScalePercent (const int uiScalePercent)
{
    const auto next = juce::jlimit (50, 120, uiScalePercent);
    if (projectUiScalePercent != next)
    {
        projectUiScalePercent = next;
        uiBridge.invalidate();
    }
}

void PluginProcessor::setPatternRecordingEnabled (bool shouldRecord, int source)
{
    std::scoped_lock lock { engineStateMutex };
    if (!shouldRecord)
    {
        // Stop producers first, then include every hit already queued in this pass.
        patternRecordingSession.store (0, std::memory_order_release);
        patternRecordingEnabled.store (0, std::memory_order_release);
        drainPendingPatternRecordEventsLocked();
        patternRecordingEdits.armed = false;
        publishPlaybackSnapshotIfChangedLocked();
    }
    else if (!patternRecordingEdits.armed || patternRecordingEdits.documentEpoch != uiBridge.getDocumentEpoch())
    {
        ++patternRecordingEdits.session;
        patternRecordingEdits.documentEpoch = uiBridge.getDocumentEpoch();
        patternRecordingEdits.cells.clear();
        patternRecordingEdits.armed = true;
    }
    patternRecordingSource.store (ksh::clampInt (source, 0, ksh::Constants::sourceCount - 1), std::memory_order_release);
    if (shouldRecord)
    {
        recordingMonitorSession.store (patternRecordingEdits.session, std::memory_order_release);
        patternRecordingSession.store (patternRecordingEdits.session, std::memory_order_release);
        patternRecordingEnabled.store (1, std::memory_order_release);
    }
    uiBridge.invalidate();
}

bool PluginProcessor::isPatternRecordingEnabled() const
{
    return patternRecordingEnabled.load (std::memory_order_acquire) != 0;
}

bool PluginProcessor::queueRowAudition (int channelZeroBased, int velocity)
{
    if (auditionDeviceActive.load (std::memory_order_acquire) == 0)
        return false;

    const int channelCount = auditionChannelCount.load (std::memory_order_acquire);

    if (channelZeroBased < 0 || channelZeroBased >= channelCount)
        return false;

    const int pitch = auditionChannelNotes[static_cast<size_t> (channelZeroBased)].load (std::memory_order_acquire);
    midiPlayback.queueAuditionNote ({ pitch,
                                      ksh::clampInt (velocity, 1, 127),
                                      ksh::Constants::defaultMidiChannel,
                                      ksh::Constants::defaultNoteDurationMs,
                                      0.0, juce::Time::getMillisecondCounterHiRes() });
    return true;
}

void PluginProcessor::setPatternRecordingQuantized (bool quantized)
{
    setPatternRecordingQuantizeStrength (quantized ? 100 : 0);
}

void PluginProcessor::setPatternRecordingQuantizeStrength (int strength)
{
    strength = ksh::clampInt (strength, 0, 100);
    if (patternRecordingQuantizeStrength.exchange (strength, std::memory_order_acq_rel) == strength)
        return;
    markDocumentModified();
    uiBridge.invalidate();
    requestMessageThreadWork();
}

void PluginProcessor::setPatternRecordingOffsetMs (int offset)
{
    offset = ksh::clampInt (offset, -100, 100);
    if (patternRecordingOffsetMs.exchange (offset, std::memory_order_acq_rel) == offset)
        return;
    markDocumentModified();
    uiBridge.invalidate();
    requestMessageThreadWork();
}

float PluginProcessor::getRecordingPulseForUi() const
{
    const auto* transport = patternRecordTransport.current();
    if (!isPatternRecordingEnabled() || transport == nullptr || !transport->playing)
        return 1.0f;
    const auto ageMs = juce::Time::getMillisecondCounterHiRes() - transport->updatedMs;
    if (transport->updatedMs <= 0.0 || ageMs < 0.0 || ageMs > kMaxPatternRecordTransportAgeMs)
        return 1.0f;
    const double beat = transport->ppq + ageMs * transport->bpm / 60000.0;
    const double phase = beat - std::floor (beat);
    return static_cast<float> (0.35 + 0.65 * std::exp (-6.0 * phase));
}

std::optional<ksh::PatternRecordingPosition> PluginProcessor::positionForUiTrigger (int source, int channel, double timestampMs, uint64_t* generation) const
{
    const auto* transport = patternRecordTransport.current();
    const auto* grids = patternRecordGrids.current();
    if (transport == nullptr || grids == nullptr || !transport->playing || transport->documentEpoch != uiBridge.getDocumentEpoch()
        || (hasStandaloneTransport() && !isStandaloneTransportPlaying()))
        return std::nullopt;
    const double ageMs = (timestampMs > 0.0 ? timestampMs : juce::Time::getMillisecondCounterHiRes()) - transport->updatedMs;
    if (transport->updatedMs <= 0.0 || ageMs < 0.0 || ageMs > kMaxPatternRecordTransportAgeMs)
        return std::nullopt;
    if (generation != nullptr)
        *generation = transport->transportGeneration;
    const double beat = recordingBeat (transport->ppq + ageMs * transport->bpm / 60000.0,
                                       getPatternRecordingOffsetMs() * transport->bpm / 60000.0, transport->loop);
    return ksh::patternRecordingPosition ((*grids)[static_cast<size_t> (source)], channel, beat, getPatternRecordingQuantizeStrength());
}

bool PluginProcessor::triggerRowFromUi (int channelZeroBased, int velocity)
{
    const int vel = ksh::clampInt (velocity, 1, 127);
    const bool auditioned = queueRowAudition (channelZeroBased, vel);

    if (! isPatternRecordingEnabled())
        return auditioned;

    const int channelCount = auditionChannelCount.load (std::memory_order_acquire);

    if (channelZeroBased < 0 || channelZeroBased >= channelCount)
        return auditioned;

    const int source = ksh::clampInt (patternRecordingSource.load (std::memory_order_acquire),
                                     0, ksh::Constants::sourceCount - 1);
    uint64_t generation = 0;
    const auto position = positionForUiTrigger (source, channelZeroBased, 0.0, &generation);
    if (!position.has_value())
        return auditioned;
    const bool queued = enqueuePatternRecordEvent (pendingUiPatternRecordEvents, source, channelZeroBased, position->step, vel,
                                                   patternRecordingSession.load (std::memory_order_acquire), position->nudge);

    if (queued)
    {
        [[maybe_unused]] const bool monitored = uiRecordingMonitors.try_enqueue ({ patternRecordingSession.load (std::memory_order_acquire), source, channelZeroBased, position->step, position->onsetPpq, generation });
        messageThreadWorkPending.store (true, std::memory_order_release);
    }

    return auditioned || queued;
}

bool PluginProcessor::recordPatternRowAtCurrentStep (int channel, int velocity)
{
    if (! isPatternRecordingEnabled())
        return false;

    const int vel = ksh::clampInt (velocity, 1, 127);
    const int source = ksh::clampInt (patternRecordingSource.load (std::memory_order_acquire),
                                     0, ksh::Constants::sourceCount - 1);
    uint64_t generation = 0;
    const auto position = positionForUiTrigger (source, channel, 0.0, &generation);
    if (!position.has_value())
        return false;

    std::scoped_lock lock { engineStateMutex };
    const int row = ksh::clampInt (channel, 0, engine.getChannelCount() - 1);
    if (!patternRecordingEdits.armed || patternRecordingEdits.documentEpoch != uiBridge.getDocumentEpoch())
        return false;
    recordPatternCellLocked (source, row, position->step, vel, position->nudge);
    [[maybe_unused]] const bool monitored = uiRecordingMonitors.try_enqueue ({ patternRecordingSession.load (std::memory_order_acquire), source, row, position->step, position->onsetPpq, generation });
    [[maybe_unused]] const bool auditioned = queueRowAudition (row, vel);
    publishPlaybackSnapshotIfChangedLocked();
    engine.flushPreview();
    markDocumentModified();
    return true;
}

void PluginProcessor::setProjectMetadata (const juce::String& name,
                                          const juce::String& description,
                                          const juce::String& createdAt,
                                          const juce::String& modifiedAt,
                                          std::optional<juce::String> author)
{
    bool changed = false;

    {
        std::scoped_lock lock { projectMetadataMutex };
        const auto nextName = name.trim().isNotEmpty() ? name.trim() : juce::String ("Untitled Project");
        const auto nextDescription = description.trim();
        const auto nextAuthor = author.has_value() ? author->trim() : projectAuthor;
        changed = nextName != projectName || nextDescription != projectDescription || nextAuthor != projectAuthor;
        projectName = nextName;
        projectDescription = nextDescription;
        projectAuthor = nextAuthor;
        projectCreatedAt = createdAt;
        projectModifiedAt = modifiedAt;
    }

    if (changed)
        markDocumentModified();
}

juce::String PluginProcessor::getProjectName() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectName;
}

juce::String PluginProcessor::getProjectDescription() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectDescription;
}

juce::String PluginProcessor::getProjectAuthor() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectAuthor;
}

juce::String PluginProcessor::getProjectCreatedAt() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectCreatedAt;
}

juce::String PluginProcessor::getProjectModifiedAt() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectModifiedAt;
}

juce::File PluginProcessor::getProjectFile() const
{
    std::scoped_lock lock { projectMetadataMutex };
    return projectFile;
}

void PluginProcessor::setProjectFile (const juce::File& file)
{
    std::scoped_lock lock { projectMetadataMutex };
    projectFile = file;
}

void PluginProcessor::applyProjectMetadataFromState (const nlohmann::json& state)
{
    try
    {
        std::scoped_lock lock { projectMetadataMutex };
        projectFile = juce::File();
        projectName = juceStringFromView (state.value ("projectName", std::string { "Untitled Project" })).trim();
        if (projectName.isEmpty())
            projectName = "Untitled Project";

        projectDescription = juceStringFromView (state.value ("projectDescription", std::string {})).trim();
        projectAuthor = juceStringFromView (state.value ("projectAuthor", std::string {})).trim();
        projectCreatedAt = juceStringFromView (state.value ("projectCreatedAt", std::string {}));
        projectModifiedAt = juceStringFromView (state.value ("projectModifiedAt", std::string {}));
    }
    catch (...)
    {
        setProjectMetadata ("Untitled Project", {}, {}, {}, juce::String {});
    }
}

void PluginProcessor::resetProject()
{
    {
        std::scoped_lock lock { engineStateMutex };
        const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);

        try
        {
            const auto resetOk = engine.deserializeForPersistence (makeDefaultPersistenceState());
            juce::ignoreUnused (resetOk);
        }
        catch (...)
        {
        }

        suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);
        patternRecordingQuantizeStrength.store (100, std::memory_order_release);
        patternRecordingOffsetMs.store (0, std::memory_order_release);
        syncMacroParametersFromEngineLocked (false);
        publishPlaybackSnapshotLocked();
        patternRecordingSession.store (0, std::memory_order_release);
        patternRecordingEnabled.store (0, std::memory_order_release);
        patternRecordingEdits.armed = false;
        patternRecordingEdits.cells.clear();
        recordingMonitorSession.store (0, std::memory_order_release);
        uiBridge.documentReplaced();
    }

    setProjectMetadata ("Untitled Project", {}, {}, {}, juce::String {});
    setProjectFile ({});
    standaloneTransportPlaying.store (0, std::memory_order_relaxed);
    standaloneTransportPpqPosition.store (0.0, std::memory_order_relaxed);
    standaloneStopAllNotesRequested.store (1, std::memory_order_release);
    currentStepForUi.store (0, std::memory_order_relaxed);
    requestPlaybackReset();
    markDocumentClean();
    uiBridge.invalidate();
    requestMessageThreadWork();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}

PluginProcessor::UiState PluginProcessor::nativeUiState()
{
    UiState state;
    copyNativeUiState (state);
    return state;
}

void PluginProcessor::copyNativeUiState (UiState& state)
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    publishPlaybackSnapshotIfChangedLocked();
    engine.fillPlaybackSnapshot (state.playback);
    state.refreshSteps = engine.getRefreshSteps();
    state.revision = uiBridge.getRevision();
    state.documentEpoch = uiBridge.getDocumentEpoch();
}

ksh::DocumentState PluginProcessor::editorDocument()
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    return engine.documentState();
}

bool PluginProcessor::applyEditorDocument (const ksh::DocumentState& document)
{
    return applyPersistenceFromUi (ksh::KickSnareHatEngine::documentToJson (document));
}

void PluginProcessor::beginUiParameterGesture (const juce::String& parameterID)
{
    const auto bit = macroParameterBit (parameterID);
    auto* parameter = parameters.getParameter (parameterID);

    if (bit == 0 || parameter == nullptr || (uiGestureParameters.fetch_or (bit, std::memory_order_acq_rel) & bit) != 0)
        return;

    parameter->beginChangeGesture();
}

void PluginProcessor::endUiParameterGesture (const juce::String& parameterID)
{
    const auto bit = macroParameterBit (parameterID);
    auto* parameter = parameters.getParameter (parameterID);

    if (bit == 0 || parameter == nullptr || (uiGestureParameters.fetch_and (~bit, std::memory_order_acq_rel) & bit) == 0)
        return;

    parameter->endChangeGesture();
}

bool PluginProcessor::applyNativeCellEdits (const std::vector<CellEdit>& edits, uint64_t epoch)
{
    if (edits.size() > 16 * 8 * 32) return false;
    std::scoped_lock lock { engineStateMutex };
    if (epoch != uiBridge.getDocumentEpoch()) return false;
    for (const auto& edit : edits)
    {
        if (edit.source < 0 || edit.source >= 16 || edit.channel < 0 || edit.channel >= 8
            || edit.step < 0 || edit.step >= 32
            || engine.sourceCellAt (edit.source, edit.channel, edit.step) != edit.expected)
            return false;
    }
    for (const auto& edit : edits)
    {
        const auto& c = edit.value;
        engine.setCell (edit.source, edit.channel, edit.step, c.enabled, c.velocity,
                        c.probability, c.cycle, c.cycleMask, c.roll);
        engine.setCellNudge (edit.source, edit.channel, edit.step, c.nudge);
    }
    publishPlaybackSnapshotIfChangedLocked();
    uiBridge.invalidate();
    markDocumentModified();
    return true;
}
