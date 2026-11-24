#include "KshJam.h"
#include "KshMath.h"

#include <limits>
#include <span>

namespace ksh
{
namespace
{
    constexpr int maxInferredSwing = 20;

    double inferTempo (std::span<const JamHit> hits, double duration, double previousTempo, int& measures)
    {
        const double start = hits.front().timestampMs;
        const double tapInterval = duration / static_cast<double> (hits.size());
        const bool steadyTaps = hits.size() >= 3 && tapInterval >= 280.0 && tapInterval <= 1100.0
            && std::all_of (hits.begin(), hits.end(), [&] (const JamHit& hit)
            {
                const double phase = (hit.timestampMs - start) / tapInterval;
                const auto index = static_cast<double> (&hit - hits.data());
                return hit.row == hits.front().row && std::abs (phase - index) < 0.16;
            });
        const double preferredTempo = std::clamp (previousTempo, 90.0, 145.0);
        double bestScore = std::numeric_limits<double>::infinity();
        double bestTempo = 0.0;
        for (int bars = 1; bars <= 4; ++bars)
        {
            const double bpm = 240000.0 * bars / duration;
            if (bpm < 20.0 || bpm > 300.0)
                continue;
            double rhythmicError = 0.0;
            for (const auto& hit : hits)
            {
                const double beat = (hit.timestampMs - start) * bpm / 60000.0;
                const double straight = beat - std::round (beat * 4.0) / 4.0;
                const double triplet = beat - std::round (beat * 3.0) / 3.0;
                rhythmicError += std::min (straight * straight, triplet * triplet + 0.0004);
            }
            const double tempoDistance = std::log2 (bpm / preferredTempo);
            const double tapDistance = std::log2 (bpm * tapInterval / 60000.0);
            // Three bars must fit a little better than a common loop length.
            const double lengthPenalty = bars == 3 ? 0.0015 : 0.0;
            const double score = rhythmicError / static_cast<double> (hits.size()) + 0.006 * tempoDistance * tempoDistance
                                 + (steadyTaps ? 0.08 * tapDistance * tapDistance : 0.0) + lengthPenalty;
            if (score < bestScore)
            {
                bestScore = score;
                bestTempo = bpm;
                measures = bars;
            }
        }
        return bestTempo;
    }

    // Estimate the swing amount for one target from recurring offbeats. Each
    // hit gets one vote at the nearest playable step within the take.
    std::optional<JamTake::Swing> fitSwing (std::span<const JamHit> hits, double origin, double stepMs, int steps, int target)
    {
        const int group = static_cast<int> (swingSubdivisionValueForIndex (target));
        double bestError = std::numeric_limits<double>::infinity();
        std::optional<JamTake::Swing> best;
        for (int amount = 0; amount <= 100; ++amount)
        {
            const auto shift = swingDelayStepsForPosition (group, amount, target);
            double error = 0.0;
            std::array<int, 2> lastCycle { -1, -1 }, cycleCount {};
            for (const auto& hit : hits)
            {
                const double position = (hit.timestampMs - origin) / stepMs;
                const int centre = static_cast<int> (std::lround (position));
                int nearest = 0;
                double distance = std::numeric_limits<double>::infinity();
                for (int step = std::max (0, centre - 2); step <= std::min (steps - 1, centre + 1); ++step)
                {
                    const double onset = step + (step / group % 2 == 0 ? 0.0 : shift);
                    if (const double candidate = std::abs (position - onset); candidate < distance)
                    {
                        nearest = step;
                        distance = candidate;
                    }
                }
                error += distance * distance;
                const auto branch = static_cast<size_t> (nearest / group % 2);
                const int cycle = nearest / (2 * group);
                if (distance < 0.2 && cycle > lastCycle[branch])
                {
                    lastCycle[branch] = cycle;
                    ++cycleCount[branch];
                }
            }
            if (cycleCount[0] >= 2 && cycleCount[1] >= 2 && error < bestError)
            {
                best = JamTake::Swing { amount, target };
                bestError = error;
            }
        }
        return best;
    }

    // Find the lowest-error sequence of cells that fits these attacks without
    // overlapping spans. Positions and errors are measured in sixteenth steps.
    double transcribeRow (std::span<const JamHit> hits, double origin, double stepMs, int steps, JamTake::Swing swing,
                          int timingStrength, std::array<Cell, Constants::maxSteps>& row, int& droppedHits)
    {
        if (hits.empty())
            return 0;
        struct Fit
        {
            double cost = std::numeric_limits<double>::infinity();
            int count = 0, span = 1, nudge = 0;
        };
        const int total = static_cast<int> (hits.size());
        std::vector<double> positions;
        positions.reserve (hits.size());
        for (const auto& hit : hits)
            positions.push_back ((hit.timestampMs - origin) / stepMs);
        const auto width = static_cast<size_t> (total + 1);
        std::vector<Fit> fits (static_cast<size_t> (steps + 1) * width);
        const auto fitAt = [&] (int step, int hit) -> Fit&
        {
            return fits[static_cast<size_t> (step) * width + static_cast<size_t> (hit)];
        };
        for (int i = 0; i <= total; ++i)
            fitAt (steps, i).cost = 16.0 * (total - i);

        for (int step = steps - 1; step >= 0; --step)
        {
            fitAt (step, total).cost = 0.0;
            for (int i = total - 1; i >= 0; --i)
            {
                auto& best = fitAt (step, i);
                best.cost = fitAt (step + 1, i).cost;
                if (const double skip = 16.0 + fitAt (step, i + 1).cost; skip < best.cost)
                    best = { skip, -1 };
                if (std::abs (positions[static_cast<size_t> (i)] - step) > 2.5)
                    continue;

                for (int span : { 1, 2, 4, 8 })
                {
                    if (step + span > steps)
                        break;
                    for (int count = span == 1 ? 1 : 2; count <= Constants::maxRoll && i + count <= total; ++count)
                    {
                        double sum = 0.0, squares = 0.0;
                        for (int part = 0; part < count; ++part)
                        {
                            const double phase = static_cast<double> (part) / count;
                            const double swingDelay = span == 1 ? swingDelayStepsForPosition (step + phase, swing.amount, swing.subdivision) : 0.0;
                            const double error = positions[static_cast<size_t> (i + part)] - (step + span * phase + swingDelay);
                            sum += error;
                            squares += error * error;
                        }
                        const int nudge = clampInt (static_cast<int> (std::lround (timingStrength * sum / count)), -100, 100);
                        const double offset = nudge / 100.0;
                        const double error = squares - 2.0 * offset * sum + count * offset * offset;
                        const double cost = fitAt (step + span, i + count).cost + error
                                            + 0.008 * (count - 1) + 0.006 * (span - 1) + 0.001 * offset * offset;
                        if (cost < best.cost)
                            best = { cost, count, span, nudge };
                    }
                }
            }
        }

        int kept = 0;
        for (int step = 0, i = 0; step < steps && i < total;)
        {
            const auto fit = fitAt (step, i);
            if (fit.count > 0)
            {
                auto& cell = row[static_cast<size_t> (step)];
                cell = { .enabled = true, .velocity = hits[static_cast<size_t> (i)].velocity,
                         .roll = fit.count, .nudge = fit.nudge, .span = fit.span };
                for (int part = 1; part < fit.count; ++part)
                {
                    const int velocity = hits[static_cast<size_t> (i + part)].velocity;
                    if (velocity != cell.velocity)
                        cell.setSplitVelocity (part, velocity);
                }
                kept += fit.count;
            }
            i += std::abs (fit.count);
            if (fit.count >= 0)
                step += fit.span;
        }
        droppedHits += total - kept;
        return fitAt (0, 0).cost;
    }
}

/** Hits use one monotonic clock. The finish press marks the next downbeat. */
std::optional<JamTake> transcribeJam (std::vector<JamHit> hits, double endMs, double previousTempo,
                                     int swing, int swingSubdivision, int quantizeStrength)
{
    std::erase_if (hits, [endMs] (const JamHit& hit)
    {
        return !std::isfinite (hit.timestampMs) || hit.timestampMs >= endMs
               || hit.row < 0 || hit.row >= Constants::maxChannels;
    });
    if (hits.empty() || hits.size() > JamTake::maxHits || !std::isfinite (endMs))
        return std::nullopt;
    std::stable_sort (hits.begin(), hits.end(), [] (const JamHit& a, const JamHit& b) { return a.timestampMs < b.timestampMs; });
    const double origin = hits.front().timestampMs;
    const double duration = endMs - origin;
    if (duration < 800.0 || duration > JamTake::maxDurationMs)
        return std::nullopt;
    JamTake take;
    int measures = 1;
    take.bpm = inferTempo (hits, duration, previousTempo, measures);
    if (take.bpm <= 0.0)
        return std::nullopt;
    take.steps = measures * 16;
    const double stepMs = 15000.0 / take.bpm;
    std::array<std::vector<JamHit>, Constants::maxChannels> rows;
    for (auto hit : hits)
    {
        hit.velocity = clampInt (hit.velocity, 1, 127);
        rows[static_cast<size_t> (hit.row)].push_back (hit);
    }
    const int timingStrength = 100 - clampInt (quantizeStrength, 0, 100);
    const auto fitPattern = [&] (JamTake::Swing feel, SourcePattern& pattern, int& dropped)
    {
        double error = 0.0;
        for (size_t row = 0; row < rows.size(); ++row)
            error += transcribeRow (rows[row], origin, stepMs, take.steps, feel, timingStrength, pattern[row], dropped);
        return error;
    };
    const JamTake::Swing current { swing, swingSubdivision };
    double bestError = fitPattern (current, take.pattern, take.droppedHits);
    if (timingStrength == 0)
    {
        // Avoid changing project swing to explain a few milliseconds of jitter.
        const double improvementFloor = bestError - 0.0064 * static_cast<double> (hits.size());
        const auto trySwing = [&] (JamTake::Swing feel)
        {
            const auto previous = take.swing.value_or (current);
            if (feel.amount == previous.amount && (feel.amount == 0 || feel.subdivision == previous.subdivision))
                return;
            SourcePattern pattern {};
            int dropped = 0;
            const double error = fitPattern (feel, pattern, dropped);
            if (error < std::min (bestError, improvementFloor))
            {
                take.pattern = std::move (pattern);
                take.droppedHits = dropped;
                take.swing = feel;
                bestError = error;
            }
        };
        for (int target = 0; target < Constants::swingSubdivisionCount; ++target)
            if (auto candidate = fitSwing (hits, origin, stepMs, take.steps, target))
            {
                // Detect recurring swing across the full range, then refit the notes to a tighter grid.
                candidate->amount = std::min (candidate->amount, maxInferredSwing);
                if (candidate->amount == 0)
                    candidate->subdivision = swingSubdivision;
                trySwing (*candidate);
            }
    }
    return take;
}
}
