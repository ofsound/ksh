#include "KshEditorController.h"

namespace ksh::ui
{
    EditorController::EditorController (PluginProcessor& p) : processor (p), state (p.nativeUiState()), epoch (state.documentEpoch)
    {
        processor.getUiBridge().onNoteHit = [this] (const NativeHit& hit) {
            const auto until = juce::Time::getMillisecondCounterHiRes() + 200.0;
            const auto row = hit.uiChannel - 1;
            if (row < 0 || row >= 8)
                return;
            if (hit.uiGeneratedStep > 0 && hit.uiGeneratedStep <= 32)
                generatedFlashes[static_cast<size_t> (row)][static_cast<size_t> (hit.uiGeneratedStep - 1)] = until;
            if (hit.uiSource == source() + 1 && hit.uiSourceStep > 0 && hit.uiSourceStep <= 32)
                sourceFlashes[static_cast<size_t> (row)][static_cast<size_t> (hit.uiSourceStep - 1)] = until;
        };
    }
    EditorController::~EditorController()
    {
        commit();
        processor.getUiBridge().onNoteHit = {};
    }
    const Cell& EditorController::cell (Location p) const
    {
        static const Cell empty;
        return source() >= 0 && p.valid() ? state.playback.sources[static_cast<size_t> (source())][static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] : empty;
    }
    LoopRange EditorController::loop (int channel) const
    {
        return state.playback.sourceSettings[static_cast<size_t> (juce::jmax (0, source()))].loopRanges[static_cast<size_t> (channel)];
    }
    std::vector<Location> EditorController::selection (bool enabledOnly) const
    {
        std::vector<Location> result;
        for (int ch = 0; ch < state.playback.channelCount; ++ch)
            for (int step = 0; step < state.playback.stepCount; ++step)
                if (selected.test (static_cast<size_t> (ch * 32 + step)) && (!enabledOnly || cell ({ ch, step }).enabled))
                    result.push_back ({ ch, step });
        return result;
    }
    void EditorController::finishParameter()
    {
        if (auto* p = processor.getValueTreeState().getParameter (gestureParameter))
            p->endChangeGesture();
        gestureParameter.clear();
    }
    void EditorController::begin (const juce::String& label, const juce::String& parameter)
    {
        if (transaction)
            return;
        refresh();
        before = capture();
        lastOwn = before;
        gestureLabel = label;
        gestureParameter = parameter;
        if (auto* p = processor.getValueTreeState().getParameter (parameter))
            p->beginChangeGesture();
        transaction = true;
    }
    void EditorController::commit()
    {
        if (!transaction)
            return;
        if (epoch != processor.getUiBridge().getDocumentEpoch())
        {
            clearHistory();
            refresh();
            return;
        }
        flush();
        if (!transaction)
            return;
        rebaseExternal();
        if (before != lastOwn)
        {
            // Retain only changed top-level fields and cells, rather than two entire
            // 4096-cell projects per undo entry. Conditional merge still compares each field.
            auto compactBefore = nlohmann::json::object(), compactAfter = nlohmann::json::object();
            for (const auto& item : before.items())
                if (item.key() != "cells" && (!lastOwn.contains (item.key()) || item.value() != lastOwn[item.key()]))
                    compactBefore[item.key()] = item.value();
            for (const auto& item : lastOwn.items())
                if (item.key() != "cells" && (!before.contains (item.key()) || item.value() != before[item.key()]))
                    compactAfter[item.key()] = item.value();
            compactBefore["cells"] = nlohmann::json::object();
            compactAfter["cells"] = nlohmann::json::object();
            for (const auto& item : before["cells"].items())
                if (!lastOwn["cells"].contains (item.key()) || item.value() != lastOwn["cells"][item.key()])
                    compactBefore["cells"][item.key()] = item.value();
            for (const auto& item : lastOwn["cells"].items())
                if (!before["cells"].contains (item.key()) || item.value() != before["cells"][item.key()])
                    compactAfter["cells"][item.key()] = item.value();
            undoEntries.push_back ({ std::move (compactBefore), std::move (compactAfter), gestureLabel });
            if (undoEntries.size() > 128)
                undoEntries.erase (undoEntries.begin());
            redoEntries.clear();
        }
        transaction = false;
        before = {};
        finishParameter();
        notify();
    }
    namespace
    {
        nlohmann::json conditionalMerge (nlohmann::json current, const nlohmann::json& expected, const nlohmann::json& desired)
        {
            for (const auto& operation : nlohmann::json::diff (expected, desired))
            {
                const nlohmann::json::json_pointer path (operation.at ("path").get<std::string>());
                if (current.contains (path) != expected.contains (path))
                    continue;
                if (current.contains (path) && current.at (path) != expected.at (path))
                    continue;
                try
                {
                    current = current.patch (nlohmann::json::array ({ operation }));
                } catch (const nlohmann::json::exception&)
                {
                }
            }
            return current;
        }
    }
    nlohmann::json EditorController::capture()
    {
        auto data = processor.enginePersistenceState();
        // Stable cell identities prevent sparse-array insertion/deletion from shifting undo targets.
        data["cells"] = nlohmann::json::object();
        const auto native = processor.nativeUiState();
        for (int src = 0; src < 16; ++src)
            for (int ch = 0; ch < 8; ++ch)
                for (int step = 0; step < 32; ++step)
                {
                    const auto& c = native.playback.sources[static_cast<size_t> (src)][static_cast<size_t> (ch)][static_cast<size_t> (step)];
                    if (c == Cell {})
                        continue;
                    data["cells"][std::to_string (src * 256 + ch * 32 + step)] = { src, ch, step, c.enabled ? 1 : 0, c.velocity, c.probability, c.cycle, c.cycleMask, c.roll };
                }
        return data;
    }
    void EditorController::rebaseExternal()
    {
        if (!transaction)
            return;
        const auto current = capture();
        // External changes become part of the baseline, never part of this gesture's undo action.
        for (const auto& operation : nlohmann::json::diff (lastOwn, current))
        {
            const nlohmann::json::json_pointer path (operation.at ("path").get<std::string>());
            if (operation.at ("op") != "remove")
                before[path] = operation.at ("value");
            else if (before.contains (path))
                before = before.patch (nlohmann::json::array ({ operation }));
        }
        lastOwn = current;
    }
    void EditorController::applyHistory (const nlohmann::json& expected, const nlohmann::json& desired)
    {
        auto current = conditionalMerge (capture(), expected, desired);
        auto cells = nlohmann::json::array();
        for (const auto& cell : current["cells"].items())
            cells.push_back (cell.value());
        current["cells"] = cells;
        (void) processor.applyPersistenceFromUi (current);
        processor.getUiBridge().invalidate();
        refresh();
    }
    void EditorController::cancel()
    {
        pending.clear();
        if (transaction && epoch == processor.getUiBridge().getDocumentEpoch())
        {
            applyHistory (lastOwn, before);
        }
        transaction = false;
        before = {};
        finishParameter();
        refresh();
    }
    void EditorController::clearHistory()
    {
        pending.clear();
        transaction = false;
        before = {};
        finishParameter();
        undoEntries.clear();
        redoEntries.clear();
        selected.reset();
        copySource = -1;
        soloChannel = -1;
    }
    void EditorController::refresh()
    {
        state = processor.nativeUiState();
        if (epoch != state.documentEpoch)
        {
            clearHistory();
            epoch = state.documentEpoch;
        }
        notify();
    }
    void EditorController::command (std::string_view selector, const nlohmann::json& args, const juce::String& label)
    {
        const bool single = !transaction && label.isNotEmpty();
        if (single)
            begin (label, juce::String (selector.data(), selector.size()));
        flush();
        rebaseExternal();
        (void) processor.dispatchUiEngineCommand (selector, args);
        if (transaction)
            lastOwn = capture();
        processor.scheduleUiPreviewFlush();
        refresh();
        if (single)
            commit();
    }
    void EditorController::editCell (Location location, Cell value)
    {
        if (source() < 0 || !location.valid())
            return;
        value = cloneCell (value);
        auto& current = state.playback.sources[static_cast<size_t> (source())][static_cast<size_t> (location.channel)][static_cast<size_t> (location.step)];
        if (value == current)
            return;
        auto existing = std::find_if (pending.begin(), pending.end(), [&] (const auto& e) { return e.source == source() && e.channel == location.channel && e.step == location.step; });
        if (existing == pending.end())
            pending.push_back ({ source(), location.channel, location.step, current, value });
        else
            existing->value = value;
        current = value;
    }
    void EditorController::flush()
    {
        if (pending.empty())
            return;
        rebaseExternal();
        if (!processor.applyNativeCellEdits (pending, epoch))
        {
            // External recording/restoration won the race. Never write the stale draft.
            pending.clear();
            transaction = false;
            before = {};
            finishParameter();
            refresh();
            return;
        }
        pending.clear();
        if (transaction)
            lastOwn = capture();
        state = processor.nativeUiState();
    }
    bool EditorController::poll()
    {
        const auto previousRevision = state.revision;
        if (processor.getUiBridge().getDocumentEpoch() != epoch)
        {
            clearHistory();
            refresh();
        }
        flush();
        if (processor.getUiBridge().getRevision() != state.revision)
            refresh();
        playingStep = processor.getCurrentStepForUi();
        if (previousRevision != state.revision)
            notify();
        return previousRevision != state.revision;
    }
    void EditorController::undo()
    {
        refresh();
        commit();
        if (undoEntries.empty())
            return;
        auto entry = std::move (undoEntries.back());
        undoEntries.pop_back();
        applyHistory (entry.after, entry.before);
        redoEntries.push_back (std::move (entry));
        notify();
    }
    void EditorController::redo()
    {
        refresh();
        commit();
        if (redoEntries.empty())
            return;
        auto entry = std::move (redoEntries.back());
        redoEntries.pop_back();
        applyHistory (entry.before, entry.after);
        undoEntries.push_back (std::move (entry));
        notify();
    }
    void EditorController::selectSource (int index, bool copy)
    {
        commit();
        if (copy && index >= 0)
        {
            copySource = index;
            notify();
            return;
        }
        if (copySource >= 0 && index >= 0)
        {
            command ("source_pattern_copy", { copySource + 1, index + 1 }, "Copy pattern");
            copySource = -1;
        }
        selected.reset();
        soloChannel = -1;
        command ("static_source", index < 0 ? nlohmann::json::array ({ "mute" }) : nlohmann::json::array ({ index + 1 }));
    }
    void EditorController::deleteSelection()
    {
        begin ("Delete cells");
        for (auto p : selection())
            editCell (p, {});
        commit();
        notify();
    }
    void EditorController::moveSelection (Location anchor, Location target, bool copy)
    {
        const auto locations = selection();
        std::vector<Cell> values;
        for (auto p : locations)
            values.push_back (cell (p));
        if (!copy)
            for (auto p : locations)
                editCell (p, {});
        selected.reset();
        for (size_t i = 0; i < locations.size(); ++i)
        {
            Location destination { wrap (target.channel + locations[i].channel - anchor.channel, state.playback.channelCount), wrap (target.step + locations[i].step - anchor.step, state.playback.stepCount) };
            editCell (destination, values[i]);
            selected.set (static_cast<size_t> (destination.key()));
        }
    }
    void EditorController::shift (int channel, int delta)
    {
        begin ("Shift pattern");
        for (int ch = 0; ch < state.playback.channelCount; ++ch)
        {
            if (channel >= 0 && ch != channel)
                continue;
            const auto range = loop (ch);
            std::vector<Cell> values;
            for (int i = 0; i < range.loopLength; ++i)
                values.push_back (cell ({ ch, range.loopStart + i }));
            for (int i = 0; i < range.loopLength; ++i)
                editCell ({ ch, range.loopStart + i }, values[static_cast<size_t> (wrap (i - delta, range.loopLength))]);
        }
        commit();
        notify();
    }
    void EditorController::clearRow (int ch, bool reset)
    {
        if (source() < 0)
            return;
        begin ("Clear channel");
        if (reset)
            command ("source_channel_reset", { source() + 1, ch + 1 });
        else
            for (int i = 0; i < 32; ++i)
                editCell ({ ch, i }, {});
        commit();
        notify();
    }
    void EditorController::clearPattern()
    {
        if (source() < 0)
            return;
        begin ("Clear pattern");
        for (int ch = 0; ch < state.playback.channelCount; ++ch)
        {
            command ("source_channel_reset", { source() + 1, ch + 1 });
            command ("channel_lock", { ch + 1, "random" });
            command ("channel_playback_mode", { ch + 1, "normal" });
        }
        commit();
    }
    void EditorController::toggleMute (int ch, bool solo)
    {
        if (source() < 0)
            return;
        begin (solo ? "Solo channel" : "Mute channel");
        if (solo)
        {
            if (soloChannel < 0)
                soloRestore = state.playback.sourceChannelMutes[static_cast<size_t> (source())];
            const bool restore = soloChannel == ch;
            for (int i = 0; i < state.playback.channelCount; ++i)
                command ("source_channel_mute", { source() + 1, i + 1, restore ? soloRestore[static_cast<size_t> (i)] : i != ch });
            soloChannel = restore ? -1 : ch;
        }
        else
        {
            soloChannel = -1;
            command ("source_channel_mute", { source() + 1, ch + 1, !state.playback.sourceChannelMutes[static_cast<size_t> (source())][static_cast<size_t> (ch)] });
        }
        commit();
    }
    void EditorController::setCycle (int length, int mask, const std::vector<Location>& locations)
    {
        for (auto p : locations)
        {
            auto c = cell (p);
            c.cycle = juce::jlimit (1, 64, length);
            c.cycleMask = normalizeCycleMask (mask, c.cycle);
            editCell (p, c);
        }
        notify();
    }
}
