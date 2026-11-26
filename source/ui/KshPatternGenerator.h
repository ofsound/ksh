#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"
#include "../engine/KshMath.h"
#include <bitset>
#include <span>

namespace ksh::ui
{
    // Stable priorities describe drum roles across a sixteen-step phrase. Increasing
    // density adds supporting hits while the kick/downbeat and snare/backbeat remain.
    inline constexpr int generatorSeedCount = 5;

    inline float drumPatternNoise (int channel, int step, int seed, int family)
    {
        auto value = static_cast<juce::uint32> (channel * 193 + step * 977 + family * 7919 + seed * 104729 + 12345);
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        return static_cast<float> (value % 1000) / 999.0f;
    }

    inline Cell generatedDrumCell (int channel, int step, float density, float variation, int seed = 0)
    {
        static constexpr int priorities[8][16] {
            { 0, 95, 72, 48, 62, 92, 38, 78, 12, 90, 55, 32, 60, 85, 42, 70 },
            { 95, 90, 80, 64, 0, 85, 74, 56, 90, 82, 68, 54, 0, 78, 62, 44 },
            { 20, 76, 8, 66, 25, 70, 10, 58, 18, 72, 6, 62, 26, 68, 12, 52 },
            { 100, 100, 66, 100, 100, 100, 46, 100, 100, 100, 64, 100, 100, 100, 38, 90 },
            { 100, 100, 92, 90, 45, 100, 86, 78, 100, 100, 90, 80, 36, 96, 78, 68 },
            { 100, 100, 94, 100, 100, 92, 100, 82, 100, 100, 88, 76, 98, 65, 54, 42 },
            { 90, 74, 88, 48, 92, 62, 86, 58, 94, 66, 82, 44, 90, 56, 78, 50 },
            { 62, 100, 100, 100, 100, 100, 94, 100, 85, 100, 100, 100, 100, 100, 90, 100 }
        };
        static constexpr int phaseOffsets[generatorSeedCount][8] {
            { 0, 0, 0, 0, 0, 0, 0, 0 },
            { 3, 1, 1, 3, 1, 4, 2, 3 },
            { 6, 3, 2, 1, 4, 2, 5, 1 },
            { 9, 7, 3, 5, 2, 6, 3, 5 },
            { 12, 5, 5, 2, 7, 3, 6, 6 }
        };
        const int phase = step % 16;
        const auto hash = [channel, step, seed] (int family)
        {
            return drumPatternNoise (channel, step, seed, family);
        };
        const float position = juce::jlimit (0.0f, 1.0f, variation) * 15.0f;
        const int family = static_cast<int> (position);
        const float blend = position - static_cast<float> (family);
        const auto variationNoise = [&] (int offset)
        {
            return juce::jmap (blend, hash (family + offset), hash (family + offset + 1));
        };
        const float noise = variationNoise (0);
        const float chaos = juce::jlimit (0.0f, 1.0f, (variation - 0.8f) * 5.0f);
        const int priority = priorities[channel][phase];
        const bool anchor = priority == 0;
        const int shifted = priorities[channel][(phase + phaseOffsets[seed][channel]) % 16];
        // Change supporting rhythms while retaining the kick and snare anchors.
        const float seededPriority = seed == 0 ? static_cast<float> (priority)
            : 0.30f * static_cast<float> (priority) + 0.70f * static_cast<float> (shifted);
        const float structured = anchor ? -12.0f : seededPriority + (noise - 0.5f) * variation * 65.0f;
        // At the top of the pad, even the kick and snare anchors give way to independent hits.
        const float threshold = juce::jmap (chaos, structured, 100.0f * variationNoise (101));
        Cell result;
        result.enabled = threshold < 8.0f + juce::jlimit (0.0f, 1.0f, density) * 78.0f;
        const bool strong = anchor || phase % 4 == 0;
        const float roleVelocity = (strong ? 100.0f : phase % 2 == 0 ? 77.0f : 48.0f)
                                   + density * 12.0f + (noise - 0.5f) * 16.0f;
        result.velocity = juce::jlimit (30, 120, juce::roundToInt (juce::jmap (chaos, roleVelocity, 30.0f + 90.0f * variationNoise (211))));
        result.nudge = juce::roundToInt (chaos * (variationNoise (307) - 0.5f) * 90.0f);
        constexpr float chanceStart = 0.7f;
        if (result.enabled && variation > chanceStart)
        {
            const float amount = juce::jmin (1.0f, (variation - chanceStart) / (1.0f - chanceStart));
            // Fixed per-step thresholds bring in each modifier independently as variation rises.
            if (hash (401) <= amount)
                result.probability = 100 - juce::jmax (1, juce::roundToInt (amount * juce::jmap (hash (409), 5.0f, 45.0f)));
            if (hash (419) <= amount)
            {
                result.cycle = 2 + juce::roundToInt (hash (421) * 4.0f);
                const int skippedBeat = juce::roundToInt (hash (431) * static_cast<float> (result.cycle - 1));
                result.cycleMask = cycleMaskForLength (result.cycle) & ~(1 << skippedBeat);
            }
        }
        return result;
    }

    inline void addGeneratedDrumRolls (std::span<Cell> row, int channel, int seed, float variation, double beatsPerStep,
                                       const std::bitset<Constants::maxSteps>& openHatSteps)
    {
        constexpr float tripletStart = 0.10f;
        constexpr float chaosStart = 0.8f;
        const bool tripletSeed = seed == 1 || seed == 3;
        const bool coreTriplets = tripletSeed && channel == 2 && variation >= tripletStart;
        if (row.empty() || (variation <= chaosStart && !coreTriplets))
            return;

        const float amount = juce::jlimit (0.0f, 1.0f, (variation - chaosStart) / (1.0f - chaosStart));
        constexpr std::array rollCounts { 2, 4, 5, 6, 7, 8 };
        const int maxRollIndex = juce::roundToInt (amount * static_cast<float> (rollCounts.size() - 1));
        const int length = static_cast<int> (row.size());
        const std::array tripletSpans { gridSpanForBeats (0.5, beatsPerStep), gridSpanForBeats (1.0, beatsPerStep), gridSpanForBeats (2.0, beatsPerStep) };
        const int phraseLength = juce::jlimit (1, length, gridSpanForBeats (4.0, beatsPerStep));
        int coreSpan = 0;
        if (coreTriplets)
        {
            // B uses a short hat fill. D lets it carry the whole beat.
            const auto order = seed == 1 ? std::array { 0, 1, 2 } : std::array { 1, 0, 2 };
            for (const int index : order)
            {
                const int span = tripletSpans[static_cast<size_t> (index)];
                if (span > 0 && span <= phraseLength)
                {
                    coreSpan = span;
                    break;
                }
            }
        }
        const int firstTriplet = juce::jmin (phraseLength - coreSpan, phraseLength / (seed == 1 ? 4 : 2));
        const int secondTriplet = seed == 1 ? juce::jmin (phraseLength - coreSpan, phraseLength * 3 / 4) : 0;

        for (int step = 0; step < length;)
        {
            auto& cell = row[static_cast<size_t> (step)];
            const int remaining = length - step;
            const int phase = step % phraseLength;
            const auto noise = [channel, step, seed] (int family) { return drumPatternNoise (channel, step, seed, family); };
            if (coreSpan > 0 && coreSpan <= remaining && (cell.enabled || variation <= chaosStart)
                && !openHatSteps.test (static_cast<size_t> (step))
                && (phase == firstTriplet || (variation >= 0.38f && phase == secondTriplet)))
            {
                cell.enabled = true;
                cell.roll = 3;
                cell.span = coreSpan;
                cell.velocity = juce::jmax (cell.velocity, 77);
                cell.setSplitVelocity (1, juce::roundToInt (cell.velocity * 0.70));
                cell.setSplitVelocity (2, juce::roundToInt (cell.velocity * 0.85));
            }
            else if (cell.enabled && noise (503) < amount * 0.76f)
            {
                const bool stretch = remaining > 1 && noise (509) < amount * 0.67f;
                int tripletSpan = 0;
                if (stretch && tripletSeed && noise (521) < 0.66f)
                {
                    // B favours 16ths and 8ths. D also reaches quarter-note triplets.
                    const int longest = seed == 1 ? 1 : 2;
                    const int preferred = juce::roundToInt (noise (523) * static_cast<float> (longest));
                    for (int i = preferred; i >= 0; --i)
                    {
                        const int candidate = tripletSpans[static_cast<size_t> (i)];
                        if (candidate > 0 && candidate <= remaining)
                        {
                            tripletSpan = candidate;
                            break;
                        }
                    }
                }

                const auto rollIndex = static_cast<size_t> (juce::roundToInt (noise (541) * static_cast<float> (maxRollIndex)));
                cell.roll = tripletSpan > 0 ? 3 : rollCounts[rollIndex];
                if (tripletSpan > 0)
                    cell.span = tripletSpan;
                else if (stretch)
                    cell.span = juce::jmin (remaining, 2 + juce::roundToInt (noise (547) * amount * 2.0f));

                for (int part = 0; part < cell.roll; ++part)
                {
                    const int offset = juce::roundToInt ((noise (557 + part) - 0.57f) * amount * 44.0f);
                    cell.setSplitVelocity (part, juce::jlimit (1, 127, cell.velocity + offset));
                }
            }
            // The span replaces its covered steps, just as a hand-entered roll does.
            for (int covered = 1; covered < cell.span; ++covered)
                row[static_cast<size_t> (step + covered)] = {};
            step += cell.span;
        }
    }

    class PatternXYPad : public juce::Component
    {
    public:
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
        void selectSeed (int next)
        {
            if (next == seed)
                return;
            seed = next;
            if (controller.source() < 0)
                return;
            cancelGesture();
            controller.begin ("Change generator seed");
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
            text (g, "Straight", { area.getX(), area.getBottom() + 4, area.getWidth(), 22 }, 14, Palette::muted);
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
            for (int ch = 0; ch < pattern.channelCount; ++ch)
            {
                if (controller.isControllerRow (ch))
                    continue;
                const auto loop = controller.loop (ch);
                std::array<Cell, Constants::maxSteps> cells;
                std::bitset<Constants::maxSteps> openHatSteps;
                const int length = juce::jlimit (0, loop.loopLength, pattern.stepCount - loop.loopStart);
                auto row = std::span (cells).first (static_cast<size_t> (length));
                for (int step = 0; step < length; ++step)
                {
                    auto& cell = row[static_cast<size_t> (step)];
                    cell = generatedDrumCell (ch, step, density, variation, seed);
                    // An open hat takes the place of its closed counterpart.
                    if (ch == 2 && pattern.channelCount > 3 && !controller.isControllerRow (3)
                        && generatedDrumCell (3, step, density, variation, seed).enabled)
                    {
                        openHatSteps.set (static_cast<size_t> (step));
                        cell.enabled = false;
                    }
                }
                addGeneratedDrumRolls (row, ch, seed, variation, pattern.beatsPerStep, openHatSteps);
                for (int step = 0; step < length; ++step)
                    controller.editCell ({ ch, loop.loopStart + step }, row[static_cast<size_t> (step)]);
            }
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
        int source = -1, seed = 0;
        uint64_t epoch = 0;
        float density = 0.4f, variation = 0.0f;
    };
}
