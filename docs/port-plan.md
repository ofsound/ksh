# KSH port plan (M4L → JUCE)

Feature-complete port of [kick-snare-hat](https://github.com/...) Max for Live device to JUCE + Svelte/Tailwind.

**Oracle:** M4L `ksh_engine.test.js` (~80 tests). Port tests before trusting behavior.

## Phase status

| Phase | Scope | Status |
| --- | --- | --- |
| 0 | Engine folder, constants, types, test harness, docs | **Complete** |
| 1 | Core engine (generation modes, channels, persistence) | **Complete** |
| 2 | Native playback table | **Complete** |
| 3 | Audio thread / transport MIDI | **Complete** |
| 4 | Plugin state save/load | **Complete** |
| 5 | WebView bridge | **Complete** |
| 6 | Compact UI | **Complete** |
| 7 | Editor UI | Pending |
| 8 | Export substitute + polish | Pending |

## Architecture map

| M4L | JUCE |
| --- | --- |
| `ksh_engine.js` | `source/engine/` (pure C++, no JUCE) |
| `coll` + `pipe` + `makenote` | Precomputed hits in `processBlock` |
| `plugsync~` | `AudioPlayHead` |
| `messnamed` | WebView native functions + events |
| `ksh_pattern_data` | `getStateInformation` / `setStateInformation` |
| Compact + editor `jsui` | Svelte app |

## Rules

1. Engine logic before UI.
2. Catch2 tests before the next feature slice.
3. Port semantics, not Max object names.
4. One vertical slice per session; do not skip failing tests.

## Verification after each phase

See the **Your checklist** section at the bottom of each phase section below.

---

## Phase 0 — Foundation

- `source/engine/KshConstants.h`
- `source/engine/KshTypes.h`
- `tests/Engine/FoundationTests.cpp`
- `docs/ui-sync.md`
- This file

**Done when:** `./Builds/Tests "[engine][foundation]"` passes; plugin still builds.

### Your checklist (Phase 0)

```sh
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Builds
./Builds/Tests "[engine][foundation]"
```

Expect: 9 passing tests, no MIDI output yet, UI unchanged (placeholder page).

---

## Phase 1 — Core engine

Generation modes, `generateWindow` / `recomposeWindow`, channel metadata, cell edits, `v:1` JSON serialization. ~25 Catch2 tests ported from M4L.

### Your checklist (Phase 1)

```sh
./Builds/Tests "[engine]"
```

Expect: generation-mode tests pass; still no MIDI from plugin.

---

## Phase 2 — Native playback table

- `buildNativePlaybackRows()`, swing, humanize, cycles, probability, roll
- Playback modes: normal, reverse, boomerang
- `transportPosition()` for refresh-boundary table rebuilds (no MIDI yet)
- See [native-playback.md](native-playback.md)

**Done when:** `./Builds/Tests "[engine][native]"` passes.

### Your checklist (Phase 2)

```sh
cmake --build Builds
./Builds/Tests "[engine][native]"
./Builds/Tests "[engine]"
```

Expect: 64 engine tests total; plugin still silent on transport.

---

## Phase 3 — Transport MIDI

`MidiPlaybackRunner` reads host transport, calls `engine.transportPosition()`, emits MIDI from `nativePlaybackRows` on step edges with swing/humanize delays. Wired in `PluginProcessor::processBlock`.

**Done when:** `./Builds/Tests "[engine][transport]"` passes; KSH Standalone outputs MIDI on play.

### Your checklist (Phase 3)

```sh
cmake --build Builds
./Builds/Tests "[engine][transport]"
./Builds/Tests
```

Standalone smoke test:
1. Open **KSH Standalone**
2. Press play — you should hear/see MIDI activity (default: kick on step 1 of each bar at 16n)
3. Route Standalone MIDI out to a drum synth, or use a MIDI monitor

Logic/AU: load in MIDI FX slot; enable transport.

---

## Phase 4 — Persistence

`getStateInformation` / `setStateInformation` store `v:1` JSON via `serializeForPersistence()`. Restore accepts plain JSON, M4L `ksh_pattern_data` wrappers, and chunked `ksh_json_chunks_v1` atoms. On restore: rebuild native playback table (via `deserializeForPersistence` → `resetPlayback`) and reset `MidiPlaybackRunner`.

**Done when:** `./Builds/Tests "[plugin][persistence]"` passes; save/reload in Logic preserves pattern.

### Your checklist (Phase 4)

```sh
cmake --build Builds
./Builds/Tests "[plugin][persistence]"
./Builds/Tests
```

Logic smoke test:
1. Load **KSH AU**, change pattern (e.g. add snare on step 5)
2. Save project, close, reopen
3. Pattern and MIDI output should match before save

---

## Phase 5 — WebView bridge

`KshUiBridge` on the message thread: native function `kshSendCommand` (JSON `{ selector, args }`), events `engine_state`, `preview`, `current_step`, `note_hit`, `status`. UI commands suspend audio briefly; `sync_all` / `request_state` match M4L. Minimal Svelte page in `ui/src/App.svelte` proves `sync_all`.

**Done when:** `./Builds/Tests "[bridge]"` passes; plugin UI shows engine_state after load.

### Your checklist (Phase 5)

```sh
cd ui && npm run dev   # terminal 1 — hot reload
cmake --build Builds   # terminal 2
./Builds/Tests "[bridge]"
./Builds/Tests
```

Debug plugin UI smoke test:
1. Open **KSH** editor in Logic (Debug build loads `http://localhost:5173`)
2. Bridge panel should show steps/channels and **sync_all OK**
3. Press play — **Current step** should advance

Release / no dev server: `cd ui && npm run build` then rebuild plugin — embedded zip UI works the same.

---

## Phase 6 — Compact UI

Svelte compact strip (`ui/src/components/CompactStrip.svelte`) mirrors `ksh_compact_ui.js`: action column (Edit, Export controls), generated preview grid, lane labels, `note_hit` flashes. Default plugin editor size **736×176**.

Export UI removed — M4L LiveAPI clip export is not ported to JUCE.

**Done when:** compact grid shows kick on step 1, cells flash on play, Edit opens editor.

### Your checklist (Phase 6)

```sh
cd ui && npm run dev
cmake --build Builds
```

1. Open **KSH AU** — compact strip (not debug panel)
2. Default pattern: blue cell on step 1, lane label **1**
3. Press play — cells flash white on hits
4. **Edit** opens full editor

---

## Phase 7 — Editor UI

Full grid editor parity with `ksh_ui.js`: shared session store (`kshSession.svelte.js`), `EditorView.svelte` with source grid, header controls, row controls, layer modes, `note_hit` flashes on source cells, `current_step` on step numbers. Edit opens editor and resizes plugin window via native `kshSetViewSize`.

**Done when:** Edit opens editor at ~1160×232+ (scales with steps/lanes), cells toggle and send `cell` commands, play flashes source cells, Compact returns to strip.

### Your checklist (Phase 7)

```sh
cd ui && npm run dev
cmake --build Builds
```

1. Open **KSH AU** — compact strip loads
2. Click **Edit** — editor opens, window resizes wider/taller
3. Click cells in source grid — toggles on/off; play in Logic updates output
4. Press play — source cells flash white on hits (source step, not generated preview)
5. Step numbers highlight during playback
6. Click **Compact** — returns to 736×176 strip

---

## Phase 8 — Editor polish (export skipped)

M4L clip export via LiveAPI is **not ported** — no standard JUCE/Logic equivalent. Remaining M4L editor interactions are ported in this phase.

**Done when:** drag-paint, layer drags, header drags, lane rename, mute paint, and keyboard layer shortcuts work in Logic; compact strip is Edit-only.
