#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KshUiBridge.h"

#include "engine/KshPersistence.h"
#include "engine/KshEngineCommands.h"

#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 6> kMacroParameterIDs {{
    "rate",
    "swing",
    "velocity_humanize",
    "timing_humanize",
    "device_active",
    "phase_offset_beats"
}};

bool isMacroParameterID (std::string_view id)
{
    for (const auto macroID : kMacroParameterIDs)
    {
        if (macroID == id)
            return true;
    }

    return false;
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
                        bool notifyHost)
{
    auto* parameter = parameters.getParameter (id);

    if (parameter == nullptr)
        return;

    const auto normalized = parameter->convertTo0to1 (plainValue);

    if (std::abs (parameter->getValue() - normalized) < 0.000001f)
        return;

    if (notifyHost)
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (normalized);
        parameter->endChangeGesture();
        return;
    }

    parameter->setValue (normalized);
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

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { "phase_offset_beats", 1 },
        "Phase Offset",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.0001f },
        0.0f));

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
    engine.setCell (0, 0, 0, true, 100, 100, 1);
}

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (createBusesProperties()),
       parameters (*this, nullptr, "KSH_PARAMETERS", createParameterLayout()),
       uiBridge (*this),
       engine (makeEngineCallbacks())
{
    initializeDefaultPattern();
    syncMacroParametersFromEngineLocked (false);
    addMacroParameterListeners();
    publishPlaybackSnapshot();
}

void PluginProcessor::publishPlaybackSnapshot()
{
    std::scoped_lock lock { engineMutex };
    publishPlaybackSnapshotLocked();
}

void PluginProcessor::publishPlaybackSnapshotLocked()
{
    playbackMailbox.publish (engine.makePlaybackSnapshot());
    lastPublishedSnapshotVersion = engine.playbackSnapshotVersion();
}

ksh::EngineCallbacks PluginProcessor::makeEngineCallbacks()
{
    ksh::EngineCallbacks callbacks;

    callbacks.emitPreview = [this] (const nlohmann::json& preview)
    {
        if (suppressEngineCallbacks.load (std::memory_order_relaxed))
            return;

        uiBridge.emitPreview (preview);
    };

    callbacks.emitStatus = [this] (const std::string& message)
    {
        if (suppressEngineCallbacks.load (std::memory_order_relaxed))
            return;

        uiBridge.emitStatus (message);
    };

    callbacks.emitNote = [this] (const ksh::MidiNoteEvent& note)
    {
        if (suppressEngineCallbacks.load (std::memory_order_relaxed))
            return;

        midiPlayback.queueAuditionNote (note);
    };

    return callbacks;
}

void PluginProcessor::handleAsyncUpdate()
{
    {
        std::scoped_lock lock { engineMutex };

        if (macroParametersDirty.exchange (false, std::memory_order_acquire))
            applyMacroParametersToEngineLocked();

        // Host tempo changes (mutating the engine + emitting UI status are not realtime-safe).
        if (hostBpmChangePending.exchange (false, std::memory_order_acquire))
        {
            const auto bpm = pendingHostBpm.load (std::memory_order_relaxed);

            if (bpm > 0.0 && std::abs (engine.getTempo() - bpm) > 0.01)
                engine.setTempo (bpm);
        }

        // Advance generative regeneration from the transport position reported by the audio thread.
        if (transportReportPending.exchange (false, std::memory_order_acquire))
        {
            const auto ppq = reportedPpq.load (std::memory_order_relaxed);
            const auto playing = reportedPlaying.load (std::memory_order_relaxed);
            engine.transportPosition (ppq, playing);
        }

        // Hand the audio thread a fresh snapshot only when playback-affecting state changed.
        if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
            publishPlaybackSnapshotLocked();
    }

    playbackMailbox.drainRetired();

    ksh::NativeHit hit;

    while (noteHitsForUi.try_dequeue (hit))
        uiBridge.emitNoteHit (hit);

    if (fullUiSyncPending.exchange (false, std::memory_order_acquire))
        uiBridge.syncAll();
}

PluginProcessor::~PluginProcessor()
{
    removeMacroParameterListeners();
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
    midiPlayback.prepare (sampleRate);
    publishPlaybackSnapshot();
}

void PluginProcessor::releaseResources()
{
    midiPlayback.reset();
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

    if (playbackResetRequested.exchange (false, std::memory_order_acquire))
        midiPlayback.reset();

    double ppqPosition = 0.0;
    double bpm = snapshot->tempo;
    bool isPlaying = false;

    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto ppq = position->getPpqPosition())
                ppqPosition = *ppq;

            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            isPlaying = position->getIsPlaying();
        }
    }

    const auto playback = midiPlayback.processBlock (*snapshot, ppqPosition, bpm, isPlaying, numSamples, midiMessages);

    currentStepForUi.store (playback.currentStepOneBased, std::memory_order_relaxed);

    bool notify = false;

    // Wake the message thread to advance generation on step changes and play/stop transitions.
    const bool stepChanged = playback.currentStepOneBased != lastReportedStepForRegen;
    lastReportedStepForRegen = playback.currentStepOneBased;

    if (playback.playing != wasPlayingForRegen || (playback.playing && stepChanged))
    {
        reportedPpq.store (ppqPosition, std::memory_order_relaxed);
        reportedPlaying.store (playback.playing, std::memory_order_relaxed);
        transportReportPending.store (true, std::memory_order_release);
        notify = true;
    }

    wasPlayingForRegen = playback.playing;

    // Defer host tempo changes to the message thread (engine mutation + UI emit are not RT-safe).
    if (isPlaying && bpm > 0.0 && std::abs (snapshot->tempo - bpm) > 0.01)
    {
        pendingHostBpm.store (bpm, std::memory_order_relaxed);
        hostBpmChangePending.store (true, std::memory_order_release);
        notify = true;
    }

    bool pushedHit = false;

    for (size_t i = 0; i < playback.noteHitCount; ++i)
        pushedHit = noteHitsForUi.try_enqueue (playback.noteHits[i]) || pushedHit;

    if (pushedHit || notify)
        triggerAsyncUpdate();
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
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto payload = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));

    if (! payload.has_value())
        return;

    {
        std::scoped_lock lock { engineMutex };
        const auto previousSuppression = suppressEngineCallbacks.exchange (true, std::memory_order_acq_rel);

        try
        {
            if (! engine.deserializeForPersistence (*payload))
            {
                suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);
                return;
            }
        }
        catch (...)
        {
            suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);
            return;
        }

        suppressEngineCallbacks.store (previousSuppression, std::memory_order_release);
        syncMacroParametersFromEngineLocked (false);
        publishPlaybackSnapshotLocked();
    }

    ksh::NativeHit drained;

    while (noteHitsForUi.try_dequeue (drained))
    {
    }

    currentStepForUi.store (0, std::memory_order_relaxed);
    requestPlaybackReset();
    fullUiSyncPending.store (true, std::memory_order_release);
    triggerAsyncUpdate();
}

nlohmann::json PluginProcessor::enginePersistenceState()
{
    std::scoped_lock lock { engineMutex };

    if (macroParametersDirty.exchange (false, std::memory_order_acquire))
    {
        applyMacroParametersToEngineLocked();

        if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
            publishPlaybackSnapshotLocked();
    }

    return engine.serializeForPersistence();
}

nlohmann::json PluginProcessor::enginePreviewState()
{
    std::scoped_lock lock { engineMutex };

    if (macroParametersDirty.exchange (false, std::memory_order_acquire))
    {
        applyMacroParametersToEngineLocked();

        if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
            publishPlaybackSnapshotLocked();
    }

    return engine.snapshot();
}

bool PluginProcessor::dispatchUiEngineCommand (std::string_view selector, const nlohmann::json& args)
{
    std::scoped_lock lock { engineMutex };

    const bool ok = ksh::dispatchEngineCommand (engine, selector, args);

    if (! ok)
        return false;

    if (selector != "channel_audition")
    {
        if (isMacroParameterID (selector))
            syncMacroParametersFromEngineLocked (true);

        requestPlaybackReset();
        publishPlaybackSnapshotLocked();
    }

    return true;
}

void PluginProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused (newValue);

    if (suppressParameterCallbacks.load (std::memory_order_relaxed))
        return;

    if (! isMacroParameterID (parameterID.toStdString()))
        return;

    macroParametersDirty.store (true, std::memory_order_release);
    triggerAsyncUpdate();
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

void PluginProcessor::applyMacroParametersToEngineLocked()
{
    const int rateIndex = ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "rate"))),
                                        0,
                                        static_cast<int> (ksh::Constants::rates.size()) - 1);
    const auto rate = ksh::Constants::rates[static_cast<size_t> (rateIndex)];

    if (engine.getRate() != rate)
        engine.setRate (rate);

    const auto swing = ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "swing"))), 0, 100);

    if (engine.getSwing() != swing)
        engine.setSwing (swing);

    const auto velocityHumanize =
        ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "velocity_humanize"))), 0, 100);

    if (engine.getVelocityHumanize() != velocityHumanize)
        engine.setVelocityHumanize (velocityHumanize);

    const auto timingHumanize =
        ksh::clampInt (static_cast<int> (std::lround (parameterValue (parameters, "timing_humanize"))), 0, 100);

    if (engine.getTimingHumanize() != timingHumanize)
        engine.setTimingHumanize (timingHumanize);

    const bool deviceActive = parameterValue (parameters, "device_active") >= 0.5f;

    if (engine.isDeviceActive() != deviceActive)
        engine.setDeviceActive (deviceActive);

    const auto phaseOffsetBeats = static_cast<double> (parameterValue (parameters, "phase_offset_beats"));

    if (std::abs (engine.getPhaseOffsetBeats() - phaseOffsetBeats) > 0.000001)
        engine.setPhaseOffsetBeats (phaseOffsetBeats);
}

void PluginProcessor::syncMacroParametersFromEngineLocked (bool notifyHost)
{
    const auto previousSuppression = suppressParameterCallbacks.exchange (true, std::memory_order_acq_rel);

    setParameterValue (parameters, "rate", static_cast<float> (rateIndexForValue (engine.getRate())), notifyHost);
    setParameterValue (parameters, "swing", static_cast<float> (engine.getSwing()), notifyHost);
    setParameterValue (parameters, "velocity_humanize", static_cast<float> (engine.getVelocityHumanize()), notifyHost);
    setParameterValue (parameters, "timing_humanize", static_cast<float> (engine.getTimingHumanize()), notifyHost);
    setParameterValue (parameters, "device_active", engine.isDeviceActive() ? 1.0f : 0.0f, notifyHost);
    setParameterValue (parameters, "phase_offset_beats", static_cast<float> (engine.getPhaseOffsetBeats()), notifyHost);

    suppressParameterCallbacks.store (previousSuppression, std::memory_order_release);
}

void PluginProcessor::setEditorResizeCallback (EditorResizeCallback callback)
{
    editorResizeCallback = std::move (callback);
}

void PluginProcessor::requestEditorSize (int width, int height)
{
    if (editorResizeCallback == nullptr)
        return;

    juce::MessageManager::callAsync ([callback = editorResizeCallback, width, height]
                                     {
                                         callback (width, height);
                                     });
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
