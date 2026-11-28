# KSH MIDI Sequencer

KSH by ofsound is a MIDI step sequencer for macOS, iPad, and landscape iPhone. It runs standalone or as a MIDI effect in a DAW.

## Sequencing

- Build up to 16 patterns with up to 64 steps each. Edit velocity, probability, cycle, and Split/Span per step, or use a row to send MIDI CC.
- The grid, Generate, and Pads start with eight rows. Turn on **16 Tracks** in Settings to access all 16. Turning it off preserves the extra rows and their playback.
- Use **Generate** to build a drum pattern or **Jam** in standalone to capture one from pads, the computer keyboard, or incoming MIDI.
- Click the project name or **Load** to browse projects and favorites or edit the current project’s details.

## Formats and routing

- In Logic, insert the AU in a **MIDI FX** slot before the instrument.
- In Ableton Live, insert the VST3 in the instrument slot of a MIDI track. On a second track with a synth, set **MIDI From** to the KSH track, select **KSH** as the source, and set monitoring to **In**.
- The macOS Ninja build produces Standalone, AU, VST3, and CLAP. Xcode builds AUv3. See the [iOS build instructions](docs/iphone-release.md) for iPad and iPhone presets.

## Build and test

On macOS, install Xcode or its Command Line Tools, CMake, and Ninja.

```sh
git submodule update --init --recursive
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Builds
ctest --test-dir Builds --output-on-failure
```

The Debug standalone bundle is at `Builds/KSH_artefacts/Debug/Standalone/KSH.app`. With `KSH_COPY_PLUGIN_AFTER_BUILD` enabled, CMake copies desktop plugins to `~/Library/Audio/Plug-Ins/`. Use a Release build for performance testing.

### Ableton Link

Link shares tempo and beat phase in standalone. In the full editor, open **Link** in the footer. On iPhone, use **Menu → Settings**.

- For macOS builds, point `KSH_ABLETON_LINK_SOURCE_DIR` at the Ableton Link C++ SDK checkout with its Asio submodule.
- For iOS builds, point `KSH_LINKKIT_SDK_DIR` at the extracted LinkKit SDK. The app’s provisioning profile needs Apple’s multicast networking entitlement, and peer discovery requires Local Network access.

## Development and releases

KSH uses JUCE 8, C++23, CMake, and Catch2. The sequencer engine lives in `source/engine/` and the native editor in `source/ui/`. The audio thread reads immutable playback snapshots and evaluates steps in real time. See [Playback](docs/native-playback.md) and [UI synchronization](docs/ui-sync.md).

GitHub Actions runs build and validation jobs for macOS and iOS. See [TestFlight](docs/testflight.md) for signed builds and archive verification.
