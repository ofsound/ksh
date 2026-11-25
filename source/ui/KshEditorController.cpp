#include "KshEditorController.h"
#include "KshStyle.h"

namespace ksh::ui
{
    namespace
    {
        constexpr int cellCount = Constants::sourceCount * Constants::cellsPerPattern;

        template <typename Document>
        auto& cellAt (Document& document, int index)
        {
            const auto inPattern = index % Constants::cellsPerPattern;
            return document.sources[static_cast<size_t> (index / Constants::cellsPerPattern)]
                                   [static_cast<size_t> (inPattern / Constants::maxSteps)]
                                   [static_cast<size_t> (inPattern % Constants::maxSteps)];
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
            visit (target.span, a.span, b.span);
            if (a.splitGates == b.splitGates && a.splitVelocities == b.splitVelocities)
                return;
            for (size_t i = 0; i < target.splitGates.size(); ++i)
            {
                visit (target.splitVelocities[i], a.splitVelocities[i], b.splitVelocities[i]);
                auto& gate = target.splitGates[i];
                const auto& first = a.splitGates[i];
                const auto& second = b.splitGates[i];
                if (first == second)
                    continue;
                int chance = gate.probability(), length = gate.cycle(), mask = gate.cycleMask();
                visit (chance, first.probability(), second.probability());
                visit (length, first.cycle(), second.cycle());
                visit (mask, first.cycleMask(), second.cycleMask());
                gate.setProbability (chance);
                gate.setCycle (length, mask);
            }
        }

        template <typename Visit>
        void visitSettings (DocumentSettings& target, const DocumentSettings& a, const DocumentSettings& b, Visit&& visit)
        {
            visit (target.stepCount, a.stepCount, b.stepCount);
            visit (target.generatedStepCount, a.generatedStepCount, b.generatedStepCount);
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
                    visit (t.playbackModes[ch], x.playbackModes[ch], y.playbackModes[ch]);
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

    EditorController::EditorController (PluginProcessor& p) : processor (p), state (*uiState)
    {
        processor.copyNativeUiState (state);
        epoch = state.documentEpoch;
        // History belongs to this editor; don't adopt hits from before it opened.
        (void) processor.takePatternRecordingEdits();
        processor.getUiBridge().onNoteHit = [this] (const NativeHit& hit) {
            const auto until = juce::Time::getMillisecondCounterHiRes() + 200.0;
            const auto row = hit.uiChannel - 1;
            if (row < 0 || row >= Constants::maxChannels)
                return;
            if (onPlaybackHit)
                onPlaybackHit (row, hit.velocity);
            if (hit.uiGeneratedStep > 0 && hit.uiGeneratedStep <= Constants::maxSteps)
                generatedFlashes[static_cast<size_t> (row)][static_cast<size_t> (hit.uiGeneratedStep - 1)] = until;
            if (hit.uiSource == source() + 1 && hit.uiSourceStep > 0 && hit.uiSourceStep <= Constants::maxSteps)
            {
                sourceFlashes[static_cast<size_t> (row)][static_cast<size_t> (hit.uiSourceStep - 1)] = juce::Time::getMillisecondCounterHiRes() + sourceFlashDurationMs;
            }
        };
        processor.getUiBridge().onCycleVisit = [this] (const NativeCycleVisit& visit) {
            if (visit.source != source() + 1 || visit.channel < 1 || visit.channel > Constants::maxChannels
                || visit.step < 1 || visit.step > Constants::maxSteps)
                return;
            const auto row = static_cast<size_t> (visit.channel - 1);
            const auto step = static_cast<size_t> (visit.step - 1);
            sourceCycleFlashes[row][step] = juce::Time::getMillisecondCounterHiRes() + 200.0;
            sourceCycleSteps[row][step] = visit.cycleStep;
            sourceSplitCycleSteps[row][step] = visit.splitSteps;
        };
    }
    EditorController::~EditorController()
    {
        commit();
        processor.getUiBridge().onNoteHit = {};
        processor.getUiBridge().onCycleVisit = {};
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
    bool EditorController::canFitSpan (Location location, int span) const
    {
        const int pattern = source();
        return pattern >= 0 && location.valid() && span >= 1 && span <= Constants::maxSteps
            && rollSpanFits (state.playback.sources[static_cast<size_t> (pattern)][static_cast<size_t> (location.channel)],
                             loop (location.channel), location.step, span);
    }
    std::vector<Location> EditorController::selection (bool enabledOnly) const
    {
        std::vector<Location> result;
        for (int ch = 0; ch < state.playback.channelCount; ++ch)
            for (int step = 0; step < state.playback.stepCount; ++step)
            {
                const Location location { ch, step };
                if (selected.test (static_cast<size_t> (location.key())) && (!enabledOnly || cell (location).enabled))
                    result.push_back (location);
            }
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
        processor.copyEditorDocument (documents->before);
        documents->lastOwn = documents->before;
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
        const auto& before = documents->before;
        const auto& lastOwn = documents->lastOwn;
        if (before != lastOwn)
        {
            Entry entry { before.settings, lastOwn.settings, {}, gestureLabel, {} };
            for (int index = 0; index < cellCount; ++index)
            {
                const auto& from = cellAt (before, index);
                const auto& to = cellAt (lastOwn, index);
                if (from != to)
                    entry.cells.push_back ({ index, from, to });
            }
            pushHistory (std::move (entry));
        }
        transaction = false;
        finishParameter();
        notify();
    }
    void EditorController::recordUiEdit (const juce::String& label, std::function<void (bool)> apply)
    {
        commit();
        collectRecordingEdits();
        Entry entry;
        entry.label = label;
        entry.applyUiEdit = std::move (apply);
        pushHistory (std::move (entry));
        notify();
    }
    void EditorController::pushHistory (Entry entry)
    {
        undoEntries.push_back (std::move (entry));
        if (undoEntries.size() > 128)
            undoEntries.erase (undoEntries.begin());
        redoEntries.clear();
    }
    void EditorController::rebaseExternal()
    {
        if (!transaction)
            return;
        auto& current = documents->scratch;
        processor.copyEditorDocument (current);
        // External changes become part of the baseline, never part of this gesture's undo action.
        visitDocument (documents->before, documents->lastOwn, current, rebaseField);
        documents->lastOwn = current;
    }
    void EditorController::applyHistory (const Entry& entry, bool redoing)
    {
        if (entry.applyUiEdit)
        {
            entry.applyUiEdit (redoing);
            return;
        }
        // Conditional merge: a field is written only while it still holds the value this entry left behind.
        auto& current = documents->scratch;
        processor.copyEditorDocument (current);
        const double previousTempo = current.settings.tempo;
        visitSettings (current.settings, redoing ? entry.before : entry.after, redoing ? entry.after : entry.before, mergeField);
        for (const auto& change : entry.cells)
            visitCell (cellAt (current, change.index), redoing ? change.before : change.after, redoing ? change.after : change.before, mergeField);
        applyDocument (current);
        if (std::abs (current.settings.tempo - previousTempo) > 0.01)
            processor.setStandaloneTempoBpm (current.settings.tempo);
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
            auto& current = documents->scratch;
            processor.copyEditorDocument (current);
            visitDocument (current, documents->lastOwn, documents->before, mergeField);
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
        recordingEntry = {};
        recordingSession = 0;
        selected.reset();
        clearSplitCycleSelection();
        copySource = -1;
        soloChannels.reset();
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
    void EditorController::commitRecordingEdits()
    {
        std::erase_if (recordingEntry.cells, [] (const auto& change) { return change.before == change.after; });
        if (recordingEntry.cells.empty())
            return;
        pushHistory (std::exchange (recordingEntry, {}));
    }
    void EditorController::collectRecordingEdits()
    {
        if (epoch != processor.getUiBridge().getDocumentEpoch())
        {
            clearHistory();
            reloadState();
        }
        auto edits = processor.takePatternRecordingEdits();
        if (edits.documentEpoch != epoch)
            return;
        if (recordingSession != edits.session)
        {
            commitRecordingEdits();
            recordingSession = edits.session;
        }
        recordingEntry.label = "Record pattern";
        for (const auto& edit : edits.cells)
        {
            const int index = edit.source * Constants::cellsPerPattern + Location { edit.channel, edit.step }.key();
            const auto existing = std::find_if (recordingEntry.cells.begin(), recordingEntry.cells.end(),
                                               [index] (const auto& change) { return change.index == index; });
            if (existing == recordingEntry.cells.end())
                recordingEntry.cells.push_back ({ index, edit.expected, edit.value });
            else
            {
                visitCell (existing->before, existing->after, edit.expected, rebaseField);
                existing->after = edit.value;
            }
        }
        if (!edits.cells.empty())
            redoEntries.clear();
        if (!edits.armed)
            commitRecordingEdits();
    }
    void EditorController::command (std::string_view selector, const nlohmann::json& args, const juce::String& label)
    {
        collectRecordingEdits();
        const bool single = !transaction && label.isNotEmpty();
        if (single)
            begin (label, juce::String (selector.data(), selector.size()));
        flush();
        rebaseExternal();
        (void) processor.dispatchUiEngineCommand (selector, args);
        collectRecordingEdits();
        if (selector == "pattern_record_enabled")
            rebaseExternal();
        if (transaction)
            processor.copyEditorDocument (documents->lastOwn);
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
            processor.copyEditorDocument (documents->lastOwn);
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
        collectRecordingEdits();
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
        collectRecordingEdits();
        commitRecordingEdits();
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
        collectRecordingEdits();
        commitRecordingEdits();
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
        if (source() != index)
            exitSolo();
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
        if (source() < 0)
            return;
        const auto locations = selection();
        std::vector<std::pair<Location, Cell>> destinations;
        for (auto p : locations)
        {
            const Location destination { wrap (target.channel + p.channel - anchor.channel, state.playback.channelCount),
                                         wrap (target.step + p.step - anchor.step, state.playback.stepCount) };
            destinations.emplace_back (destination, cell (p));
        }
        const auto isSpanned = [] (const auto& entry) { return entry.second.enabled && entry.second.span > 1; };
        if (std::any_of (destinations.begin(), destinations.end(), isSpanned))
        {
            auto pattern = state.playback.sources[static_cast<size_t> (source())];
            if (!copy)
                for (auto p : locations)
                    pattern[static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] = {};
            for (const auto& [p, value] : destinations)
                pattern[static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] = value;
            for (const auto& [p, value] : destinations)
                if (value.enabled && value.span > 1
                    && !rollSpanFits (pattern[static_cast<size_t> (p.channel)], loop (p.channel), p.step, value.span))
                    return;
        }
        if (!copy)
            for (auto p : locations)
                editCell (p, {});
        selected.reset();
        for (const auto& [destination, value] : destinations)
        {
            editCell (destination, value);
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
    void EditorController::clearRow (int ch)
    {
        if (source() < 0)
            return;
        begin ("Clear channel");
        for (int i = 0; i < Constants::maxSteps; ++i)
            editCell ({ ch, i }, {});
        commit();
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
    bool EditorController::applyJamTake (const JamTake& take, int source, uint64_t documentEpoch)
    {
        commit();
        collectRecordingEdits();
        begin ("Jam recording");
        const bool applied = processor.applyJamTake (take, source, documentEpoch);
        if (applied)
        {
            processor.copyEditorDocument (documents->lastOwn);
            selected.reset();
            soloChannels.reset();
        }
        refresh();
        commit();
        return applied;
    }
    void EditorController::toggleMute (int ch)
    {
        const int pattern = source();
        if (pattern < 0)
            return;
        soloChannels.reset();
        command ("source_channel_mute", { pattern + 1, ch + 1, !state.playback.sourceChannelMutes[static_cast<size_t> (pattern)][static_cast<size_t> (ch)] }, "Mute channel");
    }

    EditorController::SoloChannels EditorController::soloedChannels() const
    {
        if (soloChannels.none() || soloSource != source())
            return {};
        const auto& mutes = state.playback.sourceChannelMutes[static_cast<size_t> (soloSource)];
        for (int i = 0; i < state.playback.channelCount; ++i)
            if (mutes[static_cast<size_t> (i)] == soloChannels.test (static_cast<size_t> (i)))
                return {};
        if ((soloChannels >> static_cast<size_t> (state.playback.channelCount)).any())
            return {};
        return soloChannels;
    }

    bool EditorController::exitSolo()
    {
        if (soloedChannels().none())
            return false;
        const int pattern = soloSource;
        soloChannels.reset();
        begin ("Exit solo");
        for (int i = 0; i < state.playback.channelCount; ++i)
            command ("source_channel_mute", { pattern + 1, i + 1, soloRestore[static_cast<size_t> (i)] });
        commit();
        notify();
        return true;
    }

    void EditorController::toggleSolo (int ch, bool extend)
    {
        const int pattern = source();
        if (pattern < 0 || !juce::isPositiveAndBelow (ch, state.playback.channelCount))
            return;
        const auto previous = soloedChannels();
        const auto channel = static_cast<size_t> (ch);
        auto next = extend ? previous : SoloChannels {};
        if (extend)
            next.flip (channel);
        else if (previous.count() != 1 || !previous.test (channel))
            next.set (channel);
        if (next.none())
        {
            exitSolo();
            return;
        }
        if (previous.none())
            soloRestore = state.playback.sourceChannelMutes[static_cast<size_t> (pattern)];
        begin ("Solo channel");
        for (int i = 0; i < state.playback.channelCount; ++i)
            command ("source_channel_mute", { pattern + 1, i + 1, !next.test (static_cast<size_t> (i)) });
        soloSource = pattern;
        soloChannels = next;
        commit();
        notify();
    }
    void EditorController::selectSplitCycle (Location location, int index)
    {
        selected.reset();
        selected.set (static_cast<size_t> (location.key()));
        splitCycleSelection = SplitCycleSelection { location, index };
        splitCycleSource = source();
    }

    std::optional<EditorController::SplitCycleSelection> EditorController::selectedSplitCycle() const
    {
        if (!splitCycleSelection || source() != splitCycleSource || selected.count() != 1)
            return {};
        const auto [location, index] = *splitCycleSelection;
        if (location.channel >= state.playback.channelCount || location.step >= state.playback.stepCount
            || !selected.test (static_cast<size_t> (location.key())))
            return {};
        const auto& step = cell (location);
        return step.enabled && step.roll > 1 && juce::isPositiveAndBelow (index, step.roll) ? splitCycleSelection : std::nullopt;
    }

    void EditorController::setCycle (int length, int mask, const std::vector<Location>& locations, bool editAllSplits)
    {
        const auto split = editAllSplits ? selectedSplitCycle() : std::nullopt;
        for (auto p : locations)
        {
            auto c = cell (p);
            if (split && split->location == p)
                c.splitGates[static_cast<size_t> (split->index)].setCycle (length, mask);
            else if (editAllSplits && c.roll > 1)
            {
                for (int part = 0; part < c.roll; ++part)
                    c.splitGates[static_cast<size_t> (part)].setCycle (length, mask);
            }
            else
            {
                c.cycle = juce::jlimit (1, 64, length);
                c.cycleMask = normalizeCycleMask (mask, c.cycle);
            }
            editCell (p, c);
        }
        notify();
    }
}
