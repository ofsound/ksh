#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KshUiBridge.h"
#include "ui/KshUiScale.h"

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

constexpr uint64_t macroValueMask = 0x7f;
constexpr uint64_t macroGenerationIncrement = macroValueMask + 1;

constexpr auto hostPatternParameterID = "pattern";
constexpr int mutedPatternChoice = ksh::Constants::sourceCount + 1;
constexpr uint32_t allRowMuteBits = (1u << ksh::Constants::maxChannels) - 1;

juce::String rowMuteParameterID (int row)
{
    return "row_" + juce::String (row + 1) + "_mute";
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

struct MacroNotification
{
    const PluginProcessor* processor = nullptr;
    uint32_t bit = 0;
};
thread_local MacroNotification notifyingMacro;
double clampStandaloneTempoBpm (double bpm)
{
    if (std::isnan (bpm))
        bpm = 120.0;

    return std::clamp (bpm, 20.0, 300.0);
}

nlohmann::json makeDefaultPersistenceState()
{
    const auto defaults = std::make_unique<ksh::DocumentState>();
    return ksh::KickSnareHatEngine::documentToJson (*defaults);
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

    juce::StringArray patterns { "Manual" };
    for (int pattern = 1; pattern <= ksh::Constants::sourceCount; ++pattern)
        patterns.add (juce::String (pattern));
    patterns.add ("M");
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { hostPatternParameterID, 2 }, "Pattern", patterns, 0));

    // Keep the original mute automation ahead of the new tracks in Audio Unit parameter order.
    constexpr int originalMuteParameters = 8;
    for (int row = 0; row < ksh::Constants::maxChannels; ++row)
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParameterID { rowMuteParameterID (row), row < originalMuteParameters ? 2 : 3 },
            "Row " + juce::String (row + 1) + " Mute", false));

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
    hostPatternParameter = static_cast<juce::AudioParameterChoice*> (parameters.getParameter (hostPatternParameterID));
    for (size_t row = 0; row < hostRowMuteParameters.size(); ++row)
        hostRowMuteParameters[row] = static_cast<juce::AudioParameterBool*> (parameters.getParameter (rowMuteParameterID (static_cast<int> (row))));
    initializeDefaultPattern();
    syncMacroParametersFromEngineLocked();
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
                             ? (snapshot.channels[static_cast<size_t> (channel)].controller
                                    ? -1 : snapshot.channels[static_cast<size_t> (channel)].note)
                             : ksh::Constants::defaultChannelNotes[static_cast<size_t> (channel)];
        auditionChannelNotes[static_cast<size_t> (channel)].store (note, std::memory_order_relaxed);
    }
}

void PluginProcessor::publishPlaybackSnapshotLocked()
{
    playbackMailbox->publishWith ([this] (ProcessorPlaybackSnapshot& snapshot)
                                 {
                                     engine.fillPlaybackSnapshot (snapshot);
                                     snapshot.documentEpoch = uiBridge.getDocumentEpoch();
                                     snapshot.patternSelectionRevision = patternSelectionRevision;
                                     snapshot.appliedHostMacros = appliedHostMacros;
                                     snapshot.hostRateBaseline = hostRateBaseline;
                                     snapshot.restoredHostRate = restoredHostRate;
                                     refreshAuditionCacheFromSnapshot (snapshot);
                                 });
    lastPublishedSnapshotVersion = engine.playbackSnapshotVersion();
}

void PluginProcessor::applyPendingMacroParametersLocked()
{
    for (size_t i = 0; i < hostMacroChanges.size(); ++i)
    {
        const auto change = hostMacroChanges[i].load (std::memory_order_acquire);
        if (change == appliedHostMacros[i])
            continue;

        appliedHostMacros[i] = change;
        const int value = static_cast<int> (change & macroValueMask);
        switch (1u << i)
        {
            case rateBit:
            {
                const auto rate = ksh::Constants::rates[static_cast<size_t> (value)];
                if (engine.getRate() != rate)
                    engine.setRate (rate);
                break;
            }
            case swingBit:
                if (engine.getSwing() != value)
                    engine.setSwing (value);
                break;
            case velocityHumanizeBit:
                if (engine.getVelocityHumanize() != value)
                    engine.setVelocityHumanize (value);
                break;
            case timingHumanizeBit:
                if (engine.getTimingHumanize() != value)
                    engine.setTimingHumanize (value);
                break;
            case deviceActiveBit:
                if (engine.isDeviceActive() != (value != 0))
                    engine.setDeviceActive (value != 0);
                break;
            default: break;
        }
    }
}

void PluginProcessor::acknowledgeHostMacrosLocked()
{
    for (size_t i = 0; i < hostMacroChanges.size(); ++i)
        appliedHostMacros[i] = hostMacroChanges[i].load (std::memory_order_acquire);
    hostRateBaseline = appliedHostMacros[0];
    restoredHostRate.reset();
}

uint32_t PluginProcessor::hostMutedRows() const
{
    uint32_t mutedRows = 0;
    for (size_t row = 0; row < hostRowMuteParameters.size(); ++row)
        if (hostRowMuteParameters[row]->get())
            mutedRows |= 1u << row;
    return mutedRows;
}

std::optional<int> PluginProcessor::hostRateOverride (uint64_t baseline, std::optional<int> restoredRate) const
{
    const auto rate = hostMacroChanges[0].load (std::memory_order_acquire);
    if (rate > baseline)
        return static_cast<int> (rate & macroValueMask);
    return restoredRate;
}

ksh::PlaybackMacros PluginProcessor::playbackMacrosForBlock (const ProcessorPlaybackSnapshot& snapshot) const
{
    auto macros = static_cast<const ksh::PlaybackMacros&> (snapshot);
    for (size_t i = 0; i < hostMacroChanges.size(); ++i)
    {
        const auto change = hostMacroChanges[i].load (std::memory_order_acquire);
        if (change <= snapshot.appliedHostMacros[i])
            continue;

        const int value = static_cast<int> (change & macroValueMask);
        switch (1u << i)
        {
            case rateBit: macros.beatsPerStep = ksh::Constants::beatsForRate (ksh::Constants::rates[static_cast<size_t> (value)]); break;
            case swingBit: macros.swing = value; break;
            case velocityHumanizeBit: macros.velocityHumanize = value; break;
            case timingHumanizeBit: macros.timingHumanize = value; break;
            case deviceActiveBit: macros.deviceActive = value != 0; break;
            default: break;
        }
    }
    if (!hasStandaloneTransport())
    {
        macros.mutedRows = hostMutedRows();
        const auto pattern = hostPatternParameter->getIndex();
        if (pattern == mutedPatternChoice)
            macros.patternSource = ksh::Constants::mutedStaticSource;
        else if (pattern > 0)
        {
            const auto source = static_cast<size_t> (pattern - 1);
            macros.patternSource = static_cast<int> (source);
            const auto hostRate = hostRateOverride (snapshot.hostRateBaseline, snapshot.restoredHostRate);
            const auto rate = hostRate ? ksh::Constants::rates[static_cast<size_t> (*hostRate)] : snapshot.sourceSettings[source].rate;
            macros.beatsPerStep = ksh::Constants::beatsForRate (rate);
        }
    }
    return macros;
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
    PlaybackUiEvent<ksh::NativeHit> hit {};
    while (noteHitsForUi.try_dequeue (hit))
    {
        if (hit.documentEpoch != uiBridge.getDocumentEpoch())
            continue;
        uiBridge.emitNoteHit (hit.event);
        ++emitted;
    }

    PlaybackUiEvent<ksh::NativeCycleVisit> visit {};
    while (cycleVisitsForUi.try_dequeue (visit))
    {
        if (visit.documentEpoch != uiBridge.getDocumentEpoch())
            continue;
        uiBridge.emitCycleVisit (visit.event);
        ++emitted;
    }

    if (midiInputNotesPending.exchange (false, std::memory_order_acq_rel))
    {
        const auto timestampMs = juce::Time::getMillisecondCounterHiRes();
        for (size_t note = 0; note < midiInputNotesForUi.size(); ++note)
            if (const int velocity = midiInputNotesForUi[note].exchange (0, std::memory_order_relaxed); velocity > 0)
            {
                uiBridge.emitMidiInputNote (static_cast<int> (note), velocity, timestampMs);
                ++emitted;
            }
    }

    return emitted;
}

void PluginProcessor::runMessageThreadWork()
{
    // Hits first so flashes are never delayed behind engine work. Generation runs on the
    // audio thread, so nothing here sits between a refresh boundary and its first note.
    emitPendingNoteHitsForUi();

    uint32_t changedMacros = 0;
    {
        std::scoped_lock lock { engineStateMutex };

        applyPendingMacroParametersLocked();
        changedMacros = drainPendingMidiPatternSelectionsLocked();
        drainPendingPatternRecordEventsLocked();
        adoptGeneratedSourcesLocked();
        applyPendingHostTempoLocked();
        publishPlaybackSnapshotIfChangedLocked();

        if (engine.isPreviewDirty())
            engine.flushPreview();

        // Keep all mailbox producer operations serialized with host state restore.
        playbackMailbox->drainRetired();
    }

    notifyMacroParameterChanges (changedMacros);
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
    else if (midiInputNotesPending.load (std::memory_order_relaxed))
        emitPendingNoteHitsForUi();
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
    // JUCE's standalone player gives MIDI effects a zero-channel process buffer.
    return !hasStandaloneTransport();
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
    if (standaloneMidiOutput != nullptr)
        standaloneMidiOutput->prepare (sampleRate, samplesPerBlock);
    recordingSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    metronomeClickSample = juce::roundToInt (recordingSampleRate * 0.018);
    midiPlayback.prepare (recordingSampleRate);
    recordingClockHistory.fill ({});
    recordingClockHistoryIndex = 0;
    linkRecordingStartMs = 0.0;
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
    if (standaloneMidiOutput != nullptr)
        standaloneMidiOutput->stop();
    midiPlayback.resetForAudioDeviceRestart();
}

void PluginProcessor::setStandaloneMidiOutput (ksh::StandaloneMidiOutput* output)
{
    const juce::ScopedLock lock (getCallbackLock());
    if (standaloneMidiOutput == output)
        return;
    if (standaloneMidiOutput != nullptr)
        standaloneMidiOutput->stop();
    standaloneMidiOutput = hasStandaloneTransport() ? output : nullptr;
    if (standaloneMidiOutput != nullptr)
        standaloneMidiOutput->prepare (recordingSampleRate, getBlockSize());
}

void PluginProcessor::flushStandaloneMidiOutput()
{
    const juce::ScopedLock lock (getCallbackLock());
    if (standaloneMidiOutput != nullptr)
        standaloneMidiOutput->stop();
}

void PluginProcessor::setStandaloneSync (ksh::StandaloneSync* sync)
{
    const juce::ScopedLock lock (getCallbackLock());
    standaloneSync = hasStandaloneTransport() ? sync : nullptr;
}

void PluginProcessor::sendStandaloneMidiOutput (const juce::MidiBuffer& midi, int numSamples,
                                               juce::Optional<juce::uint64> presentationTimeNs)
{
    if (hasStandaloneTransport() && !midi.isEmpty())
        standaloneMidiActivity.fetch_add (1, std::memory_order_relaxed);
    if (standaloneMidiOutput == nullptr)
        return;
    juce::Optional<juce::uint64> hostTime;
    if (const auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            hostTime = position->getHostTimeNs();
    standaloneMidiOutput->send (midi, hostTime, numSamples, presentationTimeNs);
}

void PluginProcessor::renderStandaloneMetronome (juce::AudioBuffer<float>& buffer, double ppq, double bpm, bool playing, bool reset, int firstPlaybackSample)
{
    const int clickLength = juce::roundToInt (recordingSampleRate * 0.018);
    const bool active = playing && isStandaloneMetronomeEnabled() && buffer.getNumChannels() > 0;
    if (reset || !active)
        metronomeClickSample = clickLength;
    if (!active)
        return;

    const double beatsPerSample = bpm / (60.0 * recordingSampleRate);
    const float volume = static_cast<float> (getMetronomeVolume()) / 100.0f;
    const int numSamples = buffer.getNumSamples();
    // Assign each beat to its nearest sample, with the half-sample tie going to the next sample.
    auto beat = static_cast<int64_t> (std::ceil (ppq + (firstPlaybackSample - 0.5) * beatsPerSample));
    int start = firstPlaybackSample;
    while (start < numSamples)
    {
        const int nextClick = juce::jmax (firstPlaybackSample, static_cast<int> (std::floor ((static_cast<double> (beat) - ppq) / beatsPerSample + 0.5)));
        const int end = juce::jmin (nextClick, numSamples);
        const double pitchRatio = clickPitch < 0 ? 0.75 : clickPitch > 0 ? 1.3333333333333333 : 1.0;
        const double frequency = clickSound == MetronomeSound::wood ? 830.0 : clickSound == MetronomeSound::claves ? 1630.0 : 1175.0;
        const double accent = metronomeDownbeat ? 1760.0 / 1175.0 : 1.0;
        const double phaseStep = juce::MathConstants<double>::twoPi * frequency * accent * pitchRatio / recordingSampleRate;
        const float amplitude = volume * (metronomeDownbeat ? 0.18f : 0.12f);
        for (int sample = start; sample < end && metronomeClickSample < clickLength; ++sample, ++metronomeClickSample)
        {
            const float decay = 1.0f - static_cast<float> (metronomeClickSample) / static_cast<float> (clickLength);
            const double phase = phaseStep * metronomeClickSample;
            double tone = std::sin (phase);
            if (clickSound == MetronomeSound::wood)
                tone = 0.78 * tone + 0.22 * std::sin (phase * 2.45) * decay * decay;
            else if (clickSound == MetronomeSound::claves)
                tone = (0.72 * tone + 0.28 * std::sin (phase * 1.73)) * decay * decay;
            const float click = amplitude * decay * decay * static_cast<float> (tone);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, sample, click);
        }
        if (nextClick >= numSamples)
            break;
        metronomeClickSample = 0;
        metronomeDownbeat = (beat % 4) == 0;
        clickSound = getMetronomeSound();
        clickPitch = getMetronomePitch();
        ++beat;
        start = nextClick;
    }
}

bool PluginProcessor::hasStandaloneTransport() const
{
    return wrapperType == wrapperType_Standalone;
}

void PluginProcessor::setStandaloneTransportPlaying (bool shouldPlay, bool shareWithPeers)
{
    if (! hasStandaloneTransport() || (shouldPlay && jamSession.load (std::memory_order_acquire) != 0))
        return;

    if (standaloneSync != nullptr && standaloneSync->requestPlaying (shouldPlay, shareWithPeers))
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
    if (jamSession.load (std::memory_order_acquire) != 0)
        return false;
    return standaloneTransportPlaying.load (std::memory_order_relaxed) != 0 || isStandaloneLaunchPending();
}

void PluginProcessor::setStandaloneTempoBpm (double bpm)
{
    if (! hasStandaloneTransport())
        return;

    bpm = clampStandaloneTempoBpm (bpm);

    {
        std::scoped_lock lock { engineStateMutex };

        if (std::abs (engine.getTempo() - bpm) > 0.01)
        {
            engine.setTempo (bpm);
            publishPlaybackSnapshotIfChangedLocked();
            markDocumentModified();
        }
    }

    if (standaloneSync != nullptr)
        standaloneSync->requestTempo (bpm);
}

double PluginProcessor::getStandaloneTempoBpm()
{
    if (standaloneSync != nullptr)
        if (const auto tempo = standaloneSync->sessionTempo(); tempo > 0.0)
            return tempo;
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

void PluginProcessor::updateStandaloneMidiThruNotes (juce::MidiBuffer& midi, bool enabled)
{
    const bool wasEnabled = std::exchange (standaloneMidiThruWasEnabled, enabled);
    if (enabled)
    {
        for (const auto event : midi)
        {
            const auto status = event.data[0];
            const auto channel = static_cast<size_t> (status & 0x0f);
            if (isNoteOnBytes (event.data, event.numBytes) || isNoteOffBytes (event.data, event.numBytes))
                standaloneMidiThruNotes[channel].set (event.data[1] & 0x7f, isNoteOnBytes (event.data, event.numBytes));
            else if ((status & 0xf0) == 0xb0 && event.numBytes >= 3)
            {
                const int controller = event.data[1];
                if (controller == 64 || controller == 121)
                    standaloneMidiThruSustain.set (channel, controller == 64 && event.data[2] >= 64);
                if (controller == 120 || controller >= 123)
                    standaloneMidiThruNotes[channel].reset();
            }
            else if (status == 0xfa || status == 0xfb || status == 0xfc)
                standaloneMidiThruPlaying = status != 0xfc;
        }
        return;
    }
    if (!wasEnabled)
        return;

    for (size_t channel = 0; channel < standaloneMidiThruNotes.size(); ++channel)
    {
        if (standaloneMidiThruSustain.test (channel))
        {
            const juce::uint8 release[] { static_cast<juce::uint8> (0xb0 | channel), 64, 0 };
            midi.addEvent (release, 3, 0);
            standaloneMidiThruSustain.reset (channel);
        }
        auto& notes = standaloneMidiThruNotes[channel];
        if (notes.none())
            continue;
        for (size_t pitch = 0; pitch < notes.size(); ++pitch)
        {
            if (notes.test (pitch))
            {
                const juce::uint8 off[] { static_cast<juce::uint8> (0x80 | channel), static_cast<juce::uint8> (pitch), 0 };
                midi.addEvent (off, 3, 0);
            }
        }
        notes.reset();
    }
    if (std::exchange (standaloneMidiThruPlaying, false))
    {
        constexpr juce::uint8 stop = 0xfc;
        midi.addEvent (&stop, 1, 0);
    }
}

void PluginProcessor::applyStandaloneMidiThru (juce::MidiBuffer& midi)
{
    if (!hasStandaloneTransport())
        return;
    const bool enabled = isStandaloneMidiThruEnabled();
    if (!enabled)
        midi.clear();
    updateStandaloneMidiThruNotes (midi, enabled);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();

    // Read-only immutable view built on the message thread. No engine access on the audio thread.
    const auto* snapshot = playbackMailbox->current();

    if (snapshot == nullptr)
    {
        applyStandaloneMidiThru (midiMessages);
        sendStandaloneMidiOutput (midiMessages, numSamples);
        return;
    }

    const auto macros = playbackMacrosForBlock (*snapshot);
    const bool playbackReset = snapshot->documentEpoch != playbackDocumentEpoch;
    playbackDocumentEpoch = snapshot->documentEpoch;
    bool standaloneReset = standaloneTransportResetRequested.exchange (0, std::memory_order_acquire) != 0;
    bool stopAllNotes = standaloneStopAllNotesRequested.exchange (0, std::memory_order_acquire) != 0;

    double ppqPosition = 0.0;
    double bpm = snapshot->tempo;
    bool isPlaying = false;
    ksh::HostLoopRange hostLoop;
    ksh::StandaloneSyncBlock sync;

    if (hasStandaloneTransport())
    {
        ppqPosition = standaloneTransportPpqPosition.load (std::memory_order_relaxed);
        bpm = clampStandaloneTempoBpm (snapshot->tempo);
        isPlaying = standaloneTransportPlaying.load (std::memory_order_relaxed) != 0;
        if (standaloneSync != nullptr)
        {
            juce::Optional<juce::uint64> hostTime;
            if (const auto* head = getPlayHead())
                if (const auto position = head->getPosition())
                    hostTime = position->getHostTimeNs();
            const bool wasWaiting = isStandaloneLaunchPending();
            sync = standaloneSync->process (hostTime, recordingSampleRate, numSamples, bpm, isPlaying);
            standaloneLaunchPending.store (sync.waiting, std::memory_order_relaxed);
            if (sync.presentationTimeNs)
            {
                bpm = sync.bpm;
                ppqPosition = sync.ppq;
                standaloneReset = standaloneReset || sync.reset;
                stopAllNotes = stopAllNotes || (isPlaying && !sync.playing && !sync.waiting);
                standaloneTransportPlaying.store (sync.playing, std::memory_order_relaxed);
                if (isPlaying != sync.playing || wasWaiting != sync.waiting)
                    requestMessageThreadWork();
                isPlaying = sync.playing;
            }
            else if (wasWaiting)
            {
                requestMessageThreadWork();
            }
        }
        if (jamSession.load (std::memory_order_acquire) != 0)
            isPlaying = false;
        renderStandaloneMetronome (buffer, ppqPosition, bpm, isPlaying, standaloneReset, sync.firstPlaybackSample);
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
    const bool seeked = std::abs (ppqPosition - expectedRecordingPpq) > clockTolerance;
    if (!recordingClockInitialized || playbackReset || standaloneReset || isPlaying != recordingClockWasPlaying
        || (isPlaying && seeked))
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
    publishPatternRecordTransportPosition (ppqPosition, bpm, isPlaying, hostLoop, sync.presentationTimeNs, sync.firstPlaybackSample);

    // Consume input before adding generated MIDI so our note-offs are never mistaken for pattern selections.
    ksh::MidiPatternSelectionBlock midiPatternSelections;
    const bool filterSelectionNotes = consumeMidiInput (midiMessages, *snapshot, macros, ppqPosition, bpm, isPlaying, midiPatternSelections, hostLoop);

    auto& midiOut = reservedMidiOutputBuffer();
    midiOut.clear();

    const bool standalone = hasStandaloneTransport();
    const bool forwardInput = !standalone || isStandaloneMidiThruEnabled();
    if (forwardInput && !filterSelectionNotes)
    {
        midiOut.data.addArray (midiMessages.data);
    }
    else if (forwardInput)
    {
        for (const auto metadata : midiMessages)
        {
            if ((isNoteOnBytes (metadata.data, metadata.numBytes) || isNoteOffBytes (metadata.data, metadata.numBytes))
                && isMidiPatternSelectionNoteNumber (metadata.data[1]))
                continue;

            midiOut.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
        }
    }
    if (standalone)
        updateStandaloneMidiThruNotes (midiOut, forwardInput);

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
                                                     recordingClockHistory[(recordingClockHistoryIndex - 1) % recordingClockHistory.size()].updatedMs,
                                                     std::nullopt, sync.firstPlaybackSample, &macros);

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
    if (!sync.presentationTimeNs && isPlaying && bpm > 0.0 && std::abs (snapshot->tempo - clampStandaloneTempoBpm (bpm)) > 0.01)
    {
        pendingHostBpm.store (bpm, std::memory_order_relaxed);
        hostBpmChangePending.store (true, std::memory_order_release);
        notify = true;
    }

    bool pushedHit = false;

    for (size_t i = 0; i < playback.noteHitCount; ++i)
        pushedHit = noteHitsForUi.try_enqueue ({ snapshot->documentEpoch, playback.noteHits[i] }) || pushedHit;

    for (size_t i = 0; i < playback.cycleVisitCount; ++i)
        pushedHit = cycleVisitsForUi.try_enqueue ({ snapshot->documentEpoch, playback.cycleVisits[i] }) || pushedHit;

    // Drained by the message-thread timer; posting an OS message from here could block.
    if (pushedHit || notify)
        requestMessageThreadWork();

    sendStandaloneMidiOutput (midiMessages, numSamples, sync.presentationTimeNs);
}

void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    metronomeClickSample = juce::roundToInt (recordingSampleRate * 0.018);
    applyStandaloneMidiThru (midiMessages);
    midiPlayback.suspend (midiMessages);
    currentStepForUi.store (0, std::memory_order_relaxed);
    sendStandaloneMidiOutput (midiMessages, buffer.getNumSamples());
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
                                        const ProcessorPlaybackSnapshot& snapshot,
                                        const ksh::PlaybackMacros& macros,
                                        double ppqPosition,
                                        double bpm,
                                        bool isPlaying,
                                        ksh::MidiPatternSelectionBlock& selections, ksh::HostLoopRange loop)
{
    bool shouldFilter = false;
    bool recordQueued = false;
    const auto recordSession = patternRecordingSession.load (std::memory_order_acquire);
    const auto activeJam = jamSession.load (std::memory_order_acquire);
    const bool timestamped = timestampedStandaloneMidiInput.load (std::memory_order_acquire);
    const double blockTime = activeJam != 0 && !timestamped ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    const bool recordEnabled = recordSession != 0;
    const int strength = getPatternRecordingQuantizeStrength();
    const double recordOffsetBeats = getPatternRecordingOffsetMs() * bpm / 60000.0;
    const int recordSource = ksh::clampInt (patternRecordingSource.load (std::memory_order_acquire),
                                           0,
                                           ksh::Constants::sourceCount - 1);
    auto recordGrid = ksh::patternRecordingGrid (snapshot, recordSource);
    recordGrid.swing = macros.swing;
    if (recordSource == snapshot.staticSource)
        recordGrid.beatsPerStep = macros.beatsPerStep;
    const double sampleRate = recordingSampleRate;
    const double quartersPerSample = bpm > 0.0 ? (bpm / 60.0) / sampleRate : 0.0;

    auto rowForNote = [&snapshot] (int noteNumber) -> int
    {
        for (int channel = 0; channel < snapshot.channelCount; ++channel)
        {
            const auto& row = snapshot.channels[static_cast<size_t> (channel)];
            if (!row.controller && row.note == noteNumber)
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
        if (noteOn && !timestamped)
            flagMidiInputNoteForUi (noteNumber, metadata.data[2]);

        if (activeJam != 0 && !timestamped && noteOn)
            if (const int row = rowForNote (noteNumber); row >= 0)
                enqueueJamEvent (jamAudioEvents, { { blockTime + 1000.0 * metadata.samplePosition / sampleRate, row, metadata.data[2] }, activeJam });

        if (recordEnabled && noteOn && isPlaying && quartersPerSample > 0.0
            && !timestamped)
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

        if (noteOn && activeJam == 0 && !macros.patternSource)
        {
            const int source = sourceForMidiPatternSelectionNoteNumber (noteNumber);
            [[maybe_unused]] const bool queued = pendingMidiPatternSelections.try_enqueue ({ snapshot.documentEpoch, snapshot.patternSelectionRevision, source });
            selections.add (metadata.samplePosition, source);
        }
    }

    if (recordQueued)
        messageThreadWorkPending.store (true, std::memory_order_release);

    return shouldFilter;
}

void PluginProcessor::publishPatternRecordTransportPosition (double ppqPosition, double bpm, bool isPlaying, ksh::HostLoopRange loop,
                                                            juce::Optional<juce::uint64> presentationTimeNs, int firstPlaybackSample)
{
    double timeMs = juce::Time::getMillisecondCounterHiRes();
    if (presentationTimeNs)
    {
        const auto firstSampleMs = firstPlaybackSample * 1000.0 / recordingSampleRate;
        timeMs = static_cast<double> (*presentationTimeNs) / 1.0e6 + firstSampleMs;
        ppqPosition += firstSampleMs * bpm / 60000.0;
    }
    if (!presentationTimeNs || !isPlaying)
        linkRecordingStartMs = 0.0;
    else if (linkRecordingStartMs <= 0.0)
        linkRecordingStartMs = timeMs;

    const PatternRecordTransport anchor { ppqPosition, bpm, timeMs, isPlaying, loop, uiBridge.getDocumentEpoch(),
                                          recordingTransportGeneration, linkRecordingStartMs };
    recordingClockHistory[recordingClockHistoryIndex++ % recordingClockHistory.size()] = anchor;
    patternRecordTransport.publish (anchor);
}

void PluginProcessor::flagMidiInputNoteForUi (int note, int velocity)
{
    if (!juce::isPositiveAndBelow (note, static_cast<int> (midiInputNotesForUi.size())))
        return;
    midiInputNotesForUi[static_cast<size_t> (note)].store (ksh::clampInt (velocity, 1, 127), std::memory_order_relaxed);
    midiInputNotesPending.exchange (true, std::memory_order_acq_rel);
}

void PluginProcessor::captureStandaloneMidiInput (const juce::MidiMessage& message)
{
    if (!message.isNoteOn())
        return;
    flagMidiInputNoteForUi (message.getNoteNumber(), message.getVelocity());
    if (const auto session = jamSession.load (std::memory_order_acquire); session != 0)
    {
        std::scoped_lock lock { standaloneMidiProducerMutex };
        if (jamSession.load (std::memory_order_acquire) != session)
            return;
        const int channels = auditionChannelCount.load (std::memory_order_acquire);
        for (int row = 0; row < channels; ++row)
        {
            if (auditionChannelNotes[static_cast<size_t> (row)].load (std::memory_order_relaxed) != message.getNoteNumber())
                continue;
            const double timestamp = message.getTimeStamp() > 0.0 ? message.getTimeStamp() * 1000.0 : juce::Time::getMillisecondCounterHiRes();
            enqueueJamEvent (jamDriverEvents, { { timestamp, row, message.getVelocity() }, session });
            break;
        }
        return;
    }
    const auto session = patternRecordingSession.load (std::memory_order_acquire);
    if (session == 0 || (hasStandaloneTransport() && (!isStandaloneTransportPlaying() || isStandaloneLaunchPending())))
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

bool PluginProcessor::startJamCapture()
{
    if (!hasStandaloneTransport() || jamSession.load (std::memory_order_acquire) != 0)
        return false;
    setStandaloneTransportPlaying (false);
    setPatternRecordingEnabled (false, 0);
    jamHits.clear();
    jamHits.reserve (ksh::JamTake::maxHits);
    jamArmedMs = juce::Time::getMillisecondCounterHiRes();
    jamSession.store (++nextJamSession, std::memory_order_release);
    drainJamEvents (nextJamSession);
    return true;
}

void PluginProcessor::enqueueJamEvent (moodycamel::ReaderWriterQueue<JamEvent>& queue, JamEvent event)
{
    if (!queue.try_enqueue (event))
        jamOverflowSession.store (event.session, std::memory_order_release);
}

void PluginProcessor::appendJamHit (ksh::JamHit hit, uint64_t session)
{
    if (!std::isfinite (hit.timestampMs) || hit.timestampMs < jamArmedMs)
        return;
    if (jamHits.size() >= ksh::JamTake::maxHits)
        jamOverflowSession.store (session, std::memory_order_release);
    else
        jamHits.push_back (hit);
}

void PluginProcessor::drainJamEvents (uint64_t session)
{
    for (auto* queue : { &jamDriverEvents, &jamAudioEvents })
    {
        JamEvent event;
        while (queue->try_dequeue (event))
            if (session != 0 && event.session == session)
                appendJamHit (event.hit, session);
    }
}

bool PluginProcessor::pollJamCapture()
{
    drainJamEvents (jamSession.load (std::memory_order_acquire));
    return !jamHits.empty();
}

void PluginProcessor::cancelJamCapture()
{
    jamSession.store (0, std::memory_order_release);
    drainJamEvents (0);
    jamHits.clear();
}

std::optional<ksh::JamTake> PluginProcessor::finishJamCapture (double endMs)
{
    uint64_t session = 0;
    {
        std::scoped_lock lock { standaloneMidiProducerMutex };
        session = jamSession.exchange (0, std::memory_order_acq_rel);
        drainJamEvents (session);
    }
    auto hits = std::exchange (jamHits, {});
    if (session == 0 || jamOverflowSession.load (std::memory_order_acquire) == session || hits.empty())
        return std::nullopt;
    const auto tempo = getStandaloneTempoBpm();
    int swing = 0;
    int subdivision = 0;
    {
        std::scoped_lock lock { engineStateMutex };
        applyPendingMacroParametersLocked();
        swing = engine.getSwing();
        subdivision = engine.getSwingSubdivisionIndex();
    }
    return ksh::transcribeJam (std::move (hits), endMs, tempo, swing, subdivision, getPatternRecordingQuantizeStrength());
}

bool PluginProcessor::applyJamTake (const ksh::JamTake& take, int source, uint64_t documentEpoch)
{
    if (!hasStandaloneTransport() || source < 0)
        return false;
    uint32_t changedMacros = 0;
    {
        std::scoped_lock lock { engineStateMutex };
        if (documentEpoch != uiBridge.getDocumentEpoch() || source != engine.getStaticSource())
            return false;
        applyPendingMacroParametersLocked();
        engine.replaceActivePattern (take.pattern, take.steps, take.bpm);
        if (take.swing)
        {
            engine.setSwing (take.swing->amount);
            engine.setSwingSubdivisionIndex (take.swing->subdivision);
        }
        changedMacros = syncMacroParametersFromEngineLocked();
        publishPlaybackSnapshotIfChangedLocked();
        markDocumentModified();
    }
    notifyMacroParameterChanges (changedMacros);
    setStandaloneTempoBpm (take.bpm);
    return true;
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
            const auto& row = snapshot.channels[static_cast<size_t> (channel)];
            if (row.controller || row.note != input.note)
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

uint32_t PluginProcessor::drainPendingMidiPatternSelectionsLocked()
{
    PendingPatternSelection launch {};
    bool switched = false;

    while (pendingMidiPatternSelections.try_dequeue (launch))
    {
        if (launch.documentEpoch != uiBridge.getDocumentEpoch() || launch.revision != patternSelectionRevision
            || engine.getStaticSource() == launch.source)
            continue;
        engine.setStaticSource (launch.source);
        switched = true;
    }

    // The newly selected pattern brings its own rate; show it on the host parameter.
    return switched ? syncMacroParametersFromEngineLocked() : 0;
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
    if (engine.channelAt (channel).controller)
        return;
    const auto before = engine.sourceCellAt (source, channel, step);
    engine.setCell (source, channel, step, ksh::Cell { .enabled = true, .velocity = velocity, .nudge = nudge });
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
        rebase (existing->expected.span, existing->value.span, before.span);
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
    nlohmann::json state;
    {
        std::scoped_lock lock { engineStateMutex };
        if (!unsupportedProjectState.isEmpty())
        {
            destData = unsupportedProjectState;
            return;
        }
        state = persistenceStateLocked (!hasStandaloneTransport());
        if (hasStandaloneTransport() && isDocumentModified())
            state["standaloneDocumentModified"] = true;
    }
    const auto jsonText = state.dump();
    destData.replaceAll (jsonText.data(), jsonText.size());
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    [[maybe_unused]] const bool restored = loadStateFromDataInternal (data, sizeInBytes, ProjectLoad::host);
}

bool PluginProcessor::loadStateFromData (const void* data, int sizeInBytes, bool imported)
{
    return loadStateFromDataInternal (data, sizeInBytes, imported ? ProjectLoad::importedProject : ProjectLoad::project);
}

bool PluginProcessor::canRestorePreviousProject() const
{
    std::scoped_lock lock { engineStateMutex };
    return !previousProject.empty();
}

bool PluginProcessor::restorePreviousProject()
{
    std::string state;
    uint64_t epoch;
    {
        std::scoped_lock lock { engineStateMutex };
        state = previousProject;
        epoch = uiBridge.getDocumentEpoch();
    }
    return loadStateFromDataInternal (state.data(), static_cast<int> (state.size()), ProjectLoad::recovery, epoch);
}

void PluginProcessor::clearUnsupportedProjectStateLocked()
{
    unsupportedProjectState.reset();
    unsupportedProjectVersion.store (false, std::memory_order_release);
}

bool PluginProcessor::loadStateFromDataInternal (const void* data, int sizeInBytes, ProjectLoad load, uint64_t recoveryEpoch)
{
    if (data == nullptr || sizeInBytes <= 0)
        return false;

    const bool isHostState = load == ProjectLoad::host;
    bool unsupportedVersion = false;
    const auto payload = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)), &unsupportedVersion);

    if (! payload.has_value())
    {
        if (isHostState && unsupportedVersion)
        {
            {
                std::scoped_lock lock { engineStateMutex };
                unsupportedProjectState.replaceAll (data, static_cast<size_t> (sizeInBytes));
                unsupportedProjectVersion.store (true, std::memory_order_release);
                previousProject.clear();
            }
            uiBridge.invalidate();
            requestMessageThreadWork();
        }
        return false;
    }
    for (const auto& [key, minimum, maximum] : { std::tuple { "patternRecordQuantizeStrength", 0, 100 },
                                               std::tuple { "patternRecordOffsetMs", -100, 100 } })
    {
        const auto value = payload->find (key);
        if (value != payload->end() && (!value->is_number_integer() || *value < minimum || *value > maximum))
            return false;
    }

    const bool recovering = load == ProjectLoad::recovery;
    const bool restoreHostPlayback = (isHostState || recovering) && !hasStandaloneTransport();
    int hostPattern = 0;
    uint32_t rowMutes = 0;
    std::optional<int> hostRate;
    if (restoreHostPlayback)
    {
        const auto playback = payload->find ("hostPlayback");
        if (playback != payload->end() && playback->is_object())
        {
            const auto read = [&playback] (const char* key, int max, int fallback)
            {
                const auto value = playback->find (key);
                return value != playback->end() && value->is_number_integer() && *value >= 0 && *value <= max
                    ? value->get<int>() : fallback;
            };
            hostPattern = read ("pattern", mutedPatternChoice, 0);
            rowMutes = static_cast<uint32_t> (read ("mutedRows", static_cast<int> (allRowMuteBits), 0));
            const int rate = read ("rate", static_cast<int> (ksh::Constants::rates.size()) - 1, -1);
            if (rate >= 0)
                hostRate = rate;
        }
    }

    uint32_t changedMacros = 0;
    {
        std::scoped_lock lock { engineStateMutex };
        if (recovering && (previousProject.empty() || recoveryEpoch != uiBridge.getDocumentEpoch()))
            return false;
        const bool replacingProject = load == ProjectLoad::project || load == ProjectLoad::importedProject;
        const bool checkpoint = replacingProject && unsupportedProjectState.isEmpty();
        const auto previousState = checkpoint ? persistenceStateLocked (!hasStandaloneTransport()) : engine.serializeForPersistence();
        auto previousProjectText = checkpoint ? previousState.dump() : std::string {};
        const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);
        bool restored = false;

        try
        {
            restored = engine.deserializeForPersistence (*payload);
            if (restored && engine.getChannelCount() < ksh::Constants::defaultChannelCount)
                engine.setChannelCount (ksh::Constants::defaultChannelCount);
        }
        catch (...)
        {
            restored = false;
        }

        if (! restored)
        {
            try
            {
                const auto rollbackSucceeded = engine.deserializeForPersistence (previousState);
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
        acknowledgeHostMacrosLocked();
        changedMacros = syncMacroParametersFromEngineLocked();
        if (restoreHostPlayback)
        {
            static_cast<juce::RangedAudioParameter&> (*hostPatternParameter)
                .setValue (hostPatternParameter->convertTo0to1 (static_cast<float> (hostPattern)));
            for (size_t row = 0; row < hostRowMuteParameters.size(); ++row)
                static_cast<juce::RangedAudioParameter&> (*hostRowMuteParameters[row]).setValue ((rowMutes & (1u << row)) != 0 ? 1.0f : 0.0f);
            restoredHostRate = hostRate;
            const auto zoom = payload->find ("editorZoomPercent");
            const int percent = zoom != payload->end() && zoom->is_number_integer()
                                && *zoom >= ksh::ui::UiScale::minimumPercent && *zoom <= ksh::ui::UiScale::maximumPercent
                                ? zoom->get<int>() : 0;
            projectUiScalePercent.store (percent, std::memory_order_relaxed);
        }
        applyProjectMetadataFromState (*payload);
        previousProject = std::move (previousProjectText);
        clearUnsupportedProjectStateLocked();
        patternRecordingSession.store (0, std::memory_order_release);
        patternRecordingEnabled.store (0, std::memory_order_release);
        patternRecordingEdits.armed = false;
        patternRecordingEdits.cells.clear();
        recordingMonitorSession.store (0, std::memory_order_release);
        uiBridge.documentReplaced();
        publishPlaybackSnapshotLocked();
    }

    if (const auto playing = payload->find ("standaloneTransportPlaying");
        !recovering && playing != payload->end() && (playing->is_boolean() || playing->is_number()))
    {
        const bool shouldPlay = playing->is_boolean() ? playing->get<bool>() : playing->get<int>() != 0;
        if (hasStandaloneTransport())
            setStandaloneTransportPlaying (shouldPlay, false);
    }

    currentStepForUi.store (0, std::memory_order_relaxed);
    const auto recoveredDirty = payload->find ("standaloneDocumentModified");
    if (recovering || load == ProjectLoad::importedProject
        || (isHostState && hasStandaloneTransport() && recoveredDirty != payload->end() && recoveredDirty->is_boolean() && recoveredDirty->get<bool>()))
        markDocumentModified();
    else
        markDocumentClean();
    uiBridge.invalidate();
    requestMessageThreadWork();
    notifyMacroParameterChanges (changedMacros, false);
    return true;
}

nlohmann::json PluginProcessor::enginePersistenceState()
{
    return persistenceState (false);
}

nlohmann::json PluginProcessor::persistenceState (bool includeHostPlayback)
{
    std::scoped_lock lock { engineStateMutex };
    return persistenceStateLocked (includeHostPlayback);
}

nlohmann::json PluginProcessor::persistenceStateLocked (bool includeHostPlayback)
{
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
    if (includeHostPlayback)
    {
        if (const int percent = getUserUiScalePercent(); percent != 0)
            state["editorZoomPercent"] = percent;
        const auto pattern = hostPatternParameter->getIndex();
        const auto mutedRows = hostMutedRows();
        const auto rate = hostRateOverride (hostRateBaseline, restoredHostRate);
        if (pattern != 0 || mutedRows != 0 || rate)
        {
            auto& playback = state["hostPlayback"];
            playback = { { "pattern", pattern }, { "mutedRows", mutedRows } };
            if (rate)
                playback["rate"] = *rate;
        }
    }
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

    std::unique_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);
    const auto previousEngineState = engine.serializeForPersistence();
    const int previousSource = engine.getStaticSource();

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

    if (engine.getStaticSource() != previousSource)
        ++patternSelectionRevision;
    const auto changedMacros = syncMacroParametersFromEngineLocked();
    publishPlaybackSnapshotLocked();
    markDocumentModified();
    lock.unlock();
    notifyMacroParameterChanges (changedMacros);
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

    std::unique_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();

    const bool ok = ksh::dispatchEngineCommand (engine, selector, args);
    if (ok) uiBridge.invalidate();

    if (! ok)
        return false;

    if (selector == "static_source")
        ++patternSelectionRevision;

    if (selector != "reset")
        markDocumentModified();

    const auto changedMacros = (isMacroParameterID (selector) || selector == "source_rate" || selector == "static_source")
                                   ? syncMacroParametersFromEngineLocked() : 0;

    if (selector == "static_source" || engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
        publishPlaybackSnapshotLocked();

    lock.unlock();
    notifyMacroParameterChanges (changedMacros);
    return true;
}

void PluginProcessor::parameterValueChanged (int parameterIndex, float normalizedValue)
{
    const auto match = std::find_if (macroParameters.begin(), macroParameters.end(), [parameterIndex] (const auto* parameter)
    {
        return parameter != nullptr && parameter->getParameterIndex() == parameterIndex;
    });
    if (match == macroParameters.end())
        return;
    const size_t index = static_cast<size_t> (match - macroParameters.begin());
    const uint32_t parameterBit = 1u << index;
    if (notifyingMacro.processor == this && notifyingMacro.bit == parameterBit)
        return;

    const auto maximum = parameterBit == rateBit ? static_cast<int> (ksh::Constants::rates.size()) - 1
                         : parameterBit == deviceActiveBit ? 1 : 100;
    const float newValue = (*match)->convertFrom0to1 (normalizedValue);
    const auto value = static_cast<uint64_t> (std::lround (juce::jlimit (0.0f, static_cast<float> (maximum), newValue)));
    auto& change = hostMacroChanges[index];
    auto previous = change.load (std::memory_order_relaxed);
    while (!change.compare_exchange_weak (previous, ((previous & ~macroValueMask) + macroGenerationIncrement) | value,
                                           std::memory_order_release, std::memory_order_relaxed))
    {
    }
    requestMessageThreadWork();
}

void PluginProcessor::addMacroParameterListeners()
{
    for (size_t i = 0; i < macroParameters.size(); ++i)
        macroParameters[i] = parameters.getParameter (kMacroParameterIDs[i].data());
    for (auto* parameter : macroParameters)
        parameter->addListener (this);
}

void PluginProcessor::removeMacroParameterListeners()
{
    for (auto* parameter : macroParameters)
        parameter->removeListener (this);
}

uint32_t PluginProcessor::syncMacroParametersFromEngineLocked()
{
    const std::array values { static_cast<float> (rateIndexForValue (engine.getRate())),
                              static_cast<float> (engine.getSwing()), static_cast<float> (engine.getVelocityHumanize()),
                              static_cast<float> (engine.getTimingHumanize()), engine.isDeviceActive() ? 1.0f : 0.0f };
    uint32_t changed = 0;
    for (size_t i = 0; i < kMacroParameterIDs.size(); ++i)
    {
        auto* parameter = parameters.getParameter (kMacroParameterIDs[i].data());
        const float normalized = parameter->convertTo0to1 (values[i]);
        if (std::abs (parameter->getValue() - normalized) >= 0.000001f)
        {
            // The parameter's atomic value is safe here. Its listeners can call back
            // into the processor, so notify them only after releasing engineStateMutex.
            parameter->setValue (normalized);
            changed |= 1u << i;
        }
    }
    return changed;
}

void PluginProcessor::notifyMacroParameterChanges (uint32_t changed, bool useGestures)
{
    for (size_t i = 0; i < kMacroParameterIDs.size() && changed != 0; ++i)
    {
        const uint32_t bit = 1u << i;
        if ((changed & bit) == 0)
            continue;
        changed &= ~bit;
        auto* parameter = parameters.getParameter (kMacroParameterIDs[i].data());
        const bool beginGesture = useGestures && (uiGestureParameters.load (std::memory_order_acquire) & bit) == 0;
        if (beginGesture)
            parameter->beginChangeGesture();
        {
            const juce::ScopedValueSetter notifyScope { notifyingMacro, MacroNotification { this, bit } };
            parameter->sendValueChangedMessageToListeners (parameter->getValue());
        }
        if (beginGesture)
            parameter->endChangeGesture();
    }
}

void PluginProcessor::setProjectUiScalePercent (const int uiScalePercent)
{
    const auto next = juce::jlimit (ksh::ui::UiScale::minimumPercent, ksh::ui::UiScale::maximumPercent, uiScalePercent);
    if (projectUiScalePercent.exchange (next, std::memory_order_relaxed) != next)
    {
        uiBridge.invalidate();
        if (!hasStandaloneTransport())
        {
            hostStateChangePending.store (true, std::memory_order_release);
            requestMessageThreadWork();
        }
    }
}

void PluginProcessor::setPatternRecordingEnabled (bool shouldRecord, int source)
{
    if (shouldRecord && jamSession.load (std::memory_order_acquire) != 0)
        return;
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

bool PluginProcessor::queueRowAudition (int channelZeroBased, int velocity, double timestampMs)
{
    if (auditionDeviceActive.load (std::memory_order_acquire) == 0)
        return false;

    const int channelCount = auditionChannelCount.load (std::memory_order_acquire);

    if (channelZeroBased < 0 || channelZeroBased >= channelCount)
        return false;

    const int pitch = auditionChannelNotes[static_cast<size_t> (channelZeroBased)].load (std::memory_order_acquire);
    if (pitch < 0)
        return false;
    midiPlayback.queueAuditionNote ({ pitch,
                                      ksh::clampInt (velocity, 1, 127),
                                      ksh::Constants::defaultMidiChannel,
                                      ksh::Constants::defaultNoteDurationMs,
                                      0.0, timestampMs > 0.0 ? timestampMs : juce::Time::getMillisecondCounterHiRes() });
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

bool PluginProcessor::PatternRecordTransport::containsTime (double timestampMs) const
{
    // The Link anchor is at hardware output, which can be ahead of a pad hit.
    // Project backward from it only within this playback run.
    const auto startMs = linkStartMs > 0.0 ? linkStartMs : updatedMs;
    return updatedMs > 0.0 && timestampMs >= startMs && std::abs (timestampMs - updatedMs) <= kMaxPatternRecordTransportAgeMs;
}

float PluginProcessor::getRecordingPulseForUi() const
{
    const auto* transport = patternRecordTransport.current();
    if (!isPatternRecordingEnabled() || transport == nullptr || !transport->playing)
        return 1.0f;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (!transport->containsTime (now))
        return 1.0f;
    const auto ageMs = now - transport->updatedMs;
    const double beat = transport->ppq + ageMs * transport->bpm / 60000.0;
    const double phase = beat - std::floor (beat);
    return static_cast<float> (0.35 + 0.65 * std::exp (-6.0 * phase));
}

std::optional<ksh::PatternRecordingPosition> PluginProcessor::positionForUiTrigger (int source, int channel, double timestampMs, uint64_t* generation) const
{
    const auto* transport = patternRecordTransport.current();
    const auto* grids = patternRecordGrids.current();
    if (transport == nullptr || grids == nullptr || !transport->playing || transport->documentEpoch != uiBridge.getDocumentEpoch()
        || (hasStandaloneTransport() && (!isStandaloneTransportPlaying() || isStandaloneLaunchPending())))
        return std::nullopt;
    const auto eventTime = timestampMs > 0.0 ? timestampMs : juce::Time::getMillisecondCounterHiRes();
    if (!transport->containsTime (eventTime))
        return std::nullopt;
    const double ageMs = eventTime - transport->updatedMs;
    if (generation != nullptr)
        *generation = transport->transportGeneration;
    const double beat = recordingBeat (transport->ppq + ageMs * transport->bpm / 60000.0,
                                       getPatternRecordingOffsetMs() * transport->bpm / 60000.0, transport->loop);
    return ksh::patternRecordingPosition ((*grids)[static_cast<size_t> (source)], channel, beat, getPatternRecordingQuantizeStrength());
}

bool PluginProcessor::triggerRowFromUi (int channelZeroBased, int velocity, double timestampMs)
{
    const int vel = ksh::clampInt (velocity, 1, 127);
    if (timestampMs <= 0.0)
        timestampMs = juce::Time::getMillisecondCounterHiRes();
    const bool auditioned = queueRowAudition (channelZeroBased, vel, timestampMs);
    const int channelCount = auditionChannelCount.load (std::memory_order_acquire);
    if (channelZeroBased < 0 || channelZeroBased >= channelCount
        || auditionChannelNotes[static_cast<size_t> (channelZeroBased)].load (std::memory_order_acquire) < 0)
        return auditioned;

    if (const auto session = jamSession.load (std::memory_order_acquire); session != 0)
    {
        appendJamHit ({ timestampMs, channelZeroBased, vel }, session);
        return true;
    }

    if (! isPatternRecordingEnabled())
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
    if (engine.channelAt (row).controller || !patternRecordingEdits.armed || patternRecordingEdits.documentEpoch != uiBridge.getDocumentEpoch())
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
   #if JUCE_MAC
    macProjectFiles.releaseAccessUnless (file);
   #endif
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
    uint32_t changedMacros = 0;
    {
        std::scoped_lock lock { engineStateMutex };
        auto checkpoint = unsupportedProjectState.isEmpty() ? persistenceStateLocked (!hasStandaloneTransport()).dump() : std::string {};
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
        previousProject = std::move (checkpoint);
        clearUnsupportedProjectStateLocked();
        patternRecordingQuantizeStrength.store (100, std::memory_order_release);
        patternRecordingOffsetMs.store (0, std::memory_order_release);
        acknowledgeHostMacrosLocked();
        changedMacros = syncMacroParametersFromEngineLocked();
        patternRecordingSession.store (0, std::memory_order_release);
        patternRecordingEnabled.store (0, std::memory_order_release);
        patternRecordingEdits.armed = false;
        patternRecordingEdits.cells.clear();
        recordingMonitorSession.store (0, std::memory_order_release);
        uiBridge.documentReplaced();
        publishPlaybackSnapshotLocked();
    }

    setProjectMetadata ("Untitled Project", {}, {}, {}, juce::String {});
    setProjectFile ({});
    setStandaloneTransportPlaying (false, false);
    standaloneTransportPpqPosition.store (0.0, std::memory_order_relaxed);
    standaloneStopAllNotesRequested.store (1, std::memory_order_release);
    currentStepForUi.store (0, std::memory_order_relaxed);
    markDocumentClean();
    uiBridge.invalidate();
    requestMessageThreadWork();
    notifyMacroParameterChanges (changedMacros, false);
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
    ksh::DocumentState document;
    copyEditorDocument (document);
    return document;
}

void PluginProcessor::copyEditorDocument (ksh::DocumentState& document)
{
    std::scoped_lock lock { engineStateMutex };
    applyPendingMacroParametersLocked();
    engine.fillDocumentState (document);
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
    using ksh::Constants;
    if (edits.size() > Constants::sourceCount * Constants::cellsPerPattern) return false;
    std::scoped_lock lock { engineStateMutex };
    if (epoch != uiBridge.getDocumentEpoch()) return false;
    for (const auto& edit : edits)
    {
        if (edit.source < 0 || edit.source >= Constants::sourceCount || edit.channel < 0 || edit.channel >= Constants::maxChannels
            || edit.step < 0 || edit.step >= Constants::maxSteps
            || engine.sourceCellAt (edit.source, edit.channel, edit.step) != edit.expected)
            return false;
    }
    for (const auto& edit : edits)
        engine.setCell (edit.source, edit.channel, edit.step, edit.value);
    publishPlaybackSnapshotIfChangedLocked();
    uiBridge.invalidate();
    markDocumentModified();
    return true;
}
