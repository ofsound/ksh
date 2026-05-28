# CLAUDE.md

This file provides guidance for AI coding agents working in this repository. See also `AGENTS.md` (symlink to this file).

## About This Project

**KSH** by **ofsound** — a MIDI effect plugin. Built on the [Pamplejuce](https://github.com/sudara/pamplejuce) template with JUCE 8, CMake, C++23, Catch2 tests, and CLAP/AU/VST3/AUv3/Standalone formats.

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

Built plugins are copied to `~/Library/Audio/Plug-Ins/` when `COPY_PLUGIN_AFTER_BUILD` is on (Debug builds too).

Builds default to **Debug** for development (faster builds, better debugging). Use **Release** if you hit audio dropouts or high CPU in a DAW.

## WebView UI (Svelte + Vite + Tailwind)

The plugin editor is a JUCE 8 `WebBrowserComponent` ([overview](https://juce.com/blog/juce-8-feature-overview-webview-uis/)). Frontend lives in `ui/`.

```bash
# Hot reload (Debug plugin loads http://localhost:5173)
cd ui && npm install && npm run dev

# Production bundle → assets/webview/ui.zip (embedded via BinaryData)
cd ui && npm run build
```

`cmake --build Builds` runs the UI build automatically when `ui/` sources change (requires `npm`).

JUCE JS helpers: `JUCE/modules/juce_gui_extra/native/javascript/` (Vite alias `@juce`).

Melatonin Inspector was removed — it only applies to native JUCE widgets, not WebView UIs.

## Agent workflow (rebuild every time)

After any change to `source/`, `CMakeLists.txt`, `ui/`, or plugin-related CMake modules:

1. Reconfigure only if CMake changed: `cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug`
2. Build: `cmake --build Builds` (installs VST3, AU, Standalone, CLAP to system plugin folders)
3. Fix compile errors before finishing the task; do not leave rebuilds to the user unless their toolchain is missing (cmake/ninja/Xcode).

Run `./Builds/Tests` when test or processor behavior changes.

## Project Structure

- `source/` - Plugin source code (PluginProcessor, PluginEditor, WebViewResourceProvider)
- `ui/` - Svelte/Vite/Tailwind WebView frontend
- `cmake_project/` - Project-specific CMake (WebViewUI.cmake)
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data; WebView bundle in `assets/webview/ui.zip`)
- `packaging/` - Installer resources and scripts

## Architecture

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `Assets.cmake` - Auto-includes all files in assets/ as binary data
- `Tests.cmake` - Configures Catch2 test target
- `Benchmarks.cmake` - Configures Catch2 benchmark target
- `PamplejuceIPP.cmake` - Intel IPP integration (optional, disabled for KSH)

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (Standalone AU VST3 AUv3)
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
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

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
