#include "KshDrumComputerImport.h"
#include "../engine/KickSnareHatEngine.h"

#include <bit>
#include <cmath>
#include <span>
#include <unordered_map>

namespace ksh::ui
{
    namespace
    {
        using Bytes = std::span<const uint8_t>;
        using Records = std::unordered_map<uint32_t, Bytes>;

        constexpr int trackCount = 8, patternCount = 16, stepsPerTrack = 16, lanesPerPattern = 8;
        constexpr int cellsPerLane = trackCount * stepsPerTrack;
        constexpr int bankSize = patternCount * lanesPerPattern * cellsPerLane;
        constexpr int maximumBytes = 16 * 1024 * 1024;

        uint32_t word (Bytes bytes, size_t index)
        {
            return juce::ByteOrder::littleEndianInt (bytes.data() + index * 4);
        }

        float number (Bytes bytes, size_t index, float fallback = 0.0f)
        {
            if (index >= bytes.size() / 4)
                return fallback;
            const auto value = std::bit_cast<float> (word (bytes, index));
            return std::isfinite (value) ? value : fallback;
        }

        Bytes record (const Records& records, uint32_t id)
        {
            const auto it = records.find (id);
            return it != records.end() ? it->second : Bytes {};
        }

        Bytes floatArray (const Records& records, uint32_t id, uint32_t count)
        {
            const auto bytes = record (records, id);
            if (bytes.size() != (size_t (count) + 2) * 4 || word (bytes, 0) != 0xab4130 || word (bytes, 1) != count)
                return {};
            return bytes.subspan (8);
        }

        std::optional<Records> readRecords (Bytes bytes)
        {
            // DrumComputer's little-endian v9 preset header stores a size in 32-bit words.
            constexpr size_t headerSize = 36;
            if (bytes.size() < headerSize || bytes.size() % 4 != 0 || word (bytes, 0) != bytes.size() / 4
                || word (bytes, 1) != 9 || word (bytes, 8) != 100518)
                return std::nullopt;

            Records records;
            for (auto remaining = bytes.subspan (headerSize); remaining.size() >= 8;)
            {
                const auto id = word (remaining, 0);
                const auto count = word (remaining, 1);
                remaining = remaining.subspan (8);
                // The bank ends here. Some presets begin their name metadata immediately after the marker.
                if (id == 0xffffffff && (count == 0 || count == 0xfffffffd))
                    return records;
                if (count > remaining.size() / 4)
                    break;
                const auto length = size_t (count) * 4;
                records.insert_or_assign (id, remaining.first (length));
                remaining = remaining.subspan (length);
            }
            return std::nullopt;
        }

        juce::String trackLabel (const Records& records, int track)
        {
            constexpr std::array<uint32_t, trackCount> samplerRecords { 989, 1005, 1007, 1009, 1011, 1013, 1015, 1017 };
            const auto bytes = record (records, samplerRecords[static_cast<size_t> (track)]);
            if (bytes.size() < 12 || word (bytes, 0) != 0xab4130)
                return {};
            const auto length = word (bytes, 2);
            if (length == 0 || length > bytes.size() - 12 || length > 1024)
                return {};
            const auto* path = reinterpret_cast<const char*> (bytes.data() + 12);
            if (!juce::CharPointer_UTF8::isValidString (path, static_cast<int> (length)))
                return {};
            auto name = juce::String::fromUTF8 (path, static_cast<int> (length));
            name = name.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
            return name.trim().substring (0, 40);
        }

        struct Grid
        {
            std::string_view rate = Constants::defaultRate;
            std::array<int, trackCount> stepsPerCell { 1, 1, 1, 1, 1, 1, 1, 1 };
            bool approximate = false;
        };

        Grid chooseGrid (const Records& records, const std::array<bool, trackCount>& used)
        {
            // Step durations in 240ths of a quarter note. DrumComputer also has dotted and quintuplet divisions.
            constexpr std::array sourceTicks { 960, 720, 480, 360, 240, 144, 120, 80, 60, 40, 30, 20, 15 };
            constexpr std::array gridTicks { 240, 160, 120, 80, 60, 40, 30, 20 };
            std::array<int, trackCount> durations {};
            for (size_t track = 0; track < durations.size(); ++track)
            {
                const auto rate = number (record (records, 248 + static_cast<uint32_t> (track)), 0, 8.0f);
                const auto index = static_cast<size_t> (std::clamp (rate, 0.0f, 12.0f));
                durations[track] = used[track] ? sourceTicks[index] : 0;
            }
            if (std::ranges::none_of (used, [] (bool active) { return active; }))
                return {};

            for (size_t index = 0; index < gridTicks.size(); ++index)
            {
                const auto ticks = gridTicks[index];
                if (!std::ranges::all_of (durations, [ticks] (int duration)
                    { return duration % ticks == 0 && duration / ticks * stepsPerTrack <= Constants::maxSteps; }))
                    continue;

                Grid grid;
                grid.rate = Constants::rates[index];
                std::transform (durations.begin(), durations.end(), grid.stepsPerCell.begin(),
                                [ticks] (int duration) { return std::max (1, duration / ticks); });
                return grid;
            }
            return { .approximate = true };
        }

        int divisions (float control)
        {
            return 1 + static_cast<int> (7.0f * std::clamp (control, 0.0f, 1.0f));
        }

        int midiVelocity (float normalised)
        {
            return std::max (1, static_cast<int> (127.0f * normalised));
        }
    }

    DrumComputerImport importDrumComputerPreset (const juce::File& file, const juce::String& originalName, double fallbackBpm)
    {
        DrumComputerImport result;
        const auto input = file.createInputStream();
        if (input == nullptr || input->getTotalLength() <= 0 || input->getTotalLength() > maximumBytes)
        {
            result.error = "Couldn't read the DrumComputer preset.";
            return result;
        }

        juce::MemoryBlock data;
        input->readIntoMemoryBlock (data, maximumBytes + 1);
        const Bytes bytes (static_cast<const uint8_t*> (data.getData()), data.getSize());
        const auto records = readRecords (bytes);
        const auto bank = records ? floatArray (*records, 244, bankSize) : Bytes {};
        if (bank.empty() || data.getSize() > maximumBytes)
        {
            result.error = "Unsupported or damaged DrumComputer .sbb preset.";
            return result;
        }

        const auto valueAt = [bank] (int pattern, int track, int step, int lane)
        {
            return number (bank, static_cast<size_t> ((pattern * lanesPerPattern + lane) * cellsPerLane + track * stepsPerTrack + step));
        };
        std::array<bool, trackCount> usedTracks {};
        std::array<bool, patternCount> usedPatterns {};
        for (int pattern = 0; pattern < patternCount; ++pattern)
            for (int track = 0; track < trackCount; ++track)
                for (int step = 0; step < stepsPerTrack; ++step)
                    if (valueAt (pattern, track, step, 0) > 0.0f)
                    {
                        usedTracks[static_cast<size_t> (track)] = true;
                        usedPatterns[static_cast<size_t> (pattern)] = true;
                    }

        const auto bpm = number (bytes, 2);
        if (bpm >= 20.0f && bpm <= 300.0f)
            result.bpm = bpm;
        result.populatedPatterns = static_cast<int> (std::ranges::count (usedPatterns, true));

        const auto rootNotes = floatArray (*records, 158, trackCount);
        const auto loopLengths = floatArray (*records, 844, 1024);
        const auto grid = chooseGrid (*records, usedTracks);
        const int longestCell = *std::ranges::max_element (grid.stepsPerCell);
        const auto swing = std::clamp (number (record (*records, 245), 0), 0.0f, 1.0f);
        const auto humanize = std::clamp (number (record (*records, 246), 0), 0.0f, 1.0f);
        result.approximateTiming = grid.approximate || (longestCell > 1 && humanize > 0.0f);

        auto document = std::make_unique<DocumentState>();
        auto& settings = document->settings;
        settings.staticSource = result.populatedPatterns == 0 ? 0 : static_cast<int> (std::ranges::find (usedPatterns, true) - usedPatterns.begin());
        settings.channelCount = trackCount;
        settings.stepCount = settings.generatedStepCount = settings.refreshSteps = stepsPerTrack * longestCell;
        settings.rate = grid.rate;
        settings.tempo = result.bpm.value_or (fallbackBpm);
        settings.swing = longestCell == 1 ? juce::roundToInt (100.0f * swing) : 0;
        // The source's timing range is about 10% of a step at full scale, with a squared control response.
        settings.timingHumanize = static_cast<int> (std::lround (50.0f * humanize * humanize));

        for (int track = 0; track < trackCount; ++track)
        {
            const auto index = static_cast<size_t> (track);
            auto& channel = settings.channels[index];
            channel = defaultChannel (track);
            channel.note = juce::roundToInt (std::clamp (number (rootNotes, index, static_cast<float> (channel.note)), 0.0f, 127.0f));
            if (const auto label = trackLabel (*records, track); label.isNotEmpty())
                channel.label = label.toStdString();
        }

        for (int pattern = 0; pattern < patternCount; ++pattern)
        {
            const auto source = static_cast<size_t> (pattern);
            auto& patternSettings = settings.sourceSettings[source];
            patternSettings.rate = grid.rate;
            patternSettings.stepCount = settings.stepCount;
            for (int track = 0; track < trackCount; ++track)
            {
                const auto index = static_cast<size_t> (track);
                const int scale = grid.stepsPerCell[index];
                const int length = 1 + juce::roundToInt (std::clamp (number (loopLengths, source * trackCount + index, 15.0f), 0.0f, 15.0f));
                const bool pingPong = number (record (*records, 873 + static_cast<uint32_t> (track)), 0) > 0.5f;
                patternSettings.loopRanges[index] = { 0, length * scale };
                patternSettings.playbackModes[index] = pingPong ? PlaybackMode::ping_pong : PlaybackMode::normal;
                result.approximateTiming |= pingPong && usedTracks[index];

                for (int step = 0; step < stepsPerTrack; ++step)
                {
                    const auto velocity = std::clamp (valueAt (pattern, track, step, 0), 0.0f, 1.0f);
                    auto& cell = document->sources[source][index][static_cast<size_t> (step * scale)];
                    cell.enabled = velocity > 0.0f;
                    if (cell.enabled)
                        cell.velocity = midiVelocity (velocity);
                    // Prb selects an every-Nth-visit condition. The first trigger is on visit N.
                    cell.cycle = divisions (valueAt (pattern, track, step, 1));
                    cell.cycleMask = 1 << (cell.cycle - 1);
                    cell.roll = divisions (valueAt (pattern, track, step, 2));
                    const auto offset = std::clamp (valueAt (pattern, track, step, 3), -1.0f, 1.0f);
                    const auto swingOffset = longestCell > 1 && step % 2 != 0 ? 0.5f * swing : 0.0f;
                    const auto nudge = static_cast<int> (std::lround ((90.0f * offset + 100.0f * swingOffset) * static_cast<float> (scale)));
                    cell.nudge = clampInt (nudge, -100, 100);
                    result.approximateTiming |= cell.enabled && nudge != cell.nudge;
                    if (cell.roll > 1)
                    {
                        cell.span = scale;
                        if (cell.enabled)
                        {
                            const auto repeatVelocity = midiVelocity (velocity * 0.69725f);
                            for (int split = 1; split < cell.roll; ++split)
                                cell.setSplitVelocity (split, repeatVelocity);
                            result.approximateTiming |= scale > 1 && nudge > 0;
                        }
                    }
                }
            }
        }
        const auto& active = settings.sourceSettings[static_cast<size_t> (settings.staticSource)];
        for (size_t track = 0; track < trackCount; ++track)
        {
            settings.channels[track].loopLength = active.loopRanges[track].loopLength;
            settings.channels[track].playbackMode = active.playbackModes[track];
        }

        auto state = KickSnareHatEngine::documentToJson (*document);
        state["projectName"] = (originalName.endsWithIgnoreCase (".sbb") ? originalName.dropLastCharacters (4) : originalName).toStdString();
        auto description = juce::String ("Imported from ") + originalName + ".";
        if (result.approximateTiming)
            description += " Some DrumComputer timing settings were approximated.";
        state["projectDescription"] = description.toStdString();
        state["standaloneTransportPlaying"] = false;
        result.projectData = state.dump();
        return result;
    }
}
