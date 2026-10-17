#pragma once
#include "../PluginProcessor.h"
#include <bitset>

namespace ksh::ui
{
    enum class Layer { velocity,
        chance };
    struct Location
    {
        int channel = -1, step = -1;
        bool valid() const { return channel >= 0 && channel < 8 && step >= 0 && step < 32; }
        int key() const { return channel * 32 + step; }
        bool operator== (const Location&) const = default;
    };
    using Selection = std::bitset<256>;
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
            case Layer::velocity:
                c.velocity = juce::jlimit (1, 127, value);
                break;
        }
        return c;
    }

    class EditorController
    {
    public:
        explicit EditorController (PluginProcessor&);
        ~EditorController();
        PluginProcessor& processor;
        PluginProcessor::UiState state;
        Selection selected;
        Layer layer = Layer::velocity;
        bool drumColors = true, noteNames = true;
        bool selectLatched = false, selectHeld = false, selectUsed = false;
        bool shiftDown (const juce::ModifierKeys& mods) const { return selectLatched || selectHeld || mods.isShiftDown(); }
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
        std::array<std::array<double, 32>, 8> sourceFlashes {}, generatedFlashes {};
        std::array<std::array<int, 32>, 8> sourceCycleSteps {};
        int playingStep = 0;

        int source() const { return state.playback.staticSource; }
        const Cell& cell (Location p) const;
        LoopRange loop (int channel) const;
        std::vector<Location> selection (bool enabledOnly = false) const;
        void selectSource (int sourceIndex, bool copy = false);
        void command (std::string_view, const nlohmann::json&, const juce::String& label = {});
        void begin (const juce::String& label, const juce::String& parameter = {});
        void commit();
        void cancel();
        bool editing() const { return transaction; }
        void editCell (Location, Cell);
        void flush();
        bool poll();
        void refresh();
        void undo();
        void redo();
        bool canUndo() const { return !undoEntries.empty(); }
        bool canRedo() const { return !redoEntries.empty(); }
        void clearHistory();
        void deleteSelection();
        void moveSelection (Location anchor, Location target, bool copy);
        void shift (int channel, int delta);
        void clearRow (int channel, bool reset);
        void clearPattern();
        void toggleMute (int channel, bool solo);
        void setCycle (int length, int mask, const std::vector<Location>& cells);
        void notify()
        {
            if (changed)
                changed();
        }

    private:
        struct Entry
        {
            nlohmann::json before, after;
            juce::String label;
        };
        std::vector<Entry> undoEntries, redoEntries;
        std::vector<PluginProcessor::CellEdit> pending;
        nlohmann::json before, lastOwn;
        juce::String gestureLabel, gestureParameter;
        bool transaction = false;
        std::array<bool, 8> soloRestore {};
        int soloChannel = -1;
        uint64_t epoch = 0;
        void applyHistory (const nlohmann::json& expected, const nlohmann::json& desired);
        nlohmann::json capture();
        void rebaseExternal();
        void finishParameter();
    };
}
