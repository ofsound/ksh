# MIDI playback

KSH playback is snapshot based. The message/host side publishes immutable `PlaybackSnapshot` objects, and `processBlock` evaluates MIDI from the current snapshot without touching mutable engine state.

## Responsibilities

| Layer | Role |
| --- | --- |
| `KickSnareHatEngine` | Owns pattern/generation state and builds `PlaybackSnapshot` |
| `PluginProcessor` | Publishes snapshots, reports host tempo/transport to the message thread, and calls `MidiPlaybackRunner` from `processBlock` |
| `MidiPlaybackRunner` | Realtime-safe MIDI evaluation and note scheduling |
| `KshUiBridge` / native editor | Receives note hits drained on the message thread and flashes grid cells |

## Realtime Rules

`MidiPlaybackRunner::processBlock` must stay allocation-free and lock-free. It uses fixed-size storage for pending note-offs, cycle counters, and note-hit metadata. Audition notes are passed through a bounded lock-free queue.

The audio thread may:

- Read the current `PlaybackSnapshot`
- Read host playhead data
- Write MIDI into the host-provided `MidiBuffer`
- Enqueue fixed-size note-hit records for the UI
- Store atomics that ask the message thread to update engine generation/tempo

The audio thread must not:

- Lock `engineStateMutex`
- Mutate `KickSnareHatEngine`
- Build JSON or strings
- Call JUCE UI or `AsyncUpdater` APIs
- Allocate containers or grow buffers

## Step Evaluation

For each emitted global step, playback resolves:

- channel playback mode: normal, reverse, ping-pong
- generated cell lookup
- cycle gate and cycle offset/inversion
- probability
- velocity humanize
- swing and timing humanize
- rolls
- note-on and note-off scheduling
- note-hit metadata for UI flashes

The runner uses a small realtime-safe RNG for probability and humanize decisions. There is no precomputed playback table; every step is evaluated live from the snapshot.

## Tests

```sh
./Builds/Tests "[engine][transport]"
./Builds/Tests "[plugin][realtime]"
./Builds/Tests
```
