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
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    /** Same as setStateInformation, but reports whether the project was actually restored. */
    [[nodiscard]] bool loadStateFromData (const void* data, int sizeInBytes);

    /** True after an in-app edit since the last load, save, or new project. */
    [[nodiscard]] bool isDocumentModified() const { return documentModified.load (std::memory_order_acquire); }
    void markDocumentClean() { documentModified.store (false, std::memory_order_release); }

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const { return parameters; }

    KshUiBridge& getUiBridge() { return uiBridge; }
    const KshUiBridge& getUiBridge() const { return uiBridge; }

    [[nodiscard]] nlohmann::json enginePersistenceState();
    [[nodiscard]] nlohmann::json enginePreviewState();
    [[nodiscard]] ksh::EngineStateSnapshot engineStateSnapshot();
    [[nodiscard]] ksh::PlaybackSnapshot enginePlaybackSnapshot();
    struct UiState
    {
        ksh::PlaybackSnapshot playback;
        int refreshSteps = 16;
        uint64_t revision = 0, documentEpoch = 0;
    };
    struct CellEdit
    {
        int source = 0, channel = 0, step = 0;
        ksh::Cell expected, value;
    };
    [[nodiscard]] UiState nativeUiState();
    /** nativeUiState() written into existing storage, avoiding a second snapshot copy. */
    void copyNativeUiState (UiState& state);
    [[nodiscard]] bool applyNativeCellEdits (const std::vector<CellEdit>& edits, uint64_t epoch);
    [[nodiscard]] bool dispatchUiEngineCommand (std::string_view selector, const nlohmann::json& args);
    [[nodiscard]] bool applyPersistenceFromUi (const nlohmann::json& state);
    /** Typed project document for undo/redo; applying keeps the current generated choices. */
    [[nodiscard]] ksh::DocumentState editorDocument();
    [[nodiscard]] bool applyEditorDocument (const ksh::DocumentState& document);

    /** UI-owned host gesture. Engine-driven parameter writes inside it do not open nested gestures. */
    void beginUiParameterGesture (const juce::String& parameterID);
    void endUiParameterGesture (const juce::String& parameterID);

    /** Low-latency QWERTY / pad trigger: queues MIDI audition without taking the engine lock.
        When pattern recording is armed, also enqueues a quantized cell write. */
    [[nodiscard]] bool triggerRowFromUi (int channelZeroBased, int velocity = 100);

    /** Latest playing step (1-based, 0 = stopped). Published by the audio thread. */
    int getCurrentStepForUi() const { return currentStepForUi.load (std::memory_order_relaxed); }
    int getGlobalStepForUi() const { return globalStepForUi.load (std::memory_order_relaxed); }
    /** Drain queued note-hit flashes to the native editor (safe on the message thread). */
    int emitPendingNoteHitsForUi();
    bool hasStandaloneTransport() const;
    void setStandaloneTransportPlaying (bool shouldPlay);
    bool isStandaloneTransportPlaying() const;
    void setStandaloneTempoBpm (double bpm);
    double getStandaloneTempoBpm();

    int getProjectUiScalePercent() const noexcept { return projectUiScalePercent; }
    void setProjectUiScalePercent (int uiScalePercent);
    void setPatternRecordingEnabled (bool shouldRecord, int source);
    [[nodiscard]] bool isPatternRecordingEnabled() const;

    void setProjectMetadata (const juce::String& name,
                             const juce::String& description,
                             const juce::String& createdAt,
                             const juce::String& modifiedAt,
                             std::optional<juce::String> author = std::nullopt);
    void resetProject();
    [[nodiscard]] juce::String getProjectName() const;
    [[nodiscard]] juce::String getProjectDescription() const;
    [[nodiscard]] juce::String getProjectAuthor() const;
    [[nodiscard]] juce::String getProjectCreatedAt() const;
    [[nodiscard]] juce::String getProjectModifiedAt() const;
    [[nodiscard]] juce::File getProjectFile() const;
    void setProjectFile (const juce::File&);

private:
    friend class PluginProcessorTestPeer;
    struct PatternRecordEvent
    {
        int source = 0;
        int channel = 0;
        int step = 0;
        int velocity = 100;
    };
    static BusesProperties createBusesProperties();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void initializeDefaultPattern();
    ksh::EngineCallbacks makeEngineCallbacks();
    /** Message-thread half of every audio/host handoff; runs from the timer when work is pending. */
    void runMessageThreadWork();
    void requestMessageThreadWork() { messageThreadWorkPending.store (true, std::memory_order_release); }
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void publishPlaybackSnapshot();
    void publishPlaybackSnapshotLocked();
    void requestPlaybackReset() { playbackResetRequested.store (true, std::memory_order_release); }
    void applyPendingMacroParametersLocked();
    void publishPlaybackSnapshotIfChangedLocked();
    void addMacroParameterListeners();
    void removeMacroParameterListeners();
    void applyMacroParametersToEngineLocked (uint32_t parameterMask);
    void syncMacroParametersFromEngineLocked (bool notifyHost);
    void adoptGeneratedSourcesLocked();
    void markDocumentModified();
    void notifyHostOfStateChangeIfDue();
    void applyPendingHostTempoLocked();
    /** Reads pattern selections and record hits; returns whether selection notes must be filtered. */
    bool consumeMidiInput (const juce::MidiBuffer& midiMessages,
                           const ksh::PlaybackSnapshot& snapshot,
                           double ppqPosition,
                           double bpm,
                           bool isPlaying,
                           ksh::MidiPatternSelectionBlock& selections);
    juce::MidiBuffer& reservedMidiOutputBuffer();
    void publishPatternRecordTransportPosition (double ppqPosition, double bpm, bool isPlaying);
    void drainPendingMidiPatternSelectionsLocked();
    void drainPendingPatternRecordEventsLocked();
    [[nodiscard]] static bool enqueuePatternRecordEvent (moodycamel::ReaderWriterQueue<PatternRecordEvent>& queue,
                                                         int source, int channel, int step, int velocity);
    [[nodiscard]] bool recordPatternRowAtCurrentStep (int channel, int velocity);
    [[nodiscard]] bool queueRowAudition (int channelZeroBased, int velocity);
    [[nodiscard]] int quantizedStepForUiTrigger() const;
    void refreshAuditionCacheFromSnapshot (const ksh::PlaybackSnapshot& snapshot);
    void applyProjectMetadataFromState (const nlohmann::json& state);

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

    // Audio -> message-thread handoff (drained in runMessageThreadWork / polled by the editor).
    moodycamel::ReaderWriterQueue<ksh::NativeHit> noteHitsForUi { 1024 };
    moodycamel::ReaderWriterQueue<int> pendingMidiPatternSelections { 128 };
    // Each queue has exactly one producer; only the message thread drains them.
    moodycamel::ReaderWriterQueue<PatternRecordEvent> pendingMidiPatternRecordEvents { 256 }; // audio -> message
    moodycamel::ReaderWriterQueue<PatternRecordEvent> pendingUiPatternRecordEvents { 256 }; // message -> message
    // Generated-source choices made by the audio thread, mirrored into the engine for the UI.
    moodycamel::ReaderWriterQueue<ksh::GeneratedSourcesFrame> generatedSourcesForEngine { 16 };
    std::atomic<int> currentStepForUi { 0 };
    std::atomic<int> globalStepForUi { 0 };
    std::atomic<double> pendingHostBpm { 0.0 };
    std::atomic<bool> hostBpmChangePending { false };
    double lastHostTempoApplyMs = 0.0; // message-thread only
    std::atomic<uint32_t> uiGestureParameters { 0 }; // one bit per kMacroParameterIDs entry

    std::atomic<bool> messageThreadWorkPending { false };
    std::atomic<bool> playbackResetRequested { false };
    // One bit per kMacroParameterIDs entry, set by host/UI parameter changes.
    std::atomic<uint32_t> dirtyMacroParameters { 0 };
    std::atomic<bool> suppressParameterCallbacks { false };
    std::atomic<bool> documentModified { false };
    std::atomic<bool> hostStateChangePending { false };
    double lastHostStateChangeNotifyMs = 0.0; // message-thread only
    std::atomic<int> standaloneTransportPlaying { 0 };
    std::atomic<double> standaloneTransportPpqPosition { 0.0 };
    std::atomic<int> standaloneTransportResetRequested { 0 };
    std::atomic<int> standaloneStopAllNotesRequested { 0 };
    std::atomic<int> patternRecordingEnabled { 0 };
    std::atomic<int> patternRecordingSource { 0 };
    std::atomic<double> patternRecordTransportPpq { 0.0 };
    std::atomic<double> patternRecordTransportBpm { 120.0 };
    std::atomic<double> patternRecordTransportUpdatedMs { 0.0 };
    std::atomic<int> patternRecordTransportPlaying { 0 };

    // Message-thread-readable cache of the last published snapshot (for lock-free row triggers).
    std::array<std::atomic<int>, ksh::Constants::maxChannels> auditionChannelNotes {};
    std::atomic<int> auditionChannelCount { ksh::Constants::defaultChannelCount };
    std::atomic<int> auditionDeviceActive { 1 };
    std::atomic<int> auditionStepCount { 16 };
    std::atomic<double> auditionBeatsPerStep { 0.25 };

    // Output is assembled here and swapped into the host buffer, so the audio thread writes
    // only into storage reserved in prepareToPlay. After a swap a slot holds the host's
    // storage, so slots are told apart by their storage address.
    std::array<juce::MidiBuffer, 2> midiOutputBuffers;
    std::array<const juce::uint8*, 2> reservedMidiStorage {};

    int projectUiScalePercent = 100;

    mutable std::mutex projectMetadataMutex;
    juce::String projectName { "Untitled Project" };
    juce::String projectDescription;
    juce::String projectAuthor;
    juce::File projectFile; // Session-only save destination; never written into project/host state.
    juce::String projectCreatedAt;
    juce::String projectModifiedAt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
