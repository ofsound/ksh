#include "KshSwingImport.h"
#include "../engine/KickSnareHatEngine.h"
#include "../engine/KshMath.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <span>

namespace ksh::ui
{
    namespace
    {
        constexpr auto maxMidiBytes = 16 * 1024 * 1024;
        constexpr auto maxWaveSeconds = 45.0;
        constexpr size_t maxFitHits = 220;
        constexpr auto tolerance = 0.18; // in grid steps

        struct Hit
        {
            double time;
            double weight = 1.0;
        };

        struct Fit
        {
            double score = -1.0;
            double phase = 0.0;
            int swing = 0, target = 0;
            bool evidence = false;
        };

        double nearestStep (double position, double first, int group)
        {
            const auto middle = 0.5 * (group - 1);
            const auto relative = std::remainder (position - first - middle, 2.0 * group) + middle;
            return position + std::clamp (std::round (relative), 0.0, group - 1.0) - relative;
        }

        Fit scoreFit (const std::vector<Hit>& hits, double step, double phase, int swing, int target)
        {
            const auto group = static_cast<int> (swingSubdivisionValueForIndex (target));
            const auto delay = swingDelayStepsForPosition (group, swing, target);
            double score = 0.0, weight = 0.0;
            std::array<double, 2> lastCycle { -1.0e9, -1.0e9 };
            std::array<int, 2> cycleCount {};
            double latestCycle = -1.0e9;
            int pairedCycles = 0;

            for (const auto& hit : hits)
            {
                const auto position = hit.time / step - phase;
                const auto straight = nearestStep (position, 0.0, group);
                const auto swung = nearestStep (position, group + delay, group);
                const auto shifted = std::abs (position - swung) < std::abs (position - straight);
                const auto distance = std::abs (position - (shifted ? swung : straight));
                weight += hit.weight;
                score += hit.weight * std::max (0.0, 1.0 - distance / tolerance);

                if (distance > tolerance)
                    continue;

                const size_t branch = shifted ? 1 : 0;
                const auto cycle = std::floor (((shifted ? swung - delay : straight) + 0.01) / (2.0 * group));
                if (cycle > lastCycle[branch])
                {
                    lastCycle[branch] = cycle;
                    ++cycleCount[branch];
                    if (cycle > latestCycle && std::abs (cycle - lastCycle[1 - branch]) < 0.01)
                    {
                        latestCycle = cycle;
                        ++pairedCycles;
                    }
                }
            }

            const auto evidence = pairedCycles >= 2
                               || (cycleCount[0] >= 2 && cycleCount[1] >= 2 && hits.size() >= 6);
            return { score / weight, phase, swing, target, evidence };
        }

        struct GridFit
        {
            Fit straight, supportedStraight, swung;
            bool ambiguousSwing = false;

            Fit rhythm() const { return swung.score > straight.score + 0.04 ? swung : straight; }

            std::optional<ImportedSwing> result() const
            {
                if (! ambiguousSwing && swung.score >= 0.46 && swung.score > straight.score + 0.065)
                    return ImportedSwing { swung.swing, swung.target };
                if (supportedStraight.score >= 0.62 && swung.score < straight.score + 0.04)
                    return ImportedSwing {};
                return {};
            }
        };

        GridFit fitGrid (const std::vector<Hit>& hits, double step, bool freePhase)
        {
            GridFit fit;
            for (int target = 0; target < Constants::swingSubdivisionCount; ++target)
            {
                const auto group = static_cast<int> (swingSubdivisionValueForIndex (target));
                const auto anchors = freePhase ? std::min (size_t { 5 }, hits.size()) : size_t { 1 };
                Fit bestForTarget;
                for (size_t anchor = 0; anchor < anchors; ++anchor)
                {
                    for (int offset = 0; offset < (freePhase ? 2 * group : 1); ++offset)
                    {
                        const auto phase = freePhase ? std::remainder (hits[anchor].time / step - offset, 2.0 * group) : 0.0;
                        const auto straight = scoreFit (hits, step, phase, 0, target);
                        if (straight.score > fit.straight.score)
                            fit.straight = straight;
                        if (target < 2 && straight.evidence && straight.score > fit.supportedStraight.score)
                            fit.supportedStraight = straight;

                        for (int swing = 10; swing <= 100; swing += 10)
                        {
                            const auto candidate = scoreFit (hits, step, phase, swing, target);
                            if (candidate.evidence && candidate.score > bestForTarget.score)
                                bestForTarget = candidate;
                        }
                    }
                }

                if (! bestForTarget.evidence)
                    continue;

                const auto coarseSwing = bestForTarget.swing;
                const auto phase = bestForTarget.phase;
                for (int swing = std::max (1, coarseSwing - 9); swing <= std::min (100, coarseSwing + 9); ++swing)
                {
                    const auto candidate = scoreFit (hits, step, phase, swing, target);
                    if (candidate.evidence && candidate.score > bestForTarget.score)
                        bestForTarget = candidate;
                }
                if (bestForTarget.score > fit.swung.score + 0.008)
                    fit.swung = bestForTarget;
            }

            if (freePhase && fit.swung.target == 2)
            {
                // With 4x swing, attacks from the two groups can coincide. A fit
                // one full step away can then explain the same audible onsets.
                const auto alias = fit.swung.swing > 50 ? fit.swung.swing - 50 : fit.swung.swing + 50;
                const auto alternative = scoreFit (hits, step, fit.swung.phase, alias, fit.swung.target);
                fit.ambiguousSwing = alternative.evidence && alternative.score > fit.swung.score - 0.025;
            }

            return fit;
        }

        std::optional<ImportedSwing> swingForRate (std::optional<ImportedSwing> swing, double analysisStep, double projectStep)
        {
            if (swing && swing->amount > 0)
            {
                const auto groupBeats = analysisStep * swingSubdivisionValueForIndex (swing->target);
                swing->target = clampSwingSubdivisionIndex (juce::roundToInt (std::log2 (groupBeats / projectStep)));
            }
            return swing;
        }

        SwingImportResult analyseMidi (const juce::File& file, double beatsPerStep)
        {
            if (file.getSize() > maxMidiBytes)
                return { {}, {}, "The MIDI file exceeds 16 MB." };

            auto stream = file.createInputStream();
            juce::MidiFile midi;
            int format = 0;
            if (stream == nullptr || ! midi.readFrom (*stream, false, &format))
                return { {}, {}, "Couldn't read this MIDI file." };
            if (midi.getTimeFormat() <= 0 || format == 2)
                return { {}, {}, "Swing analysis needs a beat-based MIDI file with a shared timeline." };

            std::vector<Hit> hits;
            for (int track = 0; track < midi.getNumTracks(); ++track)
                for (const auto* event : *midi.getTrack (track))
                    if (event->message.isNoteOn())
                        hits.push_back ({ event->message.getTimeStamp() / midi.getTimeFormat() });

            std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.time < b.time; });
            auto end = std::unique (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return b.time - a.time < 0.004; });
            hits.erase (end, hits.end());
            hits.resize (std::min (maxFitHits, hits.size()));
            if (hits.size() < 4)
                return { {}, {}, "This MIDI file has too few distinct note starts to measure swing." };

            const auto project = fitGrid (hits, beatsPerStep, false);
            const auto sixteenth = Constants::beatsForRate ("16n");
            if (beatsPerStep > sixteenth)
            {
                const auto finer = fitGrid (hits, sixteenth, false);
                const auto groove = finer.result();
                if (groove && finer.rhythm().score > project.rhythm().score + 0.025)
                    return { swingForRate (groove, sixteenth, beatsPerStep), {}, {} };
            }
            return { project.result(), {}, {} };
        }

        constexpr auto frameSeconds = 0.002;

        std::vector<Hit> waveOnsets (juce::AudioFormatReader& reader, int64_t samples)
        {
            constexpr int bands = 3;
            const int frameSamples = juce::jmax (1, juce::roundToInt (frameSeconds * reader.sampleRate));
            const auto analysisChannels = static_cast<int> (std::min (reader.numChannels, 2u));
            const auto lowCoefficient = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 160.0 / reader.sampleRate);
            const auto midCoefficient = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 1800.0 / reader.sampleRate);
            std::vector<std::array<double, bands>> levels;
            levels.reserve (static_cast<size_t> (samples / frameSamples + 1));
            std::array<double, 2> low {}, mid {};
            juce::AudioBuffer<float> block (analysisChannels, frameSamples * 32);
            for (int64_t start = 0; start < samples; start += block.getNumSamples())
            {
                const auto count = static_cast<int> (std::min<int64_t> (samples - start, block.getNumSamples()));
                if (! reader.read (&block, 0, count, start, true, analysisChannels > 1))
                    return {};

                for (int first = 0; first < count; first += frameSamples)
                {
                    std::array<double, bands> energy {};
                    const auto end = std::min (first + frameSamples, count);
                    for (int channel = 0; channel < analysisChannels; ++channel)
                    {
                        const auto* data = block.getReadPointer (channel);
                        auto& bass = low[static_cast<size_t> (channel)];
                        auto& body = mid[static_cast<size_t> (channel)];
                        for (int i = first; i < end; ++i)
                        {
                            const auto sample = std::isfinite (data[i]) ? static_cast<double> (data[i]) : 0.0;
                            bass += lowCoefficient * (sample - bass);
                            body += midCoefficient * (sample - body);
                            energy[0] += bass * bass;
                            energy[1] += (body - bass) * (body - bass);
                            energy[2] += (sample - body) * (sample - body);
                        }
                    }
                    for (auto& band : energy)
                        band /= (end - first) * analysisChannels;
                    levels.push_back (energy);
                }
            }

            std::array<double, bands> maxima {};
            for (const auto& level : levels)
                for (size_t band = 0; band < bands; ++band)
                    maxima[band] = std::max (maxima[band], level[band]);

            // Positive multiband spectral flux through a compressed envelope.
            // The per-band floor suppresses noise and keeps kick and hat attacks comparable.
            std::vector<double> flux (levels.size());
            for (size_t i = 0; i < levels.size(); ++i)
            {
                for (size_t band = 0; band < bands; ++band)
                {
                    const auto floor = std::max (1.0e-10, maxima[band] * 0.0002);
                    const auto previous = i >= 4 ? levels[i - 4][band] : 0.0;
                    flux[i] += std::max (0.0, std::log ((floor + levels[i][band]) / (floor + previous)));
                }
            }

            if (flux.empty())
                return {};

            std::vector<Hit> hits;
            const auto peak = *std::max_element (flux.begin(), flux.end());
            const auto secondsPerFrame = frameSamples / reader.sampleRate;
            double localMean = 0.0;
            for (size_t i = 0; i + 1 < flux.size(); ++i)
            {
                localMean += 0.02 * (flux[i] - localMean);
                if (flux[i] < std::max ({ 0.75, peak * 0.075, 2.8 * localMean })
                    || (i > 0 && flux[i] < flux[i - 1]) || flux[i] <= flux[i + 1])
                    continue;

                auto leading = i;
                while (leading > 0 && i - leading < 6 && flux[leading - 1] >= flux[i] * 0.25)
                    --leading;
                const Hit hit { static_cast<double> (leading) * secondsPerFrame, flux[i] };
                if (hits.empty() || hit.time - hits.back().time >= 0.012)
                    hits.push_back (hit);
                else if (hit.weight > hits.back().weight * 1.5)
                    hits.back() = hit;
            }

            for (auto& hit : hits)
                hit.weight = 0.75 + 0.25 * std::sqrt (hit.weight / peak);
            return hits;
        }

        std::vector<double> candidateBeats (const std::vector<Hit>& hits, double duration, double referenceBpm)
        {
            constexpr double binSeconds = 0.002;
            std::array<double, 1201> histogram {};
            for (size_t i = 0; i < hits.size(); ++i)
            {
                for (size_t j = i + 1; j < hits.size(); ++j)
                {
                    const auto lag = hits[j].time - hits[i].time;
                    if (lag >= 2.39)
                        break;
                    if (lag < 0.12)
                        continue;
                    const auto bin = static_cast<size_t> (std::round (lag / binSeconds));
                    for (int offset = -2; offset <= 2; ++offset)
                        histogram[static_cast<size_t> (static_cast<int> (bin) + offset)] += (3 - std::abs (offset)) * hits[i].weight * hits[j].weight;
                }
            }

            std::vector<double> periods;
            const auto add = [&periods] (double beat) {
                if (beat >= 60.0 / 240.0 && beat <= 60.0 / 45.0
                    && std::none_of (periods.begin(), periods.end(), [beat] (double existing) { return std::abs (existing - beat) < beat * 0.004; }))
                    periods.push_back (beat);
            };

            for (int i = 0; i < 9; ++i)
            {
                const auto peak = std::max_element (histogram.begin(), histogram.end());
                if (*peak < 3.0)
                    break;
                const auto index = peak - histogram.begin();
                const auto lag = static_cast<double> (index) * binSeconds;
                for (const auto beats : { 0.25, 0.5, 1.0, 2.0, 3.0, 4.0 })
                    add (lag / beats);
                std::fill (histogram.begin() + std::max (std::ptrdiff_t { 0 }, index - 6),
                           histogram.begin() + std::min (std::ssize (histogram), index + 7), 0.0);
            }

            // Exact loop length and the current tempo are candidates, subject to the same onset evidence.
            for (const auto beats : { 4.0, 8.0, 12.0, 16.0, 32.0 })
                add (duration / beats);
            add (60.0 / referenceBpm);
            return periods;
        }

        double refinedBeat (const std::vector<Hit>& hits, Fit fit, double beat, double beatsPerStep)
        {
            const auto step = beat * beatsPerStep;
            const auto group = static_cast<int> (swingSubdivisionValueForIndex (fit.target));
            const auto delay = swingDelayStepsForPosition (group, fit.swing, fit.target);
            double x = 0.0, y = 0.0, xx = 0.0, xy = 0.0, n = 0.0;
            for (const auto& hit : hits)
            {
                const auto position = hit.time / step - fit.phase;
                const auto straight = nearestStep (position, 0.0, group);
                const auto swung = nearestStep (position, group + delay, group);
                const auto grid = std::abs (position - straight) <= std::abs (position - swung) ? straight : swung;
                if (std::abs (position - grid) >= tolerance)
                    continue;

                x += grid;
                y += hit.time;
                xx += grid * grid;
                xy += grid * hit.time;
                n += 1.0;
            }

            const auto denominator = n * xx - x * x;
            if (denominator <= 0.0)
                return beat;
            const auto adjusted = (n * xy - x * y) / denominator / beatsPerStep;
            return std::clamp (adjusted, beat * 0.98, beat * 1.02);
        }

        struct AudioGroove
        {
            SwingImportResult result;
            GridFit fit;
            std::optional<double> exactTempo;
        };

        bool usableReader (const std::unique_ptr<juce::AudioFormatReader>& reader)
        {
            return reader != nullptr && std::isfinite (reader->sampleRate) && reader->sampleRate >= 4000.0
                && reader->sampleRate <= 384000.0 && reader->numChannels > 0 && reader->lengthInSamples > 0;
        }

        int64_t analysisSamples (const juce::AudioFormatReader& reader, double seconds)
        {
            return std::min (reader.lengthInSamples, static_cast<int64_t> (reader.sampleRate * seconds));
        }

        AudioGroove measureAudioGroove (const juce::AudioFormatReader& reader, int64_t samples,
                                        std::vector<Hit> hits, double beatsPerStep, double referenceBpm)
        {
            const auto duration = static_cast<double> (samples) / reader.sampleRate;
            // Keep the tempo search sensitive to sixteenth-note grooves even
            // when the project currently uses a coarser grid.
            const auto analysisStep = std::min (beatsPerStep, Constants::beatsForRate ("16n"));
            hits.resize (std::min (maxFitHits, hits.size()));

            const auto taggedTempo = reader.metadataValues[juce::WavAudioFormat::acidTempo].getDoubleValue();
            if (taggedTempo >= 20.0 && taggedTempo <= 300.0
                && reader.metadataValues[juce::WavAudioFormat::acidOneShot].getIntValue() == 0)
            {
                const auto fit = hits.size() >= 4 ? fitGrid (hits, 60.0 * analysisStep / taggedTempo, true) : GridFit {};
                return { { swingForRate (fit.result(), analysisStep, beatsPerStep), std::round (taggedTempo * 10.0) / 10.0, {} },
                         fit, taggedTempo };
            }
            if (hits.size() < 4)
                return { { {}, {}, "This audio has too few distinct attacks to measure tempo or swing." }, {}, {} };

            const auto end = std::upper_bound (hits.begin(), hits.end(), hits.front().time + 10.0,
                                               [] (double time, const Hit& hit) { return time < hit.time; });
            const std::vector<Hit> excerpt (hits.begin(), end);
            if (excerpt.size() < 4)
                return {};

            double bestBeat = 0.0, bestScore = 0.0;
            GridFit bestFit;
            for (const auto beat : candidateBeats (hits, duration, referenceBpm))
            {
                const auto coarse = fitGrid (excerpt, beat * analysisStep, true);
                if (coarse.rhythm().score < 0.36)
                    continue;

                const auto refined = refinedBeat (excerpt, coarse.rhythm(), beat, analysisStep);
                const auto fit = fitGrid (hits, refined * analysisStep, true);
                const auto rhythm = fit.rhythm();
                const auto span = (hits.back().time - hits.front().time) / refined;
                if (rhythm.score < (hits.size() < 7 ? 0.75 : 0.56) || span < 2.5)
                    continue;

                // Favor whole beats backed by attacks. This helps separate plausible half/double tempos.
                double beatCoverage = 0.0;
                for (int offset = 0; offset * analysisStep < 1.0; ++offset)
                {
                    double coverage = 0.0;
                    for (const auto& hit : hits)
                    {
                        const auto beats = hit.time / refined - (rhythm.phase + offset) * analysisStep;
                        if (std::abs (beats - std::round (beats)) < 0.07)
                            coverage += hit.weight;
                    }
                    beatCoverage = std::max (beatCoverage, coverage);
                }
                const auto bpmDistance = std::abs (std::log2 ((60.0 / refined) / referenceBpm));
                const auto loopBeats = duration / refined;
                const auto wholeBar = 4.0 * std::round (loopBeats / 4.0);
                const auto loopBonus = samples == reader.lengthInSamples && wholeBar >= 4.0
                                            && hits.front().time < refined * 0.1 && duration - hits.back().time < refined * 1.15
                                            && std::abs (loopBeats - wholeBar) < 0.06 ? 0.035 : 0.0;
                const auto score = rhythm.score + 0.16 * std::min (1.0, beatCoverage / span) + loopBonus - 0.095 * bpmDistance;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestBeat = refined;
                    bestFit = fit;
                }
            }

            if (bestBeat <= 0.0)
                return {};

            auto swing = swingForRate (bestFit.result(), analysisStep, beatsPerStep);
            if (beatsPerStep - analysisStep > 0.001)
            {
                const auto projectFit = fitGrid (hits, bestBeat * beatsPerStep, true);
                const auto projectSwing = projectFit.result();
                if (projectSwing && projectFit.rhythm().score > bestFit.rhythm().score + 0.025)
                    swing = projectSwing;
            }
            return { { swing, std::round (600.0 / bestBeat) / 10.0, {} }, bestFit, 60.0 / bestBeat };
        }

        std::unique_ptr<juce::AudioFormatReader> openAudio (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file.createInputStream()));
        }

        SwingImportResult analyseWave (const juce::File& file, double beatsPerStep, double referenceBpm)
        {
            const auto reader = openAudio (file);
            if (!usableReader (reader))
                return { {}, {}, "Couldn't read this WAV file." };
            const auto samples = analysisSamples (*reader, maxWaveSeconds);
            return measureAudioGroove (*reader, samples, waveOnsets (*reader, samples), beatsPerStep, referenceBpm).result;
        }

        constexpr int drumRows = 3, stepsPerBar = 16, barsPerPattern = Constants::maxSteps / stepsPerBar;
        constexpr int maximumBars = Constants::sourceCount * barsPerPattern;

        struct DrumHit
        {
            double time, strength;
        };

        using DrumTracks = std::array<std::vector<DrumHit>, drumRows>;

        // Measure the change in spectral energy around each transient. Using
        // both sides of the attack suppresses sustained notes and drum tails.
        class AttackSpectrum
        {
        public:
            explicit AttackSpectrum (juce::AudioFormatReader& input)
                : reader (input),
                  channels (static_cast<int> (std::min (input.numChannels, 2u))),
                  stride (std::max (1, static_cast<int> (input.sampleRate / 22050.0))),
                  plan (juce::roundToInt (std::log2 (input.sampleRate * 0.0375 / stride))),
                  audio (channels, plan.getSize() * stride),
                  frame (static_cast<size_t> (2 * plan.getSize())),
                  window (static_cast<size_t> (plan.getSize()))
            {
                for (size_t i = 0; i < window.size(); ++i)
                    window[i] = 0.5f - 0.5f * static_cast<float> (std::cos (
                        juce::MathConstants<double>::twoPi * static_cast<double> (i) / static_cast<double> (window.size())));
            }

            using Bands = std::array<double, 5>;

            Bands rise (double time)
            {
                const auto onset = static_cast<int64_t> (std::llround (time * reader.sampleRate));
                const auto lead = static_cast<int64_t> (std::llround (0.005 * reader.sampleRate));
                const auto before = energy (onset - lead - audio.getNumSamples());
                auto after = energy (onset - lead);
                for (size_t i = 0; i < after.size(); ++i)
                    after[i] = std::max (0.0, after[i] - 0.9 * before[i]);
                return after;
            }

        private:
            Bands energy (int64_t start)
            {
                Bands bands {};
                audio.clear();
                const int offset = static_cast<int> (std::clamp<int64_t> (-start, 0, audio.getNumSamples()));
                const auto count = audio.getNumSamples() - offset;
                if (count <= 0 || !reader.read (&audio, offset, count, std::max (start, int64_t { 0 }), true, channels > 1))
                    return bands;

                const auto binFrequency = reader.sampleRate / (stride * plan.getSize());
                constexpr std::array<double, 4> boundaries { 160.0, 550.0, 1700.0, 4300.0 };
                for (int channel = 0; channel < channels; ++channel)
                {
                    std::ranges::fill (frame, 0.0f);
                    const auto* source = audio.getReadPointer (channel);
                    for (size_t i = 0; i < window.size(); ++i)
                    {
                        float sum = 0.0f;
                        for (int part = 0; part < stride; ++part)
                        {
                            const auto value = source[i * static_cast<size_t> (stride) + static_cast<size_t> (part)];
                            if (std::isfinite (value))
                                sum += value;
                        }
                        frame[i] = sum * window[i] / static_cast<float> (stride);
                    }
                    plan.performFrequencyOnlyForwardTransform (frame.data(), true);
                    size_t band = 0;
                    for (size_t bin = 1; bin < window.size() / 2; ++bin)
                    {
                        const auto frequency = static_cast<double> (bin) * binFrequency;
                        if (frequency < 28.0)
                            continue;
                        while (band < boundaries.size() && frequency >= boundaries[band])
                            ++band;
                        const double amplitude = frame[bin];
                        bands[band] += amplitude * amplitude;
                    }
                }
                return bands;
            }

            juce::AudioFormatReader& reader;
            int channels, stride;
            juce::dsp::FFT plan;
            juce::AudioBuffer<float> audio;
            std::vector<float> frame, window;
        };

        DrumTracks classifyDrumHits (juce::AudioFormatReader& reader, const std::vector<Hit>& hits)
        {
            AttackSpectrum spectrum (reader);
            DrumTracks tracks;
            for (const auto& hit : hits)
            {
                const auto [bass, body, snap, high, air] = spectrum.rise (hit.time);
                const auto snare = body + 0.75 * snap;
                const auto hats = high + air;
                const auto strongest = std::max ({ bass, snare, hats });
                if (strongest <= 1.0e-6)
                    continue;

                const bool kick = bass > 0.45 * snare && bass > 0.15 * strongest;
                const bool snareHit = snare > 0.40 * hats && snare > (kick ? 0.27 : 0.15) * strongest
                                   && (!kick || snap > 0.07 * bass || body > 0.55 * bass);
                // Subtract the snare's usual high-frequency spill before looking for a coincident hat.
                const auto independentHat = std::max (0.0, high - 0.50 * snap - 0.07 * body)
                                          + std::max (0.0, air - 0.16 * snap - 0.02 * body);
                const bool hat = snareHit ? independentHat > 0.06 * strongest && air > 0.025 * strongest
                                          : hats > 0.55 * snare && hats > 0.03 * strongest;
                if (kick)
                    tracks[0].push_back ({ hit.time, bass });
                if (snareHit)
                    tracks[1].push_back ({ hit.time, snare });
                if (hat)
                    tracks[2].push_back ({ hit.time, snareHit ? independentHat : hats });
            }
            return tracks;
        }

        int closestDrumStep (double position, ImportedSwing swing, int maximumSteps)
        {
            const auto centre = static_cast<int> (std::floor (position));
            auto best = std::clamp (centre, -1, maximumSteps);
            double distance = std::numeric_limits<double>::infinity();
            for (int step = std::max (-1, centre - 3); step <= std::min (maximumSteps, centre + 2); ++step)
            {
                const auto candidate = std::abs (position - (step + swingDelayStepsForPosition (step, swing.amount, swing.target)));
                if (candidate < distance)
                {
                    distance = candidate;
                    best = step;
                }
            }
            return best;
        }

        int matchedRoll (std::span<const DrumHit> hits, double stepSeconds, double offset)
        {
            if (std::abs (offset) > 0.23)
                return 1;
            const auto maxRoll = static_cast<int> (std::min (hits.size(), static_cast<size_t> (Constants::maxRoll)));
            for (int roll = maxRoll; roll > 1; --roll)
            {
                bool fits = true;
                for (int i = 1; i < roll; ++i)
                {
                    const auto time = (hits[static_cast<size_t> (i)].time - hits.front().time) / stepSeconds;
                    if (std::abs (time - static_cast<double> (i) / roll) > 0.07)
                    {
                        fits = false;
                        break;
                    }
                }
                if (fits)
                    return roll;
            }
            return 1;
        }

        int printDrumTrack (DocumentState& document, size_t row, const std::vector<DrumHit>& hits,
                            double origin, double stepSeconds, ImportedSwing swing, int endStep)
        {
            if (hits.empty())
                return 0;

            std::vector<double> strengths;
            strengths.reserve (hits.size());
            for (const auto& hit : hits)
                strengths.push_back (hit.strength);
            const auto reference = strengths.begin() + std::ssize (strengths) * 9 / 10;
            std::nth_element (strengths.begin(), reference, strengths.end());
            const auto velocity = [peak = *reference] (const DrumHit& hit)
            {
                return std::clamp (juce::roundToInt (22.0 + 95.0 * std::pow (hit.strength / peak, 0.33)), 1, 127);
            };

            int printed = 0;
            for (size_t i = 0; i < hits.size();)
            {
                const auto position = (hits[i].time - origin) / stepSeconds;
                const int step = closestDrumStep (position, swing, endStep);
                if (step >= endStep)
                    break;
                const auto timing = position - (step + swingDelayStepsForPosition (step, swing.amount, swing.target));
                if (step < 0 || std::abs (timing) > 0.74)
                {
                    ++i;
                    continue;
                }
                const auto roll = matchedRoll (std::span (hits).subspan (i), stepSeconds, timing);
                auto& cell = document.sources[static_cast<size_t> (step / Constants::maxSteps)][row]
                                             [static_cast<size_t> (step % Constants::maxSteps)];
                const auto vel = velocity (hits[i]);
                if (!cell.enabled || vel > cell.velocity || roll > cell.roll)
                {
                    printed += roll - (cell.enabled ? cell.roll : 0);
                    cell = {};
                    cell.enabled = true;
                    cell.velocity = vel;
                    cell.nudge = clampInt (juce::roundToInt (100.0 * timing), -100, 100);
                    cell.roll = roll;
                    for (int part = 1; part < roll; ++part)
                        cell.setSplitVelocity (part, velocity (hits[i + static_cast<size_t> (part)]));
                }
                i += static_cast<size_t> (roll);
            }
            return printed;
        }
    }

    bool isDrumAudioFile (const juce::File& file)
    {
        return file.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3;m4a;aac;caf;ogg");
    }

    AudioDrumImport importDrumAudio (const juce::File& file, const juce::String& originalName, double referenceBpm)
    {
        AudioDrumImport result;
        const auto reader = openAudio (file);
        if (!usableReader (reader))
        {
            result.error = "Couldn't read the dropped drum audio.";
            return result;
        }

        const auto excerptSamples = analysisSamples (*reader, maxWaveSeconds);
        auto hits = waveOnsets (*reader, excerptSamples);
        if (hits.empty())
        {
            result.error = "Couldn't find drum attacks in the audio.";
            return result;
        }

        auto groove = measureAudioGroove (*reader, excerptSamples, hits, 0.25, std::clamp (referenceBpm, 90.0, 145.0));
        result.estimatedTempo = groove.exactTempo.has_value();
        result.bpm = groove.exactTempo.value_or (std::clamp (referenceBpm, 20.0, 300.0));
        const auto stepSeconds = 15.0 / result.bpm;
        if (!result.estimatedTempo && hits.size() >= 4)
        {
            const auto count = std::min (maxFitHits, hits.size());
            groove.fit = fitGrid (std::vector<Hit> (hits.begin(), hits.begin() + static_cast<std::ptrdiff_t> (count)), stepSeconds, true);
            groove.result.swing = groove.fit.result();
        }
        const auto swing = groove.result.swing.value_or (ImportedSwing {});
        const auto fit = swing.amount > 0 ? groove.fit.swung : groove.fit.straight;
        const auto phase = fit.score >= 0.0 ? fit.phase : hits.front().time / stepSeconds;
        const auto swingPeriod = swing.amount > 0 ? 2.0 * (1 << swing.target) : 1.0;
        const auto firstPosition = hits.front().time / stepSeconds;
        const auto origin = (phase + swingPeriod * std::floor ((firstPosition - phase + 0.22) / swingPeriod)) * stepSeconds;

        const auto sourceSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
        const auto bars = std::ceil (((sourceSeconds - origin) / stepSeconds - 0.5) / stepsPerBar);
        result.bars = static_cast<int> (std::clamp (bars, 1.0, static_cast<double> (maximumBars)));
        const auto endTime = origin + stepSeconds * (result.bars * stepsPerBar + 0.25);
        const auto samples = analysisSamples (*reader, endTime);
        if (samples > excerptSamples)
            hits = waveOnsets (*reader, samples);
        while (!hits.empty() && hits.back().time >= endTime)
            hits.pop_back();
        const auto tracks = classifyDrumHits (*reader, hits);

        auto document = std::make_unique<DocumentState>();
        auto& settings = document->settings;
        settings.channelCount = drumRows;
        settings.tempo = result.bpm;
        settings.swing = swing.amount;
        settings.swingSubdivisionIndex = swing.target;
        settings.stepCount = settings.generatedStepCount = settings.refreshSteps = std::min (result.bars, barsPerPattern) * stepsPerBar;
        constexpr std::array labels { "Kick", "Snare", "Hat" };
        constexpr std::array pitches { 36, 38, 42 };
        for (size_t row = 0; row < labels.size(); ++row)
        {
            settings.channels[row].label = labels[row];
            settings.channels[row].note = pitches[row];
            settings.channels[row].loopLength = settings.stepCount;
            result.hits += printDrumTrack (*document, row, tracks[row], origin, stepSeconds, swing, result.bars * stepsPerBar);
        }
        if (result.hits == 0)
        {
            result.error = "Couldn't identify kick, snare, or hi-hat hits.";
            return result;
        }

        for (int slot = 0; slot < Constants::sourceCount; ++slot)
        {
            const auto remaining = result.bars - slot * barsPerPattern;
            const int steps = remaining > 0 ? std::min (remaining, barsPerPattern) * stepsPerBar : stepsPerBar;
            auto& source = settings.sourceSettings[static_cast<size_t> (slot)];
            source.stepCount = steps;
            source.loopRanges.fill ({ 0, steps });
        }

        auto project = KickSnareHatEngine::documentToJson (*document);
        project["projectName"] = originalName.upToLastOccurrenceOf (".", false, false).toStdString();
        auto description = juce::String ("Drum transcription from ") + originalName + ". "
                         + juce::String (result.bars) + (result.bars == 1 ? " bar." : " bars.");
        if (!result.estimatedTempo)
            description += " Used the current tempo because the recording's tempo was unclear.";
        project["projectDescription"] = description.toStdString();
        project["standaloneTransportPlaying"] = false;
        result.projectData = project.dump();
        return result;
    }

    std::optional<SwingFileType> swingFileType (const juce::File& file)
    {
        if (file.hasFileExtension ("mid;midi;smf"))
            return SwingFileType::midi;
        if (file.hasFileExtension ("wav;wave"))
            return SwingFileType::wave;
        return {};
    }

    SwingImportResult analyseSwingFile (const juce::File& file, SwingFileType type, double beatsPerStep, double referenceBpm)
    {
        if (! file.existsAsFile())
            return { {}, {}, "Couldn't open the dropped file." };
        if (type == SwingFileType::midi)
            return analyseMidi (file, beatsPerStep);
        return analyseWave (file, beatsPerStep, std::clamp (referenceBpm, 90.0, 145.0));
    }
}
