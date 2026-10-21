# KSH native frontend replacement

Prepared 2026-10-12 against commit `7907610` and the current working tree. Implemented 2026-10-13. See [implementation and validation report](native-ui-implementation-report.md) for delivered behavior, evidence, and the checks that remain unverified. The original plan below records the intended acceptance criteria; it is not a claim that every manual host check passed.

## Delivery contract

Deliver one integrated, working native JUCE frontend that completely replaces the Svelte/WebView frontend. Keep KSH's recognizable composition and all currently exposed musical editing workflows. Use one hardcoded palette derived from the existing Alternative theme. The first delivery must be usable for composing, performing, saving, restoring, and editing patterns; small visual refinements can follow in native code.

“One shot” means one complete migration delivery, with intermediate compilation and testing throughout implementation. It does not mean writing the entire replacement before testing its foundations.

Decisions:

- Native `juce::Component` controls and `juce::Graphics` rendering, using the repository's existing JUCE version and Pamplejuce build. No new UI framework, CSS interpreter, browser fallback, or custom GPU renderer.
- A fixed `KshPalette` with semantic C++ names, plus shared layout/font/drawing helpers. No runtime theme selection or CSS dependency.
- Preserve the existing engine, MIDI processing, APVTS parameter IDs, plugin IDs, formats, and strict `v:1` project format. Adapt the processor's UI-facing boundary rather than replacing its musical algorithms.
- Keep the source editor, generated preview, random controls, inspector, and footer together in the same window.
- Preserve drum/channel color coding and the DC control as an editing display option within this single palette. It is independent of the removed light/dark/alt theme selector.
- First priority is complete workflow coverage, correct state synchronization, and a coherent native layout. Fine shadow softness, font rasterization, and small icon/spacing adjustments can be refined afterward. Missing controls, project incompatibility, and broken interactions cannot.

## What the repository establishes

| Existing implementation | Migration implication |
| --- | --- |
| `ui/src/components/EditorView.svelte` contains roughly 2,540 lines, including selection and editing gestures | The task includes porting interaction logic, not only drawing a grid. |
| `ui/src/lib/kshSession.svelte.js` coordinates commands, local drafts, history, project operations, and playback feedback | Introduce a native editor controller with explicit ownership and transaction rules. |
| `PluginProcessor` already exposes snapshots, engine commands, APVTS, and a direct row-trigger API | Reuse existing behavior through an in-process boundary. |
| Engine callbacks can execute while `engineStateMutex` is held | Native callbacks must not synchronously re-enter the processor to fetch state or mutate components. |
| `EngineStateSnapshot` includes native playback vectors and debug counters | Do not copy that entire structure on every UI frame; add a lean editor read model. |
| `PluginEditor` already implements project file validation, asynchronous choosers, and temporary-file saves | Reuse those mechanics, removing WebBrowser completion types and preprocessor guards. |
| CMake currently enables browser support and invokes npm to embed `ui.zip` | The migration must change build and asset packaging as well as the editor. |
| Native defaults are 1328×828/872, while web layout uses a 1338 minimum width and content-derived heights | Establish one native geometry authority from actual content and captured reference bounds. Do not preserve conflicting resize rules. |
| The working tree already contains changes in CMake, editor, bridge, README, and CLAUDE instructions | Preserve these changes, including iOS-related support, in the migration baseline. |

## Fixed Alternative palette and assets

Extract the actual Alternative values from `ui/src/semantic-theme.generated.css`, including the colors used by states and channel coding. Initial key values:

| Role | Color |
| --- | --- |
| App background | `#0B0D0E` |
| Shell | `#121719` |
| Surface | `#192124` |
| Raised surface / subtle border | `#223036` |
| Main border | `#38535C` |
| Text | `#F4FAFB` |
| Muted text | `#95B4BB` |
| Orange accent / focus | `#F1954A` |
| Strong accent | `#F1954A` |
| Accent surface | `#2E2410` |
| Info / generated preview | `#67E8F9` |

Resolve the used `color-mix` values into native colors/blends once, preserving alpha and sRGB mixing intent. Centralize those constants rather than scattering hex literals through components. Implement state-dependent fills, gradients, borders, and modest cached shadows in shared drawing helpers.

Bundle the Gantari font locally with its redistribution license. Verify the exact weights used by the current UI; embed static weight files if variable-font weight selection is inconsistent with the pinned JUCE build. Preserve numeric alignment, tracking, and baseline placement. Extract existing SVG paths and recreate CSS-only branding/drum marks as native vector paths. Use vector shapes for playback-direction and note-value icons to avoid platform-dependent symbol fallback.

Remove the theme button. Accept old `projectThemeMode` metadata without affecting appearance, and write `alt` on new saves for compatibility with older KSH builds. This requires no project schema change.

> **Superseded:** the processor no longer stores a theme mode. `projectThemeMode`, `patternViewScale`, and `standaloneTempo` are ignored on load and not written. A WebView-era build opening a new save falls back to its default theme.

## Native architecture

```text
PluginEditor
  NativeEditorView (layout and component ownership)
    ProjectHeader / MacroControls / PatternBank
    PatternGrid + ChannelControls + GridToolbar
    GeneratedPreview / GenerationControls / CellInspector
    Footer / UI scale / anchored CycleEditor
  EditorController (selection, gestures, undo, UI drafts)
  ProjectFileController (native asynchronous file operations)
       |
       v
PluginProcessor native UI boundary
  validated edits -> existing engine -> PlaybackSnapshot -> audio thread
  revisioned editor state + message-thread hit notifications -> editor
```

Proposed organization under `source/ui/`:

- `KshPalette.h`, `KshLayout.h/.cpp`, `KshLookAndFeel.h/.cpp`: fixed design values, geometry, fonts, shared control styling.
- `KshEditorState.h`, `KshEditorController.h/.cpp`: lean read model, temporary UI state, commands, and history.
- `KshGridInteraction.h/.cpp`: testable hit testing, selection, drag resolution, cell transformations, and cycle operations.
- `KshNativeEditor.h/.cpp`, `KshPatternGrid.h/.cpp`, `KshHeader.h/.cpp`, `KshPreviewPanel.h/.cpp`, `KshCellInspector.h/.cpp`, `KshCycleEditor.h/.cpp`: visual sections.
- `KshValueControl.h/.cpp`: reusable number drag/type/reset control with per-control limits and gesture behavior.
- `KshProjectFiles.h/.cpp`: file operations and project metadata integration.

These are responsibilities, not a requirement to create a file for every small helper. Avoid reproducing the monolithic Svelte editor as a single C++ file.

### State, notification, and command rules

1. The processor remains authoritative for musical state. The editor owns selection, hovered cells, open popovers, interaction drafts, and undo transactions.
2. Expose a lean, revisioned `UiReadModel`: cells, channels, source settings/mutes, generated cells, macros, project metadata, and standalone/record state. Build it on the message thread under the existing engine lock, then release that lock before notifying the editor. Snapshot access is outside `paint()`.
3. Replace browser event delivery with native state invalidation and typed note-hit notifications. Keep one consumer of the existing hit FIFO in the processor and forward results to the attached editor. Opening, closing, and hiding an editor must not affect MIDI processing or leave callbacks pointing at deleted components.
4. Route native control edits through typed controller methods. For the first delivery, those methods can adapt to `dispatchUiEngineCommand(selector, args)` using in-memory arguments, preserving its validation and parameter synchronization. There is no JSON-string encoding/decoding, JavaScript promise chain, or `juce::var` browser round trip. JSON remains valid for persistence and existing internal engine interfaces.
5. Add a bounded bulk-cell edit method for multi-cell operations: validate first, hold the engine mutex once, apply the edit, and publish playback state once per batch. Coalesce continuous gesture writes to a UI tick, flushing the final value immediately on release. Preserve direct `triggerRowFromUi()` calls for audition and recording pads.
6. Mark changes from all producers: edits, MIDI recording, host state restore, automation, generation, project loading, and reset. An unchanged revision must not cause a full-model copy or full-window repaint.
7. Keep APVTS the host-automation authority. Wrap existing macro command writes in matching parameter `beginChangeGesture`/`endChangeGesture` calls, with the dispatcher providing the value update. Do not add a second parameter-writing path that duplicates notifications. Host parameter callbacks request message-thread refresh; they never touch components directly. Include rate and pattern-selection synchronization in the tests.
8. Preserve the audio-thread mailbox/FIFO/atomic model. No component calls, locks, allocations, persistence work, or UI-triggered async notifications are added to `processBlock`. Retain the current generation scheduling initially; remove only browser-specific presentation work after regression tests establish equivalence.

### Gestures, history, and external changes

Use explicit begin/update/commit/cancel transactions with one undo entry per completed gesture. Selection changes alone are not musical edits. Store before/after values for the fields an edit owns, rather than restoring an entire stale project for a one-cell edit. Treat compound pattern operations as one bounded transaction. Preserve native text-editor undo while a text field has focus.

During an edit, keep the working draft for affected fields and accept unrelated engine updates. If host state restore, project load, or reset replaces the document, cancel the gesture without writing its stale draft and clear history/selection. For overlapping external cell changes, cancel the affected gesture and refresh authoritative state; do not silently overwrite newly recorded notes. Reset redo on new edits. On editor destruction, flush an already-applied gesture's pending final values and close parameter gestures; on explicit Escape, restore only gesture-owned changes. Cancel uncommitted drafts on focus/capture loss and prevent stuck pad keys.

### Layout and rendering

Use one `LayoutMetrics` result for painting, child bounds, hit testing, popover anchors, and minimum editor size. Preserve the existing column structure and formulas: 320-pixel sidebar, 34-pixel pre-grid gap, 54.375 logical-pixel base cell width, 16-step grid footprint, 4-pixel row gap, 200-pixel preview area, 40-pixel footer, and 44-pixel standalone transport row. Reconcile these with measured reference bounds before fixing the final base dimensions.

Keep fewer-than-16-step grids anchored left; compress 17–32 steps into the same footprint. Preserve supported pattern-view scaling independently of overall UI scale. Persist overall scale at 50–120% through native preferences/project metadata, using a single root transform and corresponding coordinate conversion. Fit the host window to content, rather than stretching the grid to match the old conflicting aspect constraint. Let JUCE handle display DPI separately from user scale.

Custom-paint the dense grid and generated preview, with row/cell dirty regions. Use native components for controls, editable text, menus, and focus. Compute geometric paths and expensive static decorations when size/state changes. Animate note flashes and play position with lightweight message-thread invalidation, targeting 60 Hz while visible and active; do not repaint the whole editor continuously. No engine calls or JSON work in painting. Provide control labels, keyboard focus, and accessible grid-cell information despite custom painting.

## Required workflow coverage

| Area | First-delivery requirements |
| --- | --- |
| Projects | New, Load, Save, previous/next project, name/description/date, cancellation and error handling, atomic save, `.kshproject` compatibility, DAW state restore. |
| Patterns | P1–P16, silent/M pattern, select/copy workflow, per-pattern step count and rate, triplet options, recording toggle, clear, nudge, undo/redo. |
| Macros | Swing, swing subdivision, velocity/timing humanize; preserve device-active and rate host parameters, including controls only where currently exposed. |
| Channels | Labels/rename, note number/name display and editing, audition, playback direction modes, mute/solo, mute painting, row reset/clear, loop start/end handles, row shifts. |
| Grid | Toggle cells, horizontal painting, vertical value editing, velocity/probability/roll/cycle displays, cycle masks/offset behavior, velocity lines, loop/inactive visual states, selection and marquee. |
| Bulk editing | Shift-add/remove selection, move, Option-copy, relative value edits, wrapped destinations and collision rules, selection inspector, mixed-value indicators. |
| Cycle editor | Anchored popup and inspector editing, length/mask manipulation, previews, commit/cancel, bounded placement and dismissal. Preserve imported values up to engine limits; do not truncate them to the shorter visible editor. |
| Generation | Off/static, Stack, Per Channel modes; refresh length; per-channel pattern locks; generated preview; coordinated source/generated note flashes. |
| Input | Existing modifier-layer mapping, layer shortcuts, Cmd-Z/Shift-Cmd-Z, Escape, ASDFGHJK audition/record keys with repeat suppression, standalone Space transport, correct text-field focus. |
| Standalone | Native play/stop and tempo, existing MIDI routing, existing iOS standalone/AUv3 configuration, touch-accessible menus and controls. Desktop modifiers remain desktop behavior. |
| Appearance | Fixed Alternative palette, existing channel colors, recognizable branding, main section geometry, scaling, selected/hover/disabled/playing states, readable labels at 32 steps. |

Capture exact gesture constants and numerical behavior from `kshConstants.js`, `kshEditorInteractions.js`, `kshBulkEdit.js`, `kshCellInspector.js`, and `cyclePattern.js`. For example: 4-pixel paint/value thresholds, separate bulk-move threshold, 2-pixel velocity/probability increments, and 8-pixel cycle/roll increments. Account for JavaScript versus C++ rounding at negative half-steps when porting helpers. Convert native zero-based indices at the existing command boundary exactly once.

Only migrate reachable features. Unused CSS names and helper functions are not evidence of an extra screen or feature requirement.

## Implementation sequence and checkpoints

### 1. Freeze a usable reference

- Preserve the current dirty working tree with a local baseline patch/snapshot before refactoring; never start from a clean HEAD that omits current work.
- Capture the running WebView in Alternative mode with representative project fixtures at 16 and 32 steps, multiple channel counts, default and non-default scales, selection, inspector, cycle popup, muted/solo states, and playback flashes. Record exact bounds, font availability, scale, and display DPI.
- Save small, deterministic state/gesture fixtures and expected outcomes from the existing helpers while they remain runnable. Use seeded engine tests for generated MIDI; do not compare unrelated random runs.
- Run the existing baseline build/tests and record any pre-existing failures separately.

Checkpoint: a reference state, screenshot, and interaction checklist exists for every major section, and existing uncommitted changes are recoverable.

### 2. Make the processor usable without a browser

- Introduce the lean read model, revision invalidation, safe native observer lifecycle, and controller command boundary.
- Port file operations away from `WebBrowserComponent::NativeFunctionCompletion` and `JUCE_WEB_BROWSER` guards.
- Establish macro gesture handling and batched cell edits. Adapt bridge tests to native commands while retaining their behavior assertions.
- Test constructor/open/close/state restore and MIDI behavior before building out every control.

Checkpoint: native code can edit/read a pattern and receive transport/hits with no WebView instance and no stale callback or lock re-entry.

### 3. Build the complete native screen

- Embed the font and icons; implement palette, control styling, metrics, and reusable value/text controls.
- Assemble every section in its final location, connected to real state and commands. Match the Alternative reference composition.
- Implement grid and preview painting, channel controls, pattern bank, macro controls, generation controls, footer, and standalone transport.

Checkpoint: all visible controls perform their actions. A static or partially wired mockup does not satisfy this checkpoint.

### 4. Complete editing semantics

- Port selection, drag modes, copying/moving, velocity drawing, cycle editing, loop editing, inspector transforms, and the remaining keyboard/modifier rules.
- Complete transaction history and cancellation, including external changes during gestures.
- Verify project operations and state restore with the full editor, including reopening while playback continues.

Checkpoint: execute every row of the workflow matrix end-to-end; fix functional regressions before polishing small visual differences.

### 5. Remove the web frontend completely

- Replace `PluginEditor`'s browser ownership with native view/controller ownership and remove the browser-only bridge/resource provider.
- Remove `KSH_UI_DEV_SERVER`, `NEEDS_WEB_BROWSER`, `NEEDS_WEBVIEW2`, `include(WebViewUI)`, and asset build dependencies. Compile with `JUCE_WEB_BROWSER=0`; remove web-only flags only where no other feature requires them.
- Remove `cmake_project/WebViewUI.cmake`, the Svelte/Vite frontend, and generated `assets/webview` content after reference capture. Git and the saved baseline preserve their history. Keep screenshot/behavior fixtures outside binary asset globs.
- Package only native fonts/artwork and their required licenses. Inspect the asset list to ensure stale `ui.zip` is absent.
- Update README, CLAUDE/AGENTS guidance, UI-sync docs, and any relevant CI/editor tasks. Preserve existing iOS and format configuration; avoid changes to shared CMake submodules unless needed.

Checkpoint: a clean native build succeeds without discovering or invoking Node/npm and contains no WebView or web bundle. No frontend-selection build switch remains.

### 6. Validate the integrated replacement

- Reconfigure Debug after CMake changes, build all configured desktop targets with `cmake --build Builds`, fix warnings, and run `./Builds/Tests`.
- Perform a fresh out-of-tree native configure/build to catch stale generated resources or objects. External CMake dependencies may still require their existing cache/network access; only frontend Node/npm and runtime web access are removed.
- Build Release in a separate directory for performance comparison; use the same project, sample rate, buffer size, and host as the captured baseline.
- Validate AU, VST3, CLAP, desktop AUv3, and Standalone artifacts. Smoke-test Logic's MIDI FX AU route and Ableton's VST3 MIDI-output route where those hosts are available. Verify editor close/reopen, two instances, host automation, project restore, and MIDI note-off behavior.
- Compile the existing iOS Standalone/AUv3 configuration with the installed toolchain and exercise touch/resize on an available simulator/device. Explicitly report unavailable host/device checks rather than claiming they passed.

Checkpoint: publish an implementation report with test results, build artifacts, native screenshots, performance observations, and any remaining cosmetic differences.

## Verification that matters

Focused Catch2 tests should cover observable outcomes:

- A representative gesture fixture produces the expected affected cells and exactly one undo action; redo restores it, Escape cancels it, and paint/value/move/copy thresholds behave consistently.
- Relative inspector changes preserve inter-cell differences until clamped; masks, rolls, reverse ramps, wrapped moves, and copy collisions match the reference.
- A bulk edit publishes a coherent playback snapshot; the audio thread cannot see a half-copied pattern. Metadata-only edits do not reset playback.
- Automation and pattern selection update controls without duplicate writes; begin/end parameter gestures are paired, including cancellation and editor teardown.
- Host restoration and MIDI recording cannot be overwritten by a stale UI draft. Opening/closing an editor cannot lose notes, drain the hit queue from two places, or call a dead observer.
- Existing project fixtures load and round-trip musical state and metadata. Old theme values are accepted and rendered with the fixed palette.
- Layout and hit-testing use identical coordinates at representative scales and 1/16/32 steps, with no popup or inspector clipping.

Use native offscreen component rendering on the JUCE message thread for repeatable screenshots, then inspect the actual Standalone/host window for focus, scaling, and input. Compare against the captured web references: major section boundaries and control alignment should be within one logical pixel at the reference size after normalizing display scale; text antialiasing can differ. Fix clipping, unreadable text, and misleading hit targets before delivery. Record small accepted visual differences rather than disguising them with a permissive screenshot threshold.

Measure editor-open time, idle CPU, active repaint cost, drag responsiveness, and memory over repeated editor opens. Aim to keep routine visible updates within a 16.7 ms frame at 60 Hz on the test machine, and report measured results rather than claiming native code automatically guarantees that. Run a sustained playback/editing session with dense 32-step patterns and rolls; check MIDI output and note-offs before and after. Keep performance optimizations driven by those measurements.

## Completion criteria

The migration is complete when the fixed-orange native editor covers the workflow matrix, old projects and DAW state load correctly, engine tests pass, configured targets build without warnings, no web UI is built or shipped, and the rendered native interface has been visually inspected. Any unavailable host/device validation must be called out with its practical limit. The follow-up backlog may contain visual polish; it must not conceal missing musical functionality.

Relevant JUCE references: [Component](https://docs.juce.com/master/classjuce_1_1Component.html) for native component ownership/input, [Graphics](https://docs.juce.com/master/classjuce_1_1Graphics.html) for drawing, and [ParameterAttachment](https://docs.juce.com/master/classjuce_1_1ParameterAttachment.html) for message-thread updates and parameter gesture semantics. Implementation should use the checked-in JUCE headers as the version authority.
