#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KshUiBridge.h"

#include "engine/KshPersistence.h"

PluginProcessor::BusesProperties PluginProcessor::createBusesProperties()
{
  #if JucePlugin_IsMidiEffect
    return BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true);
  #else
    return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
  #endif
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
       uiBridge (*this),
       engine (makeEngineCallbacks())
{
    initializeDefaultPattern();
    publishPlaybackSnapshot();
}

void PluginProcessor::publishPlaybackSnapshot()
{
    playbackMailbox.publish (engine.makePlaybackSnapshot());
    lastPublishedSnapshotVersion = engine.playbackSnapshotVersion();
}

ksh::EngineCallbacks PluginProcessor::makeEngineCallbacks()
{
    ksh::EngineCallbacks callbacks;

    callbacks.emitPreview = [this] (const nlohmann::json& preview)
    {
        uiBridge.emitPreview (preview);
    };

    callbacks.emitStatus = [this] (const std::string& message)
    {
        uiBridge.emitStatus (message);
    };

    callbacks.emitNote = [this] (const ksh::MidiNoteEvent& note)
    {
        midiPlayback.queueAuditionNote (note);
    };

    return callbacks;
}

void PluginProcessor::handleAsyncUpdate()
{
    // Everything here runs on the message thread, the sole owner of the engine. The audio thread
    // only reads published snapshots, so no lock is required.

    // Host tempo changes (mutating the engine + emitting UI status are not realtime-safe).
    if (hostBpmChangePending.exchange (false, std::memory_order_acquire))
    {
        const auto bpm = pendingHostBpm.load (std::memory_order_relaxed);

        if (bpm > 0.0 && std::abs (engine.tempo - bpm) > 0.01)
            engine.setTempo (bpm);
    }

    // Advance generative regeneration from the transport position reported by the audio thread.
    if (transportReportPending.exchange (false, std::memory_order_acquire))
    {
        const auto ppq = reportedPpq.load (std::memory_order_relaxed);
        const auto playing = reportedPlaying.load (std::memory_order_relaxed);
        engine.transportPosition (ppq, playing);
    }

    // Hand the audio thread a fresh snapshot only when the playback table actually changed.
    if (engine.playbackSnapshotVersion() != lastPublishedSnapshotVersion)
        publishPlaybackSnapshot();

    playbackMailbox.drainRetired();

    ksh::NativeHit hit;

    while (noteHitsForUi.try_dequeue (hit))
        uiBridge.emitNoteHit (hit);
}

PluginProcessor::~PluginProcessor() = default;

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
    const auto jsonText = engine.serializeForPersistence().dump();
    destData.replaceAll (jsonText.data(), jsonText.size());
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Called on the message thread, the sole engine owner.
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto payload = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));

    if (! payload.has_value())
        return;

    if (! engine.deserializeForPersistence (*payload))
        return;

    ksh::NativeHit drained;

    while (noteHitsForUi.try_dequeue (drained))
    {
    }

    currentStepForUi.store (0, std::memory_order_relaxed);
    requestPlaybackReset();
    publishPlaybackSnapshot();
    uiBridge.syncAll();
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
