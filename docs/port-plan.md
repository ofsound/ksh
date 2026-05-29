# KSH port status and architecture

KSH started as a Max for Live prototype and is now a JUCE-driven MIDI effect plugin. This document records the current port state and the architecture the repo should preserve.

## Port Status

| Area | Status |
| --- | --- |
| Pure C++ engine, constants, types, tests | Complete |
| Generation modes, channels, source cells, persistence | Complete |
| MIDI playback in JUCE `processBlock` | Complete |
| Plugin state save/load | Complete |
| WebView bridge | Complete |
| Compact UI | Complete |
| Full editor UI | Complete |
| LiveAPI clip export | Not ported |

M4L clip export is intentionally skipped. There is no standard JUCE/Logic equivalent to LiveAPI clip writing.

## Current Architecture

| Responsibility | Implementation |
| --- | --- |
| Engine state | `source/engine/KickSnareHatEngine.*` |
| UI/host command dispatch | `source/engine/KshEngineCommands.*` |
| Audio-thread playback | `source/engine/KshMidiPlayback.*` |
| Snapshot handoff | `RealtimeMailbox<PlaybackSnapshot>` in `PluginProcessor` |
| UI bridge | `KshUiBridge` + JUCE WebView native functions |
| Frontend | Svelte/Vite in `ui/` |
| Persistence | Strict `v:1` JSON via `serializeForPersistence()` / `deserializeForPersistence()` |

The engine is mutable only on non-audio threads. The message/host side mutates engine state, then publishes an immutable `PlaybackSnapshot` to the audio thread. The audio thread reads only that snapshot and never locks or calls UI code.

## Threading Contract

- `processBlock` must not allocate, block, lock, build strings, emit WebView events, or mutate `KickSnareHatEngine`.
- Playback uses the latest immutable snapshot published by the message/host side.
- Host tempo and transport position are reported from audio to message thread with atomics.
- Note-hit UI events are queued through a lock-free FIFO and drained on the message thread.
- Message-thread work is timer-polled. Do not call `triggerAsyncUpdate()` from audio or realtime-adjacent callbacks.
- The remaining `engineStateMutex` protects message-thread and host-thread engine access only. It is not an audio-thread lock.

## Playback Model

`MidiPlaybackRunner` evaluates enabled generated cells live from `PlaybackSnapshot` at host step edges. Probability, cycle gates, velocity/timing humanize, rolls, swing, playback direction, note-on/off scheduling, and UI note-hit metadata are resolved in the runner with fixed-size state.

The older precomputed native playback table code remains only as engine-side compatibility/test coverage for native-row behavior. It is not the audio-thread playback source of truth.

## Persistence

The only supported plugin state format is direct JSON:

```json
{
  "v": 1,
  "stepCount": 16,
  "channelCount": 8,
  "channels": [],
  "sourceChannelMutes": [],
  "cells": []
}
```

Legacy M4L `ksh_pattern_data` wrappers and chunked `ksh_json_chunks_v1` atoms are rejected. Keep the format small and explicit: globals, channel rows, mutes, and sparse enabled/edited cells.

## Naming

Use **channel** everywhere in new code and docs:

- `channelCount`
- `channels`
- `selectedChannel`
- `MAX_CHANNELS`

Do not reintroduce lane naming. It came from early UI/prototype terminology and made the bridge/persistence contract harder to reason about.

## Parameters

Host-automatable macro controls live in APVTS:

- `rate`
- `swing`
- `velocity_humanize`
- `timing_humanize`
- `device_active`
- `phase_offset_beats`

Pattern/grid data stays in custom JSON state. Do not model the step grid as thousands of APVTS parameters.

## Verification

```sh
cmake --build Builds
./Builds/Tests
```

Useful focused test filters:

```sh
./Builds/Tests "[engine][generation]"
./Builds/Tests "[engine][native]"
./Builds/Tests "[engine][transport]"
./Builds/Tests "[plugin][persistence]"
./Builds/Tests "[plugin][bridge]"
```
