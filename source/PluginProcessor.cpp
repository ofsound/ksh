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
    for (const auto& hit : pendingNoteHitsForUi)
        uiBridge.emitNoteHit (hit);

    pendingNoteHitsForUi.clear();
    uiBridge.pollTransportUi();
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
    const juce::ScopedLock lock (engineLock);
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();
    double ppqPosition = 0.0;
    double bpm = engine.tempo;
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

    const auto playback = midiPlayback.processBlock (engine, ppqPosition, bpm, isPlaying, numSamples);
    midiMessages.addEvents (playback.midi, 0, numSamples, 0);

    if (! playback.noteHits.empty())
    {
        recentNoteHits = playback.noteHits;
        pendingNoteHitsForUi = playback.noteHits;
        triggerAsyncUpdate();
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
    const auto jsonText = engine.serializeForPersistence().dump();
    destData.replaceAll (jsonText.data(), jsonText.size());
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const juce::ScopedLock lock (engineLock);

    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto payload = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));

    if (! payload.has_value())
        return;

    if (! engine.deserializeForPersistence (*payload))
        return;

    recentNoteHits.clear();
    pendingNoteHitsForUi.clear();
    midiPlayback.reset();
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
