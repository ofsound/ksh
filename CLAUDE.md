# CLAUDE.md

This file provides guidance for AI coding agents working in this repository. See also `AGENTS.md` (symlink to this file).

## About This Project

**KSH** by **ofsound** — a MIDI effect plugin. Built on the [Pamplejuce](https://github.com/sudara/pamplejuce) template with JUCE 8, CMake, C++23, and Catch2 tests. The default Ninja build produces Standalone, AU, VST3, and CLAP; AUv3 is configured but only built by the Xcode generator (desktop or the iOS presets in `docs/iphone-release.md`).

Plugin-specific logic lives in `source/`. CI is macOS-only for now; code signing is disabled until release.

**Hosts:** Logic — load the **AU** in the top **MIDI FX** slot (before the instrument). **Ableton** — use **VST3** only (Live does not expose AU MIDI-out). Live cannot place third-party plugins in the built-in MIDI-effects row; load **KSH** in the **instrument** slot on one MIDI track, put your synth on a second track, and set **MIDI From** → first track → **KSH** (Monitor **In**) — [Ableton guide](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in).

## Build Commands (VS Code / Cursor)

Use the **CMake Tools** extension (`.vscode/settings.json` points at `Builds/` and Ninja).

**macOS prerequisites** (once): Xcode or Xcode Command Line Tools, Homebrew CMake ≥ 3.25, and Ninja:

```bash
xcode-select --install          # if needed
brew install cmake ninja
```

```bash
# One-time: fetch submodules
git submodule update --init --recursive

# Configure (after CMakeLists.txt changes)
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build Builds

# Run tests
ctest --test-dir Builds --verbose --output-on-failure
# or: ./Builds/Tests

# Run a single test
./Builds/Tests "[Plugin instance]"

# Release build (for DAW performance testing)
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
```

On macOS for universal binary: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to configure.

`KSH.component`, `KSH.vst3`, and `KSH.clap` are copied to `~/Library/Audio/Plug-Ins/` when `KSH_COPY_PLUGIN_AFTER_BUILD` is on (the macOS default, Debug builds too). Run the build outside the agent sandbox so the install step can write there. The Debug standalone app is `Builds/KSH_artefacts/Debug/Standalone/KSH.app`.

Builds default to **Debug** for development (faster builds, better debugging). Use **Release** if you hit audio dropouts or high CPU in a DAW.

## Native JUCE frontend

The complete editor lives in `source/ui/`. It uses a fixed Alternative palette with orange accents, native components and custom grid painting. There is no Svelte project, npm build, browser resource provider, or embedded web bundle. `JUCE_WEB_BROWSER=0` is required.

- `KshStyle.h`: palette, embedded Gantari typography, vector icons, native controls.
- `KshLayout.h`: shared rendering/hit-test geometry; one root UI-scale transform.
- `KshEditorController`: typed snapshots, batched edits, conditional undo/redo, selection and gestures.
- `KshPatternGrid`, `KshCycleEditor`, `KshPreviewPanel`, `KshNativeEditor`, `KshCompactEditor`: native interface.
- `KshUiBridge`: message-thread revision counter, document epoch, and note-hit callback only. There is no JSON command bus; editor and tests send edits through `KshEditorController` or `PluginProcessor::dispatchUiEngineCommand`.

The processor has no theme mode, pattern-view scale, or editor-resize callback. Older saves containing `projectThemeMode`, `patternViewScale`, or `standaloneTempo` load with those keys ignored; do not write them again.

Preserve strict `v:1` projects, host parameter IDs, audio-thread isolation, gesture cancellation, and external-recording conflict checks. See `docs/ui-sync.md` and `docs/native-ui-migration-plan.md`.

## Agent workflow (rebuild every time)

After any change to `source/`, `tests/`, `assets/`, `CMakeLists.txt`, or CMake modules (see `.cursor/rules/rebuild-after-changes.mdc`):

1. Reconfigure only if CMake or the asset list changed: `cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug`
2. Build: `cmake --build Builds` (Standalone, AU, VST3, CLAP, and Tests; installs the AU, VST3, and CLAP bundles)
3. Fix compile warnings before finishing the task; do not leave rebuilds to the user unless their toolchain is missing (cmake/ninja/Xcode).

Run `./Builds/Tests` when test or processor behavior changes. After a rebuild, quit and reopen the standalone app and reload the plugin in the DAW.

## Project Structure

- `source/` - Plugin source code (PluginProcessor, PluginEditor, engine, native UI)
- `cmake_project/` - Project-specific CMake (`KshAssets.cmake` for the native font list, `IconBundleGuard.cmake`)
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (explicit native font list via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `cmake_project/KshAssets.cmake` - Embeds the explicit native asset list as binary data (the template's `Assets.cmake` glob is not used)
- `Tests.cmake` - Configures Catch2 test target
- `Benchmarks.cmake` - Configures Catch2 benchmark target
- `PamplejuceIPP.cmake` - Intel IPP integration (present but not included by KSH)

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats (desktop: Standalone AU VST3 AUv3; iOS: Standalone AUv3). CLAP is added separately through clap-juce-extensions on desktop.
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: LSP/clangd often reports false positive diagnostic errors (like "undeclared identifier", "file not found") because it doesn't have full context of the JUCE module system. Ignore these unless the actual build fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Playback state**: Message/host threads mutate `KickSnareHatEngine`, then publish an immutable `PlaybackSnapshot` through `RealtimeMailbox`. `processBlock` reads only that snapshot.
- **Audio → UI updates**: Use fixed-size atomics/FIFOs drained by the processor's message-thread `juce::Timer`; never call UI code, `triggerAsyncUpdate()`, JSON/string builders, or engine mutation from the audio thread. The processor is not an `AsyncUpdater`: message-thread work is requested with `requestMessageThreadWork()` (an atomic flag) and run by `runMessageThreadWork()` on the timer.
- **Playback evaluation**: `MidiPlaybackRunner` evaluates each step live from the snapshot. There is no precomputed playback table.
- **Engine locking**: `engineStateMutex` is only for message/host-thread access. The audio thread must never take it.

Host-automatable macro controls live in APVTS (`rate`, `swing`, `velocity_humanize`, `timing_humanize`, `device_active`). Pattern/grid state stays in strict `v:1` JSON using `channelCount` and `channels`; do not reintroduce legacy M4L wrapper/chunk persistence or UI `lane` naming.

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
