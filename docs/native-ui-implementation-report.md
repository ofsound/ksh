# Native frontend implementation and validation

2026-10-13. KSH now uses native JUCE components and drawing throughout its editor, with the fixed Alternative palette and orange accents. The Svelte application, Vite/npm build, embedded web bundle, browser resource provider, and browser editor have been removed. `JUCE_WEB_BROWSER=0` is required by the build.

## Delivered

- Embedded Gantari Regular, Semibold, and ExtraBold fonts with their OFL license; native vector icons, gradients, channel colors, focus/selection/disabled states, and one shared layout/hit-test geometry.
- Project controls and asynchronous file choosers, metadata, atomic saves, previous/next project navigation, P1–P16/M, pattern copy, step count/rate, recording, macros, standalone transport and tempo.
- Channel naming, note editing, audition, directions/ping-pong, mute/solo and mute painting, clear/reset, shifts, and loop handles.
- Grid toggle/paint/value gestures, velocity/probability/roll/cycle layers, marquee and additive selection, wrapped move/copy, velocity lines, relative inspector edits, cycle popup and inspector, generation controls/channel locks, and generated preview.
- Keyboard layer changes, audition pads, undo/redo, cancellation and text-entry handling. Grid cells expose accessible names and values. Overall scale persists at 50–120% using one root transform.
- Revision-based UI refresh, bounded cell batches, stale-cell and document-epoch validation, compact conditional undo history, and note-hit notifications. Recording and document replacement cannot be overwritten by an old cell draft. Playback updates repaint affected regions instead of continuously repainting the entire editor.

The implementation is in `source/ui/`; `PluginEditor` owns the native view and project operations. `KshUiBridge` now carries native revision/hit notifications. Its compatibility command adapter has no browser dependency. Host parameter IDs, plugin IDs, strict `v:1` persistence, and the existing audio-thread processing path are preserved. Legacy theme metadata is accepted; newly saved projects use `alt`.

## Automated validation

| Check | Result |
| --- | --- |
| Debug desktop build and installation | Passed: Standalone, AU, VST3, CLAP, tests and benchmarks |
| Fresh separate Release build | Passed without a Node/npm frontend build or stale web assets |
| macOS Xcode Standalone and AUv3 build | Passed with signing disabled; final incremental build returned exit 0 with no diagnostics |
| Debug tests with native screenshot capture | 153 cases, 2,555 assertions passed |
| Release tests | 153 cases, 2,546 assertions passed; screenshot-writing assertions are conditional |
| Apple `auval -v aumi Ksh1 Osnd` | AU VALIDATION SUCCEEDED |
| iOS simulator Standalone and AUv3 compile | Passed with signing disabled; runtime touch testing remains unverified |
| Release Standalone symbol inspection | Zero `WebBrowserComponent`, `WKWebView`, or `WebViewResourceProvider` symbols |
| Source/build resource inspection | No `ui/`, `assets/webview/`, browser provider, dev-server switch, or `ui.zip` dependency |
| Whitespace and compiler diagnostics | `git diff --check` passed; final build logs contain no compiler warnings/errors |

The ten new native test cases cover atomic edits, conditional undo, external recording conflicts, cancellation, document replacement, wrapped move/copy, geometry and rounding, actual grid mouse handlers, repeated editor lifetime, and dense rendering at 50/100/120%. Existing processor, persistence, bridge-command and MIDI tests remain part of the suite. This is focused regression coverage, not an exhaustive automated proof of every gesture combination.

CMake emits an existing upstream `readerwriterqueue` minimum-version deprecation notice. JUCE's VST3 packaging repairs an intermediate ad-hoc signature during rebuilding. Neither is a C++ compile warning. JUCE's macOS module metadata still links the system WebKit framework; browser implementation code and browser assets are absent.

## Visual and interactive checks

The actual Standalone editor was inspected, including a reversible cell toggle/undo and scale change/restoration. Native offscreen captures were inspected at 16 and 32 steps, including selection, cycle state, and 50/100/120% scales. The original web capture and native captures are retained in [native-ui-reference](native-ui-reference/).

Ableton Live 12 loaded the installed KSH VST3 onto a MIDI track. The host reported its plugin-window toggle enabled, but the automation interface exposed only the main Live window, so hosted editor interactions and the complete second-track MIDI-output route are **not marked passed**. Logic's startup plugin scan prevented completing its MIDI FX route check. AU validation above is separate evidence and does not substitute for a Logic session.

No sustained DAW playback/editing soak, two-instance host automation/restore session, physical iOS device test, or simulator touch test is claimed. These remain release-validation work. The Live test set is unsaved.

The native composition follows the Alternative reference, but exact pixel parity is not claimed. Font antialiasing, shadow softness, some vector icon details, branding depth, and the cycle editor's native length control differ. These are visible native design refinements to review against the retained reference.

## Release performance observations

Catch2 microbenchmarks on the development machine, 20 samples for native operations:

| Operation | Mean |
| --- | ---: |
| Dense 32-step full offscreen render | 19.26 ms |
| Eight-channel flash-strip offscreen repaint | 1.10 ms |
| 32-cell edit, history commit, and snapshot publication | 2.34 ms |
| Editor construction/destruction, 10 samples | 2.63 ms |

These are CPU timings in the test executable, not end-to-end host frame latency. Full-window rendering exceeds a 16.7 ms frame budget on this run; routine playback therefore relies on dirty-region painting. The unchanged-revision controller poll also has a microbenchmark, but its nanosecond result is not a measurement of process idle CPU. No measured memory/idle-CPU improvement over WebView or sustained performance guarantee is claimed. Raw benchmark and test results are retained alongside the screenshots.

## Build and evidence locations

- Development artifacts: `Builds/KSH_artefacts/Debug/`; the desktop build installs the plugins to the normal user plugin folders.
- Release artifacts: `Builds-native-release/KSH_artefacts/Release/`; copying is disabled for this validation build.
- macOS AUv3 and containing app: `Builds-native-macos-xcode/KSH_artefacts/Debug/`; Xcode is required for this format.
- iOS simulator artifacts: `Builds-iOS/KSH_artefacts/Debug/`.
- Screenshots and compact validation logs: `docs/native-ui-reference/`.
- Pre-migration dirty changes and web source were preserved under `/private/tmp/ksh-native-baseline/` before replacement. Those temporary backups are not durable release artifacts; Git retains the original tracked sources.

The native implementation and build migration are delivered. Host soak testing and exact visual acceptance remain explicitly open rather than being hidden behind the passing unit suite.
