# UI and engine sync contract

The C++ engine is the source of truth. Svelte mirrors engine state and sends commands; it does not persist an independent model.

## Engine to UI

| Event | Payload | Thread |
| --- | --- | --- |
| `engine_state` | Strict `v:1` JSON: globals, channels, source cells, mutes | Message |
| `preview` | Generated grid + dimensions | Message |
| `current_step` | 1-based step index, 0 when stopped | Message |
| `note_hit` | `channel`, `generatedStep`, `source`, `sourceStep` | Message |
| `status` | Incremental selector + args, e.g. `steps 16` | Message |

The audio thread never emits WebView events. It enqueues note-hit metadata into a lock-free FIFO, stores transport/tempo reports in atomics, and sets a pending-work flag. A message-thread timer drains that work and calls the bridge.

## UI to Engine

Commands use JSON `{ selector, args }` through `kshSendCommand`. Indexes crossing the JS/C++ boundary are 1-based for M4L message compatibility.

- Globals: `steps`, `channels`, `refresh_steps`, `mode`, `rate`, `swing`, `velocity_humanize`, `timing_humanize`, `device_active`, `phase_offset_beats`, `static_source`
- Channel: `channel_label`, `channel_note`, `channel_lock`, `channel_loop_length`, `channel_playback_mode`, `channel_audition`
- Source grid: `cell`, `cell_enabled`, `cell_velocity`, `cell_probability`, `cell_cycle`, `cell_cycle_offset`, `cell_cycle_inverted`, `cell_roll`
- Source/channel utilities: `source_channel_mute`, `source_channel_reset`
- Sync: `sync_all`, `request_state`, `reset`

Message/host-side commands mutate the engine, then publish a new immutable playback snapshot only if `playbackSnapshotVersion()` changed. Metadata-only edits such as `channel_label` do not reset audio playback.

## State Shape

Use channel naming in UI state:

- `channelCount`
- `channels`
- `selectedChannel`
- `MAX_CHANNELS`

The persisted JSON also uses `channelCount` and `channels`. Legacy UI `laneCount`/`lanes` naming should not come back.

## Compact vs Editor

- Compact view shows the generated preview and flashes generated cells from `note_hit`.
- Editor view shows the source grid and channel controls; source cells flash using `source` + `sourceStep`.
- Cell edits are optimistic in the UI, then sent to the engine as `cell` or focused cell setter commands.

## Persistence

The bridge syncs direct `v:1` JSON only. Legacy M4L wrapper/chunk formats are rejected by persistence parsing.
