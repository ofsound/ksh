#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ksh
{
    /** Keeps sample offsets on the audio device's host clock, independent of
        when processing finishes. One buffer (at least 2 ms) of scheduling lead
        lets CoreMIDI deliver early-buffer events without a sleeping worker.
        Only the audio thread accesses this state while the device runs. */
    class StandaloneMidiClock
    {
    public:
        void prepare (double rate, int maximumBlockSize)
        {
            nsPerSample = 1.0e9 / (rate > 0.0 ? rate : 44100.0);
            leadNs = juce::jmax<juce::uint64> (2000000, samplesToNs (juce::jmax (1, maximumBlockSize)));
            nextBlockNs = 0;
        }

        juce::uint64 beginBlock (juce::Optional<juce::uint64> hostTimeNs, juce::uint64 nowNs, int numSamples)
        {
            const bool hasHostTime = hostTimeNs.hasValue() && *hostTimeNs != 0;
            auto start = hasHostTime ? *hostTimeNs : nextBlockNs;
            // Without a device timestamp, advance by samples instead of adding
            // callback wakeup jitter to every block. Reanchor after interruption.
            if (start == 0 || (!hasHostTime &&
                              (start > nowNs + leadNs || nowNs > start + 4 * leadNs)))
                start = nowNs;
            nextBlockNs = start + samplesToNs (numSamples);
            return start + leadNs;
        }

        juce::uint64 eventTime (juce::uint64 blockTimeNs, int sampleOffset) const
        {
            return blockTimeNs + samplesToNs (juce::jmax (0, sampleOffset));
        }

        static juce::uint64 nanosecondsToHostTicks (juce::uint64 ns, uint32_t numerator, uint32_t denominator)
        {
            // Split before multiplying: device uptime in nanoseconds can be
            // large, and converting the whole value through double loses bits.
            return (ns / numerator) * denominator + ((ns % numerator) * denominator) / numerator;
        }

    private:
        juce::uint64 samplesToNs (int count) const
        {
            return static_cast<juce::uint64> (std::llround (juce::jmax (0, count) * nsPerSample));
        }
        double nsPerSample = 1.0e9 / 44100.0;
        juce::uint64 leadNs = 2000000;
        juce::uint64 nextBlockNs = 0;
    };

    // Keep note-offs that are still queued as well as notes without a scheduled release.
    // Flushing a port can cancel a queued release for a note that already reached the synth.
    class StandaloneMidiHeldNotes
    {
    public:
        void observe (uint32_t word, juce::uint64 time)
        {
            const auto status = (word >> 20) & 0xf;
            if (status != 8 && status != 9)
                return;
            const auto channel = (word >> 16) & 0xf;
            const auto note = (word >> 8) & 0x7f;
            auto& release = releases[channel][note];
            if (status == 9 && (word & 0x7f) != 0)
                release = std::numeric_limits<juce::uint64>::max();
            else if (release != 0)
                release = time;
        }

        template <typename Emit>
        void forEachHeld (juce::uint64 now, Emit&& emit) const
        {
            for (size_t channel = 0; channel < releases.size(); ++channel)
                for (size_t note = 0; note < releases[channel].size(); ++note)
                    if (releases[channel][note] > now)
                        emit (0x20800000u | (static_cast<uint32_t> (channel) << 16) | (static_cast<uint32_t> (note) << 8));
        }

        void clear() { releases = {}; }

    private:
        std::array<std::array<juce::uint64, 128>, 16> releases {};
    };

    // Raw-byte conversion avoids MidiMessage's allocating SysEx copy. Pad short
    // messages because JUCE's converter reads three bytes before masking them.
    template <typename Emit>
    void forEachStandaloneMidiPacket (const juce::MidiBuffer& midi, uint8_t group, Emit&& emit)
    {
        for (const auto metadata : midi)
        {
            const std::byte* bytes = reinterpret_cast<const std::byte*> (metadata.data);
            std::array<std::byte, 3> padded {};
            if (metadata.numBytes <= 3)
            {
                std::copy_n (bytes, metadata.numBytes, padded.begin());
                bytes = padded.data();
            }
            juce::ump::Conversion::toMidi1 ({ group, { bytes, static_cast<size_t> (metadata.numBytes) } },
                                           [&] (const juce::ump::View& packet) { emit (metadata.samplePosition, packet); });
        }
    }
}
