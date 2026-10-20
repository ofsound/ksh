#include "KshEditorController.h"
#include "KshStyle.h"

namespace ksh::ui
{
    namespace
    {
        constexpr int cellCount = Constants::sourceCount * Constants::maxChannels * Constants::maxSteps;

        template <typename Document>
        auto& cellAt (Document& document, int index)
        {
            return document.sources[static_cast<size_t> (index / 256)][static_cast<size_t> (index / 32 % 8)][static_cast<size_t> (index % 32)];
        }

        // Undo compares stored values exactly, so floating-point fields are compared bit-for-bit on purpose.
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        constexpr auto mergeField = [] (auto& current, const auto& expected, const auto& desired)
        {
            if (expected != desired && current == expected)
                current = desired;
        };
        constexpr auto rebaseField = [] (auto& baseline, const auto& own, const auto& now)
        {
            if (own != now)
                baseline = now;
        };
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        // Undo granularity is one persisted field: visit(target, a, b) runs on each matching leaf.
        template <typename Visit>
        void visitCell (Cell& target, const Cell& a, const Cell& b, Visit&& visit)
        {
            visit (target.enabled, a.enabled, b.enabled);
            visit (target.velocity, a.velocity, b.velocity);
            visit (target.probability, a.probability, b.probability);
            visit (target.cycle, a.cycle, b.cycle);
            visit (target.cycleMask, a.cycleMask, b.cycleMask);
            visit (target.roll, a.roll, b.roll);
            visit (target.nudge, a.nudge, b.nudge);
        }

        template <typename Visit>
        void visitSettings (DocumentSettings& target, const DocumentSettings& a, const DocumentSettings& b, Visit&& visit)
        {
            visit (target.stepCount, a.stepCount, b.stepCount);
            visit (target.channelCount, a.channelCount, b.channelCount);
            visit (target.refreshSteps, a.refreshSteps, b.refreshSteps);
            visit (target.generationMode, a.generationMode, b.generationMode);
            visit (target.staticSource, a.staticSource, b.staticSource);
            visit (target.rate, a.rate, b.rate);
            visit (target.tempo, a.tempo, b.tempo);
            visit (target.swing, a.swing, b.swing);
            visit (target.swingSubdivisionIndex, a.swingSubdivisionIndex, b.swingSubdivisionIndex);
            visit (target.velocityHumanize, a.velocityHumanize, b.velocityHumanize);
            visit (target.timingHumanize, a.timingHumanize, b.timingHumanize);
            visit (target.deviceActive, a.deviceActive, b.deviceActive);
            for (size_t ch = 0; ch < target.channels.size(); ++ch)
            {
                auto& t = target.channels[ch];
                const auto &x = a.channels[ch], &y = b.channels[ch];
                visit (t.label, x.label, y.label);
                visit (t.note, x.note, y.note);
                visit (t.lock, x.lock, y.lock);
                visit (t.loopStart, x.loopStart, y.loopStart);
                visit (t.loopLength, x.loopLength, y.loopLength);
                visit (t.playbackMode, x.playbackMode, y.playbackMode);
            }
            for (size_t src = 0; src < target.sourceSettings.size(); ++src)
            {
                auto& t = target.sourceSettings[src];
                const auto &x = a.sourceSettings[src], &y = b.sourceSettings[src];
                visit (t.stepCount, x.stepCount, y.stepCount);
                visit (t.rate, x.rate, y.rate);
                for (size_t ch = 0; ch < t.loopRanges.size(); ++ch)
                {
                    visit (t.loopRanges[ch].loopStart, x.loopRanges[ch].loopStart, y.loopRanges[ch].loopStart);
                    visit (t.loopRanges[ch].loopLength, x.loopRanges[ch].loopLength, y.loopRanges[ch].loopLength);
                }
                for (size_t ch = 0; ch < target.sourceChannelMutes[src].size(); ++ch)
                    visit (target.sourceChannelMutes[src][ch], a.sourceChannelMutes[src][ch], b.sourceChannelMutes[src][ch]);
            }
        }

        template <typename Visit>
        void visitDocument (DocumentState& target, const DocumentState& a, const DocumentState& b, Visit&& visit)
        {
            visitSettings (target.settings, a.settings, b.settings, visit);
            for (int index = 0; index < cellCount; ++index)
                visitCell (cellAt (target, index), cellAt (a, index), cellAt (b, index), visit);
        }
    }

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
            {
                sourceFlashes[static_cast<size_t> (row)][static_cast<size_t> (hit.uiSourceStep - 1)] = until;
                sourceCycleSteps[static_cast<size_t> (row)][static_cast<size_t> (hit.uiSourceStep - 1)] = hit.uiCycleStep;
            }
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
        if (gestureParameter.isNotEmpty())
            processor.endUiParameterGesture (gestureParameter);
        gestureParameter.clear();
    }
    void EditorController::begin (const juce::String& label, const juce::String& parameter)
    {
        ValueControl::endPendingWheelGesture();
        if (transaction)
            return;
        refresh();
        before = processor.editorDocument();
        lastOwn = before;
        gestureLabel = label;
        gestureParameter = parameter;
        if (parameter.isNotEmpty())
            processor.beginUiParameterGesture (parameter);
        transaction = true;
    }
    void EditorController::commit()
    {
        ValueControl::endPendingWheelGesture();
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
            Entry entry { before.settings, lastOwn.settings, {}, gestureLabel };
            for (int index = 0; index < cellCount; ++index)
            {
                const auto& from = cellAt (before, index);
                const auto& to = cellAt (lastOwn, index);
                if (from != to)
                    entry.cells.push_back ({ index, from, to });
            }
            undoEntries.push_back (std::move (entry));
            if (undoEntries.size() > 128)
                undoEntries.erase (undoEntries.begin());
            redoEntries.clear();
        }
        transaction = false;
        finishParameter();
        notify();
    }
    void EditorController::rebaseExternal()
    {
        if (!transaction)
            return;
        auto current = processor.editorDocument();
        // External changes become part of the baseline, never part of this gesture's undo action.
        visitDocument (before, lastOwn, current, rebaseField);
        lastOwn = std::move (current);
    }
    void EditorController::applyHistory (const Entry& entry, bool redoing)
    {
        // Conditional merge: a field is written only while it still holds the value this entry left behind.
        auto current = processor.editorDocument();
        visitSettings (current.settings, redoing ? entry.before : entry.after, redoing ? entry.after : entry.before, mergeField);
        for (const auto& change : entry.cells)
            visitCell (cellAt (current, change.index), redoing ? change.before : change.after, redoing ? change.after : change.before, mergeField);
        applyDocument (current);
    }
    void EditorController::applyDocument (const DocumentState& document)
    {
        (void) processor.applyEditorDocument (document);
        processor.getUiBridge().invalidate();
        refresh();
    }
    void EditorController::cancel()
    {
        ValueControl::endPendingWheelGesture();
        pending.clear();
        if (transaction && epoch == processor.getUiBridge().getDocumentEpoch())
        {
            auto current = processor.editorDocument();
            visitDocument (current, lastOwn, before, mergeField);
            applyDocument (current);
        }
        transaction = false;
        finishParameter();
        refresh();
    }
    void EditorController::clearHistory()
    {
        ValueControl::endPendingWheelGesture();
        pending.clear();
        transaction = false;
        finishParameter();
        undoEntries.clear();
        redoEntries.clear();
        selected.reset();
        copySource = -1;
        soloChannel = -1;
    }
    void EditorController::reloadState()
    {
        processor.copyNativeUiState (state);
        if (epoch != state.documentEpoch)
        {
            clearHistory();
            epoch = state.documentEpoch;
        }
    }
    void EditorController::refresh()
    {
        reloadState();
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
            lastOwn = processor.editorDocument();
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
            finishParameter();
            refresh();
            return;
        }
        pending.clear();
        if (transaction)
            lastOwn = processor.editorDocument();
        reloadState();
    }
    bool EditorController::poll()
    {
        const auto previousRevision = state.revision;
        if (processor.getUiBridge().getDocumentEpoch() != epoch)
        {
            clearHistory();
            reloadState();
        }
        flush();
        if (processor.getUiBridge().getRevision() != state.revision)
            reloadState();
        const int previousGlobalStep = playingStep > 0 ? playingGlobalStep : 0;
        playingStep = processor.getCurrentStepForUi();
        playingGlobalStep = playingStep > 0 ? processor.getGlobalStepForUi() : 0;
        const bool swingPhaseChanged = state.playback.swing > 0 && previousGlobalStep != playingGlobalStep;
        const bool revised = previousRevision != state.revision;
        if (revised || swingPhaseChanged)
            notify();
        return revised;
    }
    void EditorController::undo()
    {
        refresh();
        commit();
        if (undoEntries.empty())
            return;
        auto entry = std::move (undoEntries.back());
        undoEntries.pop_back();
        applyHistory (entry, false);
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
        applyHistory (entry, true);
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
