#pragma once
#include "../PluginProcessor.h"
#include <bitset>

namespace ksh::ui
{
    enum class Layer { velocity,
        chance, nudge, roll, cycle };
    inline bool overviewLayer (Layer layer) { return layer == Layer::nudge || layer == Layer::roll || layer == Layer::cycle; }
    struct Location
    {
        int channel = -1, step = -1;
        bool valid() const { return channel >= 0 && channel < Constants::maxChannels && step >= 0 && step < Constants::maxSteps; }
        int key() const { return channel * Constants::maxSteps + step; }
        bool operator== (const Location&) const = default;
    };
    using Selection = std::bitset<Constants::cellsPerPattern>;
    inline int wrap (int value, int size) { return ((value % size) + size) % size; }
    inline int jsRound (double value) { return static_cast<int> (std::floor (value + 0.5)); }
    inline int dragOffset (float delta, float sensitivity, float deadZone = 4.0f)
    {
        if (std::abs (delta) <= deadZone)
            return 0;
        return (delta < 0 ? -1 : 1) * jsRound ((std::abs (delta) - deadZone) / sensitivity);
    }
    inline int cellValue (const Cell& c, Layer layer)
    {
        switch (layer)
        {
            case Layer::chance:
                return c.probability;
            case Layer::nudge: return c.nudge;
            case Layer::roll: return c.roll;
            case Layer::cycle: return c.cycle;
            case Layer::velocity:
                return c.velocity;
        }
        return c.velocity;
    }
    inline Cell withValue (Cell c, Layer layer, int value)
    {
        switch (layer)
        {
            case Layer::chance:
                c.probability = juce::jlimit (0, 100, value);
                break;
            case Layer::nudge:
            case Layer::roll:
            case Layer::cycle:
                break; // Inspector overview layers never paint values into the grid.
            case Layer::velocity:
                c.velocity = juce::jlimit (1, 127, value);
                break;
        }
        return c;
    }

    class EditorController
    {
        const std::unique_ptr<PluginProcessor::UiState> uiState = std::make_unique<PluginProcessor::UiState>();

    public:
        explicit EditorController (PluginProcessor&);
        ~EditorController();
        PluginProcessor& processor;
        PluginProcessor::UiState& state;
        Selection selected;
        Layer layer = Layer::velocity;
        std::optional<Layer> inspectorHoverLayer;
        bool drumColors = true, noteNames = true, graphicalCellValues = true;
        bool keyboardModifiersEnabled = true;
        bool selectLatched = false, selectHeld = false, selectUsed = false;
        bool chanceHeld = false, chanceUsed = false;
        bool shiftDown (const juce::ModifierKeys& mods) const { return selectLatched || selectHeld || (keyboardModifiersEnabled && mods.isShiftDown()); }
        bool optionDown (const juce::ModifierKeys& mods) const { return keyboardModifiersEnabled && mods.isAltDown(); }
        bool useSelectionModifier (const juce::ModifierKeys& mods)
        {
            if (selectHeld)
                selectUsed = true;
            return shiftDown (mods);
        }
        void beginSelect()
        {
            selectHeld = true;
            selectUsed = false;
            notify();
        }
        void endSelect (bool held, bool inside)
        {
            if (!selectHeld)
                return;
            if (!held && !selectUsed && inside)
                selectLatched = !selectLatched;
            selectHeld = false;
            notify();
        }
        void cancelSelect()
        {
            selectHeld = selectLatched = selectUsed = false;
            notify();
        }
        int copySource = -1;
        std::function<void()> changed;
        std::function<void (int channel, int velocity)> onPlaybackHit;
        static constexpr double sourceFlashDurationMs = 80.0;
        std::array<std::array<double, Constants::maxSteps>, Constants::maxChannels> sourceFlashes {}, generatedFlashes {}, sourceCycleFlashes {};
        std::array<std::array<int, Constants::maxSteps>, Constants::maxChannels> sourceCycleSteps {};
        std::array<std::array<std::array<uint8_t, Constants::maxRoll>, Constants::maxSteps>, Constants::maxChannels> sourceSplitCycleSteps {};
        int playingStep = 0;
        int playingGlobalStep = 0;

        int source() const { return state.playback.staticSource; }
        const Cell& cell (Location p) const;
        LoopRange loop (int channel) const;
        std::vector<Location> selection (bool enabledOnly = false) const;
        void selectSource (int sourceIndex, bool copy = false);
        void command (std::string_view, const nlohmann::json&, const juce::String& label = {});
        void begin (const juce::String& label, const juce::String& parameter = {});
        void recordUiEdit (const juce::String& label, std::function<void (bool redoing)>);
        void commit();
        void cancel();
        bool editing() const { return transaction; }
        void editCell (Location, Cell);
        void flush();
        bool poll();
        void refresh();
        void undo();
        void redo();
        bool canUndo() const { return !undoEntries.empty() || !recordingEntry.cells.empty(); }
        bool canRedo() const { return !redoEntries.empty(); }
        void clearHistory();
        void deleteSelection();
        void moveSelection (Location anchor, Location target, bool copy);
        void shift (int channel, int delta);
        void clearRow (int channel);
        void clearPattern();
        void toggleMute (int channel);
        void toggleSolo (int channel);
        int soloedChannel() const;
        bool exitSolo();
        void setCycle (int length, int mask, const std::vector<Location>& cells, int split = -1);
        void notify()
        {
            if (changed)
                changed();
        }

    private:
        struct CellChange
        {
            int index = 0; // source * cellsPerPattern + Location::key()
            Cell before, after;
        };
        // Settings are small enough to keep whole; cells keep only what the gesture changed.
        struct Entry
        {
            DocumentSettings before, after;
            std::vector<CellChange> cells;
            juce::String label;
            std::function<void (bool redoing)> applyUiEdit;
        };
        std::vector<Entry> undoEntries, redoEntries;
        Entry recordingEntry;
        uint64_t recordingSession = 0;
        std::vector<PluginProcessor::CellEdit> pending;
        struct GestureDocuments
        {
            DocumentState before, lastOwn, scratch;
        };
        // These snapshots contain every pattern. Keep them off the UI thread stack.
        const std::unique_ptr<GestureDocuments> documents = std::make_unique<GestureDocuments>();
        juce::String gestureLabel, gestureParameter;
        bool transaction = false;
        std::array<bool, 8> soloRestore {};
        int soloChannel = -1, soloSource = -1;
        uint64_t epoch = 0;
        void pushHistory (Entry);
        void applyHistory (const Entry&, bool redoing);
        void applyDocument (const DocumentState&);
        void reloadState();
        void rebaseExternal();
        void finishParameter();
        void collectRecordingEdits();
        void commitRecordingEdits();
    };
}
