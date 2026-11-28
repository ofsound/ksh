#pragma once
#include "KshPatternGrid.h"
#include "KshPatternButton.h"
#include "KshPreviewPanel.h"
#include "KshProjectDetails.h"
#include "KshProjectBrowser.h"
#include "KshCompactPatternPicker.h"
namespace ksh::ui
{
    class NativeEditor : public juce::Component, private juce::Timer, private juce::ChangeListener,
                         private juce::ScrollBar::Listener, private juce::FocusChangeListener, private juce::KeyListener
    {
    public:
        explicit NativeEditor (PluginProcessor&, std::shared_ptr<juce::PropertiesFile> preferences = {}, bool windowCanGrow = true);
        ~NativeEditor() override;
        EditorController controller;
        Layout layout;
        static constexpr int restorePreviousProjectAction = 7;
        std::function<void (int)> projectAction; // new, load, save, previous, next, save as, share, restore
        std::function<void()> preferredSizeChanged;
        std::function<void (bool)> zenModeChanged;
        std::function<void (const juce::File&)> projectBrowserDismissed;
        void setZenMode (bool);
       #if JUCE_MAC
        void setTallMode (bool);
       #endif
        void setZenScaleLimit (int);
        juce::String projectTitle() const { return controller.processor.getProjectName(); }
        juce::String projectDescription() const { return controller.processor.getProjectDescription(); }
        void sync();
        void showProjectSaved();
        void returnToPhoneGrid();
        void confirmDiscardingChanges (std::function<void()>);
        void showProjectBrowser (juce::File root, juce::File current, std::optional<ProjectBrowser::Details>,
                                 ProjectBrowser::ListProjects, ProjectBrowser::Submit, std::function<void()> importExternal = {},
                                 bool confirmOpen = false, ProjectBrowser::ReadDetails = {}, juce::File lastFolder = {},
                                 ProjectBrowser::StorageLocations = {});
        bool isProjectBrowserOpen() const { return projectBrowser.isVisible(); }
        bool isProjectDialogOpen() const { return isProjectBrowserOpen() || discardChanges.isVisible(); }
        void setCompactViewport (bool, int width, int height, bool phone = true);
        void setCompactVisibleRows (int);
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void focusLost (FocusChangeType) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;

    private:
        LookAndFeel look;
        std::shared_ptr<juce::PropertiesFile> uiPreferences;
       #if JUCE_MAC
        const bool allowTallMode;
        bool tallModeAvailable() const
        {
            const auto wrapper = controller.processor.wrapperType;
            return allowTallMode && !layout.compact
                && (layout.standalone || wrapper == juce::AudioProcessor::wrapperType_AudioUnit
                    || wrapper == juce::AudioProcessor::wrapperType_VST3);
        }
       #endif
        struct Theme
        {
            bool blue;
            Palette::AccentColours colours;
            Palette::BackgroundTone backdrop;
            int textBrightness;
            bool operator== (const Theme&) const = default;
        };
        std::optional<Theme> themeBeforeGesture;
        static Theme currentTheme();
        static void applyTheme (const Theme&, juce::PropertiesFile&);
        void editTheme (const Theme&);
        void finishThemeGesture();
        FooterHelp footerHelp { *this };
        juce::TextButton rowOrder { "Reverse rows" }, cellDisplay { "Show values" };
        juce::TextButton show16Rows { "16 Tracks" };
        juce::TextButton modifierToggle { "Shift/Option shortcuts" };
        juce::TextButton midiOutput { "MIDI Output" };
        juce::TextButton settings;
        juce::TextButton linkSettings { "Link" };
        std::unique_ptr<juce::Component> trackColourContent;
        std::unique_ptr<juce::CallOutBox> trackColourPopup;
        bool zenMode = false, lastStandalonePlaying = false, lastStandaloneLaunchPending = false;
        uint32_t lastMidiActivity = 0;
        double lastMidiActivityMs = 0.0;
        PatternGrid grid;
        juce::TextButton visibleRows { "Rows: 8" };
        struct RowScrollLookAndFeel final : juce::LookAndFeel_V4
        {
            int artworkInset = 0, artworkWidth = Layout::rowScrollWidth;
            void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int, int, int, int, bool, int, int, bool, bool) override;
            int getMinimumScrollbarThumbSize (juce::ScrollBar&) override;
        } rowScrollLook;
        juce::ScrollBar rowScroll { true };
        int preferredPhoneRows = 8;
       #if JUCE_MAC
        int firstRowBeforeTallMode = 0;
       #endif
        void scrollBarMoved (juce::ScrollBar*, double) override;
        void scrollGridRows (int);
        void layoutRowScroll();
        void visibleRowsMenu();
        juce::Label layerHint;
        PreviewPanel preview, randomPanel;
        juce::Viewport aboutViewport;
        std::array<PatternButton, 16> patterns;
        static constexpr int patternGlowExtent = 20;
        melatonin::DropShadow patternGlow { { Palette::accent.withAlpha (0.8f), 18 }, { Palette::accent.withAlpha (0.55f), 5 } };
        std::array<juce::TextButton, 6> projectButtons;
        juce::TimedCallback projectSavedTimer { [this] { projectSavedTimer.stopTimer(); repaint(); } };
        juce::Component recordingStrip;
        std::array<RowTriggerButton, Constants::maxChannels> recordingPads;
        PadVelocitySwitch padVelocityMode;
        void syncRecordingPads();
        void layoutRecordingPads();
        void triggerPad (int channel, int velocity = CellDefaults::velocity, std::optional<juce::Time> touchTime = std::nullopt);
        std::optional<std::pair<juce::Time, double>> lastPadTouch;
        ProjectHeader projectHeader;
        ProjectBrowser projectBrowser;
        NoteChooser noteChooser;
        bool noteChooserEditing = false;
        uint64_t noteChooserEpoch = 0;
        int noteChooserChannel = -1;
        Channel noteChooserOriginal;
        void showNoteChooser (int, juce::Component&);
        bool showingPhoneNoteChooser() const { return layout.phoneRowControls && noteChooser.isVisible(); }
        void syncNoteChooserGrid();
        void syncNoteChooserControls();
        void applyNoteChooserPreset (const NoteChooser::RowNotes&, const juce::String& undoLabel);
        DiscardChanges discardChanges;
        uint64_t discardEpoch = 0;
        GestureButton selectMode { "Select" }, chanceLayer { "Chance" };
        juce::TextButton stop { "Stop" };
        juce::TextButton generate, jamButton, showPads, showSwing;
        RowTriggerButton finishJamButton, footerJamPad;
        struct JamTarget { int source; uint64_t epoch; bool padsOpen; double armedAtMs; };
        std::optional<JamTarget> jamTarget;
        enum class JamPlayback { stopped, starting, playing };
        JamPlayback jamPlayback = JamPlayback::stopped;
        std::optional<int> heldJamStopKey;
        juce::Component::SafePointer<juce::Component> jamKeyComponent;
        bool restoreJamTextInput = false;
        void globalFocusChanged (juce::Component*) override;
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        bool keyStateChanged (bool, juce::Component*) override;
        bool handleJamStopKey (const juce::KeyPress&);
        void toggleJam();
        void cancelJam();
        void syncJamButton();
        SettingsButton recordQuantize, metronome;
        juce::TextButton mutePattern { "M" }, velocityLayer { "Velocity" }, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "Channel colors" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale;
        std::bitset<8> heldPads;
        int previousScale = 100;
        std::optional<std::pair<Layout, juce::Rectangle<int>>> laidOut;
        bool syncing = false;
        enum class Panel { none, project, pattern, feel, view, help, channel, generate };
        Panel panel = Panel::none;
        Panel lastSettingsPanel = Panel::pattern;
        bool confirmPatternClear = false;
        int clearSource = -1;
        uint64_t clearEpoch = 0;
        CompactPatternPicker patternPicker { controller };
        int selectedChannel = 0;
        juce::TextButton compactPattern, controls { "Settings" }, closePanel { "Done" }, deselect { "Done" }, page { "1-16" }, duplicate { "Copy" };
        juce::TextButton compactAccent, compactClear { "Clear pattern..." };
        ClearFlash patternClearFlash { clear }, compactClearFlash { compactClear };
        juce::TextButton recordOptions { "Timing options..." };
        GestureButton tempoButton;
        juce::TextButton metronomeOptions { "Metronome sound..." };
        struct TempoScrub
        {
            juce::MouseInputSource pointer;
            int start, requested;
            bool dragging = false;
        };
        std::optional<TempoScrub> tempoScrub;
        juce::Slider tempoFader { juce::Slider::LinearHorizontal, juce::Slider::TextBoxAbove };
        juce::TextButton tempoDown { juce::String::fromUTF8 ("−") }, tempoUp { "+" };
        juce::TextButton tapTempo { "Tap" };
        class TempoOverlay : public juce::Component
        {
        public:
            std::function<void()> dismiss;
            juce::Rectangle<int> card, triggerBounds;
            juce::Path outline;
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
        private:
            melatonin::DropShadow panelGlow { juce::Colours::transparentBlack, 10, {}, 1 };
        } tempoOverlay;
        bool tempoOpen = false;
        std::optional<double> optionTapStarted;
        bool lastOptionDown = false;
        bool warnedUnsupportedProjectVersion = false;
        static constexpr int minimumTempoBpm = 20, maximumTempoBpm = 300;
        struct TempoTaps
        {
            std::optional<double> lastMs;
            std::array<double, 4> intervals {};
            size_t count = 0, next = 0;
        } tempoTaps;
        void setTempoOpen (bool);
        void layoutTempoPopup();
        void syncTempo();
        void finishTempoScrub();
        void registerTempoTap();
        AccentWordmark phoneAboutWordmark;
        std::array<juce::TextButton, 5> panelTabs;
        ValueControl loopStart, loopLength;
        NoteInput channelNote;
        juce::TextButton channelDirection { "Forward" }, channelSolo { "Solo" }, channelClear { "Clear Track" };
        ClearFlash channelClearFlash { channelClear };
        class ChannelOverlay : public juce::Component
        {
        public:
            std::function<void()> dismiss;
            juce::Rectangle<int> opener, card;
            bool hitTest (int x, int y) override { return !opener.contains (x, y); }
            void mouseDown (const juce::MouseEvent& event) override;
            void paint (juce::Graphics&) override;
        } channelOverlay;
        bool showingPhoneChannelSettings() const
        {
            return layout.phoneRowControls && panel == Panel::channel && !isProjectDialogOpen();
        }
        bool showingCompactPadPreview() const;
        bool showingRecordingPads() const;
        juce::Rectangle<int> compactPadPreviewBounds() const;
        void paintCompactPadPreview (juce::Graphics&) const;
        void paintPhoneMenu (juce::Graphics&) const;
        bool phonePatternPanel() const;
        void layoutPhoneMenu (std::vector<juce::Component*>&);
        void resizedCompact();
        void setupCompactControls();
        void configureSelectButton();
        void showPanel (Panel);
        void syncChannel();
        bool noteChooserMatches() const;
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
        void showDesktopRandomizer (bool visible);
        bool textFocused() const;
    };
}
