# Native playback (JUCE port)

Port of M4L `docs/native-timing.md`. The engine precomputes MIDI hits into a **native playback table**; Phase 3 will consume this table from `processBlock`. Phase 2 implements table generation only.

## Responsibilities

| Layer | Role |
| --- | --- |
| `KickSnareHatEngine` | Builds `nativePlaybackRows`, `nativePlaybackStepCount`, handles `transportPosition` for refresh-window generation and table rebuilds |
| `processBlock` (Phase 3) | Step edges → lookup `nativePlaybackRows` → emit MIDI + collect `note_hit` |
| Svelte UI (Phase 5+) | Flash cells on `note_hit` |

## Playback table

`buildNativePlaybackRows()` fills one sparse row per native playback index. Row count is `stepCount × cyclePeriod`, capped at **2048** rows (`nativePlaybackSupported()` false above that).

Each hit is a `NativeHit` (9 fields, see `KshNativePlayback.h`):

| Field | Use |
| --- | --- |
| pitch | MIDI note |
| velocity | MIDI velocity |
| durationMs | Note length |
| midiChannel | Fixed channel 1 |
| delayMs | Swing / timing humanize delay within the step |
| uiChannel | 1-based lane for UI flash |
| uiGeneratedStep | Generated grid step |
| uiSource | 1-based source pattern |
| uiSourceStep | Source grid step |

Swing, timing/velocity humanize, probability, cycles, roll, and playback modes are resolved at table build time (matching M4L).

## Transport (engine side)

`transportPosition(beats, isPlaying)`:

- Updates playhead / refresh-window generation (`prepareStepForPlayback`)
- Rebuilds native table via `syncNativePlaybackTable()`
- Does **not** emit MIDI (patch/`processBlock` owns output in M4L/JUCE respectively)

## Tests

`tests/Engine/NativePlaybackTests.cpp` — table output, playback modes, transport table sync.

Run:

```sh
./Builds/Tests "[engine][native]"
```
