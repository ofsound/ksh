# UI and engine sync contract (JUCE port)

Ported from M4L Kick Snare Hat. The **C++ engine is the source of truth**. Svelte mirrors state; it does not persist independently.

For native transport playback design, see `port-plan.md` Phase 2–3 (equivalent of M4L `docs/native-timing.md`).

## Engine → UI events

| Event | Payload | When |
| --- | --- | --- |
| `engine_state` | Compact `v:1` JSON (globals, channels, sparse source cells, mutes) | Load, reset, restore, `sync_all`, bulk edits |
| `preview` | Generated grid + dimensions | After generation/table rebuild |
| `current_step` | 1-based step index | Editor open + transport playing |
| `note_hit` | channel, generatedStep, source, sourceStep | Each MIDI hit (UI flash) |
| Status selectors | e.g. `steps 16`, `mode stack` | Incremental setter feedback |

Legacy M4L saves may include `nativeTiming`; ignore on load.

## UI → engine commands

Same vocabulary as M4L Max messages (1-based indexes at the boundary):

- Globals: `steps`, `channels`, `refresh_steps`, `mode`, `rate`, `swing`, `velocity_humanize`, `timing_humanize`, `device_active`, `phase_offset_beats`, `static_source`
- Channel: `channel_label`, `channel_note`, `channel_lock`, `channel_loop_length`, `channel_playback_mode`, `channel_audition`
- Source grid: `cell`, `cell_enabled`, `cell_velocity`, …, `source_channel_mute`, `source_channel_reset`
- Sync: `sync_all`, `request_state`, `reset`
- Transport (internal): driven by processor, not WebView

## Compact vs editor

- **Compact** — globals + generated preview; flashes on `note_hit` (channel + generated step).
- **Editor** — full source grid, lane controls, phase, device on/off; flashes source-layer cell on `note_hit` (channel, source, sourceStep).

Editor cell edits are optimistic: update local state, then send `cell` to engine.

## Naming

Engine/API/persistence use **channel**. UI labels may say **Lane**.

## JUCE bridge (Phase 5)

- UI → C++: `kshSendCommand(JSON.stringify({ selector, args }))` via `@juce` `getNativeFunction`
- C++ → UI: `emitEventIfBrowserIsVisible` — `engine_state`, `preview`, `current_step`, `note_hit`, `status`
- Helpers: `ui/src/lib/kshBridge.js`; dispatch in `source/engine/KshEngineCommands.cpp`
