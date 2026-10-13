#include "PluginProcessor.h"
#include <chrono>
#include <iostream>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    ksh::KickSnareHatEngine engine;
    engine.setChannelCount (1);
    engine.setStepCount (16);
    engine.setGenerationMode (ksh::GenerationMode::staticSource);
    engine.setTempo (120.0);
    engine.setRate ("16n");
    for (int step = 0; step < 16; ++step)
        engine.setCell (0, 0, step, step == 0, 100, 100, 1, 0, false, 2);
    auto snapshot = engine.makePlaybackSnapshot();
    for (int blockSize : {64, 128, 512, 1024})
    {
        ksh::MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        juce::MidiBuffer midi;
        midi.ensureSize (65536);
        std::cout << "Timing block=" << blockSize << " expected on=0,3000 off=2688,5688 actual:";
        for (int sample = 0; sample < 7500; sample += blockSize)
        {
            midi.clear();
            (void) runner.processBlock (snapshot, static_cast<double> (sample) / 24000.0, 120.0, true, blockSize, midi);
            for (const auto event : midi)
                std::cout << ' ' << (event.getMessage().isNoteOn() ? "on=" : "off=") << sample + event.samplePosition;
        }
        std::cout << '\n';
    }
    RealtimeMailbox<int> mailbox;
    for (int i = 1; i <= 10; ++i)
        mailbox.publish (i);
    std::cout << "Mailbox latest after publishing 1..10 without consumption: " << *mailbox.current() << '\n';
    mailbox.drainRetired();
    std::cout << "Mailbox after retiring and rereading: " << *mailbox.current() << '\n';
    snapshot.channelCount = 8;
    snapshot.stepCount = 32;
    snapshot.tempo = 300;
    snapshot.beatsPerStep = 1.0 / 12.0;
    snapshot.stepIntervalMs = 1000.0 / 60.0;
    for (int ch = 0; ch < 8; ++ch)
    {
        snapshot.channels[static_cast<size_t> (ch)].note = 36 + ch;
        snapshot.sourceSettings[0].loopRanges[static_cast<size_t> (ch)] = {0, 32};
        for (int step = 0; step < 32; ++step)
        {
            auto& cell = snapshot.sources[0][static_cast<size_t> (ch)][static_cast<size_t> (step)];
            cell.enabled = true;
            cell.probability = 100;
            cell.velocity = 100;
            cell.roll = 8;
            cell.cycle = 1;
            cell.cycleMask = 1;
        }
    }
    snapshot.sourceSettings[0].stepCount = 32;
    for (int blockSize : {64, 128, 512, 1024})
    {
        ksh::MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        juce::MidiBuffer midi;
        midi.ensureSize (65536);
        std::vector<double> times;
        times.reserve (30000);
        size_t hits = 0;
        for (int block = 0; block < 31000; ++block)
        {
            midi.clear();
            const auto start = std::chrono::steady_clock::now();
            const auto result = runner.processBlock (snapshot, static_cast<double> (block) * blockSize / 9600.0, 300.0, true, blockSize, midi);
            const auto end = std::chrono::steady_clock::now();
            hits += result.noteHitCount;
            if (block >= 1000)
                times.push_back (std::chrono::duration<double, std::micro> (end - start).count());
        }
        double sum = 0;
        for (double t : times) sum += t;
        std::sort (times.begin(), times.end());
        std::cout << "Dense runner block=" << blockSize << " mean_us=" << sum / static_cast<double> (times.size()) << " p99_us=" << times[29700] << " max_us=" << times.back() << " hits=" << hits << '\n';
    }
}
