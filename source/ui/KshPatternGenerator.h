#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"
#include "../engine/KshMath.h"
#include <bitset>
#include <span>

namespace ksh::ui
{
    inline constexpr int generatorDrumRows = 6;
    inline constexpr int closedHatRow = 2;
    inline constexpr int openHatRow = 3;

    struct DrumFill
    {
        int row;
        double beatsBeforeEnd, duration;
        float variation;
        int splits;
        std::array<int, 4> velocities;
    };

    struct DrumGroove
    {
        const char* name;
        std::array<std::string_view, generatorDrumRows> parts;
        DrumFill answer;
        std::array<DrumFill, 2> turnaround;
    };

    // Each group is one beat of sixteenths. X is the foundation, x and + are
    // density layers, g is a ghost note, and a plays on the answering bar.
    inline constexpr std::array drumGrooves {
        DrumGroove { "Four on the Floor",
                     { "X... X... X... X...", ".... X... ...g X...", "x.X+ x.Xg x.X+ x.Xg",
                       "..x. ..+. ..a. ..x.", ".... ..x. .... .a+.", ".... ...+ .... .xg." },
                     { 2, 1.0, 0.5, 0.45f, 2, { 54, 28 } },
                     { DrumFill { 5, 0.5, 0.5, 0.34f, 4, { 0, 32, 55, 91 } },
                       DrumFill { 2, 1.0, 0.5, 0.63f, 4, { 49, 26, 63, 36 } } } },
        DrumGroove { "Half-Time",
                     { "X... ..x. ..X. .a+.", "...g .... X... ..g.", "X.x. ..Xg x.X. X.x+",
                       "..x. .... ...+ ....", ".... ..a. .... .x..", ".... .... .x.. ..+." },
                     { 2, 0.5, 0.5, 0.45f, 3, { 55, 26, 68 } },
                     { DrumFill { 2, 1.0, 1.0, 0.32f, 3, { 79, 30, 57 } },
                       DrumFill { 1, 0.5, 0.5, 0.62f, 3, { 0, 0, 85 } } } },
        DrumGroove { "Broken Beat",
                     { "X..X ..X. ...X ..a.", "...g X... ..g. .X..", "x.X. .x+X .xX. x.g+",
                       ".... ..x. .... ..x.", "...x .... a... ..+.", ".... .+.x .... x..." },
                     { 4, 0.5, 0.5, 0.45f, 2, { 61, 39 } },
                     { DrumFill { 1, 0.5, 0.5, 0.37f, 4, { 0, 22, 45, 98 } },
                       DrumFill { 5, 0.75, 0.5, 0.61f, 3, { 71, 31, 64 } } } }
    };

    inline float drumPatternNoise (int channel, int step, int groove, int family)
    {
        auto value = static_cast<juce::uint32> (channel * 193 + step * 977 + family * 7919 + groove * 104729 + 12345);
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        return static_cast<float> (value % 1000) / 999.0f;
    }

    struct DrumGenerator
    {
        int groove;
        float density, variation;
        double beatsPerStep;

        using Steps = std::bitset<Constants::maxSteps>;
        static constexpr float chaosStart = 0.8f;

        void generateRow (std::span<Cell> row, int channel, const Steps& openHats = {}) const
        {
            const int length = static_cast<int> (row.size());
            const int barSteps = juce::jmax (1, gridSpanForBeats (4.0, beatsPerStep));
            const float chaos = juce::jlimit (0.0f, 1.0f, (variation - chaosStart) / (1.0f - chaosStart));
            Steps anchors;
            if (channel < generatorDrumRows)
                writeGroove (row, channel, barSteps, anchors);

            // Bring in the two nearby percussion rows first, then open up the
            // rest of the kit in the last stretch of the pad.
            const float extraStart = channel < 8 ? chaosStart + (channel - generatorDrumRows) * 0.025f
                                                 : 0.87f + (channel - 8) * 0.008f;
            const float extraAmount = juce::jlimit (0.0f, 1.0f, (variation - extraStart) / (1.0f - extraStart));
            for (int step = 0; step < length; ++step)
            {
                const auto index = static_cast<size_t> (step);
                auto& cell = row[index];
                if (openHats[index])
                {
                    cell = {};
                    continue;
                }

                const auto noise = [=, this] (int family) { return drumPatternNoise (channel, step, groove, family); };
                if (channel >= generatorDrumRows)
                    cell.enabled = noise (101) < extraAmount * (0.06f + 0.86f * density);
                else if (chaos > 0.0f && noise (103) <= chaos)
                {
                    anchors.reset (index);
                    cell.enabled = noise (101) < 0.06f + 0.86f * density;
                }
                else
                    continue;

                if (cell.enabled)
                    cell.velocity = juce::roundToInt (26.0f + 95.0f * noise (211));
                else
                    cell = {};
            }

            Steps fills;
            if (chaos < 0.5f && channel < generatorDrumRows)
            {
                const auto& part = drumGrooves[static_cast<size_t> (groove)];
                for (const auto& fill : part.turnaround)
                    writeFill (row, channel, length, fill, anchors, openHats, fills);

                // Give two-bar loops a smaller fill at the midpoint, and longer
                // loops an answer every two bars before the final turnaround.
                const int firstAnswer = length < 3 * barSteps ? barSteps : 2 * barSteps;
                for (int end = firstAnswer; end < length; end += 2 * barSteps)
                    writeFill (row, channel, end, part.answer, anchors, openHats, fills);
            }
            if (chaos > 0.0f)
                addChaos (row, channel, chaos, openHats | fills);
        }

    private:
        void writeGroove (std::span<Cell> row, int channel, int barSteps, Steps& anchors) const
        {
            const auto pattern = drumGrooves[static_cast<size_t> (groove)].parts[static_cast<size_t> (channel)];
            constexpr std::array accentVelocities { 112, 108, 76, 82, 80, 88 };
            const int accent = accentVelocities[static_cast<size_t> (channel)];
            const int length = static_cast<int> (row.size());
            for (int bar = 0, start = 0; start < length; ++bar, start += barSteps)
            {
                for (int pulse = 0; pulse < 16; ++pulse)
                {
                    const char note = pattern[static_cast<size_t> (pulse + pulse / 4)];
                    if (note == '.' || (note == 'a' && bar % 2 == 0))
                        continue;

                    const int step = start + (pulse * barSteps + 8) / 16;
                    if (step >= juce::jmin (length, start + barSteps))
                        continue;

                    const float priority = drumPatternNoise (channel, pulse + 16 * bar, groove, 0);
                    const bool anchor = note == 'X';
                    const bool ghost = note == 'g';
                    const float threshold = ghost ? 0.43f - 0.30f * variation
                                                   : note == '+' ? 0.53f : 0.15f;
                    if (!anchor && density < threshold + 0.18f * priority)
                        continue;

                    const int target = anchor ? accent : ghost ? 27 : accent - (note == '+' ? 32 : 20);
                    const int velocity = juce::jlimit (12, 125, target + juce::roundToInt ((priority - 0.5f) * (8.0f + variation * 22.0f)));
                    auto& cell = row[static_cast<size_t> (step)];
                    if (!cell.enabled || velocity > cell.velocity)
                    {
                        cell.enabled = true;
                        cell.velocity = velocity;
                    }
                    if (anchor)
                        anchors.set (static_cast<size_t> (step));
                }
            }

            // At fine grid divisions a short loop can end before its first
            // backbeat. Give it a snare near the middle of the loop.
            if (channel == 1 && length > 1 && anchors.none())
            {
                const auto step = static_cast<size_t> (length / 2);
                row[step] = Cell { .enabled = true, .velocity = accent };
                anchors.set (step);
            }
        }

        void writeFill (std::span<Cell> row, int channel, int end, const DrumFill& fill,
                        const Steps& anchors, const Steps& openHats, Steps& fills) const
        {
            if (fill.row != channel || variation < fill.variation)
                return;

            const int lead = juce::jmax (1, juce::roundToInt (fill.beatsBeforeEnd / beatsPerStep));
            const int tail = juce::roundToInt ((fill.beatsBeforeEnd - fill.duration) / beatsPerStep);
            const int start = end - lead;
            const int span = lead - tail;
            if (start < 0 || span < 1)
                return;

            for (int step = start; step < start + span; ++step)
            {
                const auto index = static_cast<size_t> (step);
                if (fills[index] || openHats[index] || (channel < closedHatRow && step > start && anchors[index]))
                    return;
            }

            const auto first = static_cast<size_t> (start);
            auto& cell = row[first];
            // Coarse divisions can put an existing kick or snare on the first
            // split. Keep that stroke at the original time and velocity.
            auto velocities = fill.velocities;
            if (channel < closedHatRow && anchors[first])
                velocities[0] = cell.velocity;

            cell = {};
            cell.enabled = true;
            cell.velocity = *std::max_element (velocities.begin(), velocities.end());
            cell.roll = fill.splits;
            cell.span = span;
            for (int part = 0; part < fill.splits; ++part)
            {
                const auto index = static_cast<size_t> (part);
                // Zero velocity inherits the parent velocity. A zero-probability
                // split leaves a true rest while the last stroke can still play.
                if (velocities[index] == 0)
                    cell.splitGates[index].setProbability (0);
                else
                    cell.setSplitVelocity (part, velocities[index]);
            }

            fills.set (first);
            for (int offset = 1; offset < span; ++offset)
            {
                row[static_cast<size_t> (start + offset)] = {};
                fills.set (static_cast<size_t> (start + offset));
            }
        }

        void addChaos (std::span<Cell> row, int channel, float amount, const Steps& reserved) const
        {
            constexpr std::array rollCounts { 2, 3, 4, 5, 6, 8 };
            const int length = static_cast<int> (row.size());
            const int maxRollIndex = juce::roundToInt (amount * static_cast<float> (rollCounts.size() - 1));

            for (int step = 0; step < length;)
            {
                const auto index = static_cast<size_t> (step);
                auto& cell = row[index];
                if (cell.enabled && !reserved[index])
                {
                    const auto noise = [=, this] (int family) { return drumPatternNoise (channel, step, groove, family); };
                    cell.nudge = juce::roundToInt (amount * (noise (307) - 0.5f) * 90.0f);
                    if (noise (401) <= amount)
                        cell.probability = 100 - juce::jmax (1, juce::roundToInt (amount * juce::jmap (noise (409), 5.0f, 45.0f)));
                    if (noise (419) <= amount)
                    {
                        cell.cycle = 2 + juce::roundToInt (noise (421) * 4.0f);
                        const int skip = juce::roundToInt (noise (431) * static_cast<float> (cell.cycle - 1));
                        cell.cycleMask = cycleMaskForLength (cell.cycle) & ~(1 << skip);
                    }

                    if (noise (503) < amount * 0.76f)
                    {
                        const auto roll = static_cast<size_t> (juce::roundToInt (noise (541) * static_cast<float> (maxRollIndex)));
                        cell.roll = rollCounts[roll];
                        if (noise (509) < amount * 0.66f)
                        {
                            const int maxSpan = juce::jmin (length - step, 2 + juce::roundToInt (noise (547) * amount * 2.0f));
                            while (cell.span < maxSpan && !reserved[index + static_cast<size_t> (cell.span)])
                            {
                                row[index + static_cast<size_t> (cell.span)] = {};
                                ++cell.span;
                            }
                        }

                        for (int part = 0; part < cell.roll; ++part)
                        {
                            const float scale = part == cell.roll - 1 ? 1.15f : juce::jmap (noise (557 + part), 0.20f, 1.0f);
                            cell.setSplitVelocity (part, juce::jlimit (1, 127, juce::roundToInt (cell.velocity * scale)));
                        }
                    }
                }
                step += cell.span;
            }
        }
    };

    class PatternXYPad : public juce::Component
    {
    public:
        int rowLimit = 8;
        explicit PatternXYPad (EditorController& c) : controller (c)
        {
            setTitle ("Drum pattern density and variation");
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
        }
        ~PatternXYPad() override { cancelGesture(); }
        void cancelGesture()
        {
            if (std::exchange (dragging, false) && controller.editing())
                controller.cancel();
        }
        void visibilityChanged() override { if (!isVisible()) cancelGesture(); }
        bool hitTest (int x, int y) override { return padBounds().contains (static_cast<float> (x), static_cast<float> (y)); }
        void selectGroove (int next)
        {
            groove = next;
            if (controller.source() < 0)
                return;
            cancelGesture();
            controller.commit();
            controller.begin ("Generate drum groove");
            controller.command ("mode", { "static" });
            generate();
            controller.commit();
        }
        void paint (juce::Graphics& g) override
        {
            const auto area = padBounds();
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (area, 8);
            g.setColour (Palette::border.withAlpha (0.6f));
            for (int i = 1; i < 4; ++i)
            {
                const float fraction = static_cast<float> (i) / 4.0f;
                g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * fraction), area.getY(), area.getBottom());
                g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * fraction), area.getX(), area.getRight());
            }
            g.drawRoundedRectangle (area.reduced (0.5f), 8, 1);
            const auto point = juce::Point<float> (area.getX() + density * area.getWidth(), area.getBottom() - variation * area.getHeight());
            g.setColour (Palette::accent.withAlpha (0.15f));
            g.fillEllipse (point.x - 22, point.y - 22, 44, 44);
            g.setColour (Palette::accent);
            g.fillEllipse (point.x - 8, point.y - 8, 16, 16);
            g.setColour (Palette::text);
            g.drawEllipse (point.x - 8, point.y - 8, 16, 16, 1.5f);
            text (g, "Chaotic", { area.getX(), 0, area.getWidth(), 22 }, 14, Palette::muted);
            text (g, "Basic", { area.getX(), area.getBottom() + 4, area.getWidth(), 22 }, 14, Palette::muted);
            text (g, "Sparse", { area.getX() + 10, area.getBottom() - 28, 80, 22 }, 14, Palette::muted, false, juce::Justification::centredLeft);
            text (g, "Dense", { area.getRight() - 90, area.getBottom() - 28, 80, 22 }, 14, Palette::muted, false, juce::Justification::centredRight);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (!event.mods.isLeftButtonDown() || !padBounds().contains (event.position) || controller.source() < 0)
                return;
            controller.commit();
            controller.begin ("Generate drum pattern");
            controller.command ("mode", { "static" });
            source = controller.source();
            epoch = controller.processor.getUiBridge().getDocumentEpoch();
            dragging = true;
            mouseDrag (event);
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (!dragging)
                return;
            if (!controller.editing() || source != controller.source() || epoch != controller.processor.getUiBridge().getDocumentEpoch())
            {
                dragging = false;
                return;
            }
            const auto area = padBounds();
            density = std::round (juce::jlimit (0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth()) * 127.0f) / 127.0f;
            variation = std::round (juce::jlimit (0.0f, 1.0f, (area.getBottom() - event.position.y) / area.getHeight()) * 127.0f) / 127.0f;
            generate();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (std::exchange (dragging, false))
            {
                controller.commit();
                controller.notify();
            }
        }
    private:
        void generate()
        {
            const auto& pattern = controller.state.playback;
            const DrumGenerator generator { groove, density, variation, pattern.beatsPerStep };
            const int rows = juce::jmin (pattern.channelCount, rowLimit);
            DrumGenerator::Steps openHatSteps;
            const auto generateRow = [&] (int channel)
            {
                if (controller.isControllerRow (channel))
                    return;
                const auto loop = controller.loop (channel);
                std::array<Cell, Constants::maxSteps> cells;
                const int length = juce::jlimit (0, loop.loopLength, pattern.stepCount - loop.loopStart);
                const auto row = std::span (cells).first (static_cast<size_t> (length));
                generator.generateRow (row, channel, channel == closedHatRow ? openHatSteps >> static_cast<size_t> (loop.loopStart) : DrumGenerator::Steps {});
                for (int step = 0; step < length; ++step)
                {
                    const auto& cell = row[static_cast<size_t> (step)];
                    const int position = loop.loopStart + step;
                    if (channel == openHatRow && cell.enabled)
                        for (int part = 0; part < cell.span; ++part)
                            openHatSteps.set (static_cast<size_t> (position + part));
                    controller.editCell ({ channel, position }, cell);
                }
            };
            // Generate open hats first so closed hats can respect their actual loop and splits.
            if (rows > openHatRow)
                generateRow (openHatRow);
            for (int channel = 0; channel < rows; ++channel)
                if (channel != openHatRow)
                    generateRow (channel);
            controller.flush();
            controller.notify();
            repaint();
        }
        juce::Rectangle<float> padBounds() const
        {
            const auto bounds = getLocalBounds().toFloat().reduced (10, 26);
            const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
            return bounds.withSizeKeepingCentre (side, side);
        }
        EditorController& controller;
        bool dragging = false;
        int source = -1, groove = 0;
        uint64_t epoch = 0;
        float density = 0.4f, variation = 0.0f;
    };
}
