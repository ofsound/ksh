#pragma once
#include "KshPatternGrid.h"
#include "KshPatternButton.h"
#include "KshPreviewPanel.h"
#include "KshProjectDetails.h"
#include "KshCompactPatternPicker.h"
namespace ksh::ui
{
    class NativeEditor : public juce::Component, private juce::Timer, private juce::ChangeListener, private juce::ScrollBar::Listener
    {
    public:
        explicit NativeEditor (PluginProcessor&, std::shared_ptr<juce::PropertiesFile> preferences = {});
        ~NativeEditor() override;
        EditorController controller;
        Layout layout;
        std::function<void (int)> projectAction; // new, load, save, previous, next, save as
        std::function<void()> preferredSizeChanged;
        std::function<void (bool)> zenModeChanged;
        void setZenMode (bool);
        void setZenScaleLimit (int);
        juce::String projectTitle() const { return controller.processor.getProjectName(); }
        juce::String projectDescription() const { return controller.processor.getProjectDescription(); }
        void sync();
        void confirmDiscardingChanges (std::function<void()>);
        void setCompactViewport (bool, int width, int height, bool phone = true);
        void setCompactVisibleRows (int);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void focusLost (FocusChangeType) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;

    private:
        LookAndFeel look;
        std::shared_ptr<juce::PropertiesFile> uiPreferences;
        FooterHelp footerHelp { *this };
        juce::TextButton rowOrder { "Reverse rows" }, cellDisplay { "Graphical values" };
        juce::TextButton modifierToggle { "Shift/Option shortcuts" };
        juce::TextButton midiOutput { "MIDI Output" };
        juce::TextButton settings;
        juce::TextButton linkSettings { "Link" };
        bool zenMode = false, lastStandalonePlaying = false, lastStandaloneLaunchPending = false;
        uint32_t lastMidiActivity = 0;
        double lastMidiActivityMs = 0.0;
        PatternGrid grid;
        juce::TextButton visibleRows { "Rows: 8" };
        juce::ScrollBar rowScroll { true };
        int preferredPhoneRows = 8;
        void scrollBarMoved (juce::ScrollBar*, double) override;
        void scrollCompactRows (int);
        void visibleRowsMenu();
        juce::Label layerHint;
        PreviewPanel preview, randomPanel;
        juce::Viewport aboutViewport;
        std::array<PatternButton, 16> patterns;
        static constexpr int patternGlowExtent = 20;
        melatonin::DropShadow patternGlow { { Palette::accent.withAlpha (0.8f), 18 }, { Palette::accent.withAlpha (0.55f), 5 } };
        std::array<juce::TextButton, 6> projectButtons;
        juce::Component recordingStrip;
        std::array<RowTriggerButton, 8> recordingPads;
        PadVelocitySwitch padVelocityMode;
        void syncRecordingPads();
        void layoutRecordingPads();
        void triggerPad (int channel, int velocity = CellDefaults::velocity);
        ProjectHeader projectHeader;
        ProjectDetails projectDetails;
        NoteChooser noteChooser;
        bool noteChooserEditing = false;
        uint64_t noteChooserEpoch = 0;
        int noteChooserChannel = -1, noteChooserOriginal = 0;
        void showNoteChooser (int, juce::Component&);
        void updateNoteChooserTranspose();
        void applyNoteChooserPreset (const NoteChooser::RowNotes&, const juce::String& undoLabel);
        DiscardChanges discardChanges;
        uint64_t discardEpoch = 0;
        GestureButton selectMode { "Select" }, chanceLayer { "Chance" };
        juce::TextButton stop { "Stop" };
        juce::TextButton showPads, showSwing;
        SettingsButton recordQuantize, metronome;
        juce::TextButton mutePattern { "M" }, velocityLayer { "Velocity" }, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "Channel colors" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale, tempo;
        std::bitset<8> heldPads;
        int previousScale = 100;
        std::optional<std::pair<Layout, juce::Rectangle<int>>> laidOut;
        bool syncing = false;
        enum class Panel { none, project, pattern, feel, topSecret, superSecret, view, help, channel, tempo };
        Panel panel = Panel::none;
        Panel lastSettingsPanel = Panel::pattern;
        Panel tempoReturnPanel = Panel::none;
        bool confirmPatternClear = false;
        int clearSource = -1;
        uint64_t clearEpoch = 0;
        CompactPatternPicker patternPicker { controller };
        int selectedChannel = 0;
        juce::TextButton compactPattern, controls { "Settings" }, closePanel { "Done" }, deselect { "Done" }, page { "1-16" }, duplicate { "Copy" };
        juce::TextButton compactAccent, compactClear { "Clear pattern..." };
        ClearFlash patternClearFlash { clear }, compactClearFlash { compactClear };
        juce::TextButton recordOptions { "Timing options..." };
        juce::TextButton phoneTempo, metronomeOptions { "Metronome sound..." };
        juce::Slider tempoFader { juce::Slider::LinearHorizontal, juce::Slider::TextBoxAbove };
        AccentWordmark phoneAboutWordmark;
        std::array<juce::TextButton, 7> panelTabs;
        ValueControl loopStart, loopLength;
        NoteInput channelNote;
        juce::TextButton channelReverse { "Reverse" }, channelPing { "Ping-pong" }, channelSolo { "Solo" };
        bool showingCompactPadPreview() const;
        bool showingRecordingPads() const;
        juce::Rectangle<int> compactPadPreviewBounds() const;
        void paintCompactPadPreview (juce::Graphics&) const;
        void paintPhoneMenu (juce::Graphics&) const;
        void layoutPhoneMenu (std::vector<juce::Component*>&);
        void resizedCompact();
        void setupCompactControls();
        void showPanel (Panel);
        void syncChannel();
        void syncMidiOutput();
        void cancelCompactGestures();
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateLayerHeading();
        void updatePatternButtons();
        void configureValue (ValueControl&, const juce::String&, int, int, const std::string&);
        void rateMenu();
        void settingsMenu();
        void toggleKeyboardModifiers();
        void swingMenu();
        void updateSwingScale();
        void recordingMenu();
        void metronomeMenu (juce::Component&);
        void dragPattern (int source);
        static const std::array<juce::String, 8> rateNames;
        static int rateControlWidth();
        void toggleAboutPanel();
        bool textFocused() const;
    };
}
