#pragma once

#include "KshUiBridge.h"
#include "RealtimeMailbox.h"
#include "engine/KickSnareHatEngine.h"
#include "engine/KshMidiPlayback.h"

#include <atomic>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <readerwriterqueue.h>
#include <string_view>

#if (MSVC)
#include "ipps.h"
#endif

class PluginProcessor : public juce::AudioProcessor,
                        private juce::AsyncUpdater,
                        private juce::Timer,
                        private juce::AudioProcessorValueTreeState::Listener
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

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const { return parameters; }

    KshUiBridge& getUiBridge() { return uiBridge; }
    const KshUiBridge& getUiBridge() const { return uiBridge; }

    [[nodiscard]] nlohmann::json enginePersistenceState();
    [[nodiscard]] nlohmann::json enginePreviewState();
    [[nodiscard]] ksh::EngineStateSnapshot engineStateSnapshot();
    [[nodiscard]] ksh::PlaybackSnapshot enginePlaybackSnapshot();
    [[nodiscard]] bool dispatchUiEngineCommand (std::string_view selector, const nlohmann::json& args);
    [[nodiscard]] bool applyPersistenceFromUi (const nlohmann::json& state);

    using EditorResizeCallback = std::function<void (int, int)>;
    void setEditorResizeCallback (EditorResizeCallback callback);
    void requestEditorSize (int width, int height);

    /** Latest playing step (1-based, 0 = stopped). Published by the audio thread. */
    int getCurrentStepForUi() const { return currentStepForUi.load (std::memory_order_relaxed); }
    bool hasStandaloneTransport() const;
    void setStandaloneTransportPlaying (bool shouldPlay);
    bool isStandaloneTransportPlaying() const;
    void setStandaloneTempoBpm (double bpm);
    double getStandaloneTempoBpm();

    void setPatternViewScale (double scale);
    [[nodiscard]] double getPatternViewScale() const { return patternViewScale; }

private:
    static BusesProperties createBusesProperties();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void initializeDefaultPattern();
    ksh::EngineCallbacks makeEngineCallbacks();
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void publishPlaybackSnapshot();
    void publishPlaybackSnapshotLocked();
    void requestPlaybackReset() { playbackResetRequested.store (true, std::memory_order_release); }
    void applyPendingMacroParametersLocked();
    void publishPlaybackSnapshotIfChangedLocked();
    void addMacroParameterListeners();
    void removeMacroParameterListeners();
    void applyMacroParametersToEngineLocked();
    void syncMacroParametersFromEngineLocked (bool notifyHost);
    ksh::MidiPatternSelectionBlock consumeMidiPatternSelectionInput (juce::MidiBuffer& midiMessages);
    void drainPendingMidiPatternSelectionsLocked();

    juce::AudioProcessorValueTreeState parameters;
    KshUiBridge uiBridge;
    ksh::KickSnareHatEngine engine;
    ksh::MidiPlaybackRunner midiPlayback;

    // Protects message-thread/host-thread engine mutation. The audio thread never takes this lock.
    mutable std::mutex engineStateMutex;
    std::atomic<bool> suppressEngineCallbacks { false };

    // Message thread builds the engine + snapshot; the audio thread only reads the published snapshot.
    RealtimeMailbox<ksh::PlaybackSnapshot> playbackMailbox;
    unsigned long lastPublishedSnapshotVersion = 0; // message-thread only

    // Audio -> message-thread handoff (drained in handleAsyncUpdate / polled by the editor).
    moodycamel::ReaderWriterQueue<ksh::NativeHit> noteHitsForUi { 1024 };
    moodycamel::ReaderWriterQueue<int> pendingMidiPatternSelections { 128 };
    std::atomic<int> currentStepForUi { 0 };
    std::atomic<double> pendingHostBpm { 0.0 };
    std::atomic<bool> hostBpmChangePending { false };

    // Audio thread reports its transport position so the message thread can advance generation.
    std::atomic<double> reportedPpq { 0.0 };
    std::atomic<bool> reportedPlaying { false };
    std::atomic<bool> transportReportPending { false };
    std::atomic<bool> messageThreadWorkPending { false };
    std::atomic<bool> playbackResetRequested { false };
    std::atomic<bool> fullUiSyncPending { false };
    std::atomic<bool> macroParametersDirty { false };
    std::atomic<bool> suppressParameterCallbacks { false };
    std::atomic<int> standaloneTransportPlaying { 0 };
    std::atomic<double> standaloneTransportPpqPosition { 0.0 };
    std::atomic<int> standaloneTransportResetRequested { 0 };
    std::atomic<int> standaloneStopAllNotesRequested { 0 };

    // Audio-thread-only state for deciding when to wake the message thread.
    int lastReportedStepForRegen = 0;
    bool wasPlayingForRegen = false;
    juce::MidiBuffer midiInputScratch;

    EditorResizeCallback editorResizeCallback;

    // Editor-only UI preference; not written to host project state.
    double patternViewScale = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
