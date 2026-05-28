#pragma once

#include "KshUiBridge.h"
#include "engine/KickSnareHatEngine.h"
#include "engine/KshMidiPlayback.h"

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

#if (MSVC)
#include "ipps.h"
#endif

class PluginProcessor : public juce::AudioProcessor,
                        private juce::AsyncUpdater
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    ksh::KickSnareHatEngine& getEngine() { return engine; }
    const ksh::KickSnareHatEngine& getEngine() const { return engine; }

    ksh::MidiPlaybackRunner& getMidiPlayback() { return midiPlayback; }

    KshUiBridge& getUiBridge() { return uiBridge; }
    const KshUiBridge& getUiBridge() const { return uiBridge; }

    using EditorResizeCallback = std::function<void (int, int)>;
    void setEditorResizeCallback (EditorResizeCallback callback);
    void requestEditorSize (int width, int height);

    const std::vector<ksh::NativeHit>& getRecentNoteHits() const { return recentNoteHits; }

private:
    static BusesProperties createBusesProperties();
    void initializeDefaultPattern();
    ksh::EngineCallbacks makeEngineCallbacks();
    void handleAsyncUpdate() override;

    KshUiBridge uiBridge;
    ksh::KickSnareHatEngine engine;
    ksh::MidiPlaybackRunner midiPlayback;
    std::vector<ksh::NativeHit> recentNoteHits;
    std::vector<ksh::NativeHit> pendingNoteHitsForUi;
    EditorResizeCallback editorResizeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
