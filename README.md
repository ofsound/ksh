# KSH

KSH by ofsound is a JUCE 8 MIDI effect plugin port of a Max for Live prototype. It uses the Pamplejuce CMake layout, a pure C++ engine in `source/engine/`, and a Svelte/Vite WebView UI in `ui/`.

## Formats

- Logic: load the AU in a MIDI FX slot before the instrument.
- Ableton Live: use the VST3. Live does not expose AU MIDI-out, and third-party MIDI generators need to be routed from an instrument slot track into another MIDI track.
- Builds currently target Standalone, AU, VST3, AUv3, and CLAP through JUCE/Pamplejuce.

## Build

```sh
git submodule update --init --recursive
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Builds
./Builds/Tests
```

The build copies plugin formats into the local macOS plugin folders when `COPY_PLUGIN_AFTER_BUILD` is enabled.

For WebView hot reload in a Debug plugin:

```sh
cd ui
npm install
npm run dev
```

Production UI assets are bundled into `assets/webview/ui.zip` by:

```sh
cd ui
npm run build
```

`cmake --build Builds` runs the UI build automatically when UI sources change.

## Architecture

- `KickSnareHatEngine` owns pattern state, source generation, persistence, and preview state.
- The processor publishes immutable `PlaybackSnapshot` objects to the audio thread through `RealtimeMailbox`.
- `processBlock` never locks or touches mutable engine state. It evaluates MIDI live from the current snapshot with fixed storage and a realtime-safe RNG in `MidiPlaybackRunner`.
- Audio-to-UI note hits are sent through a lock-free FIFO and drained on the message thread by a timer.
- Host-automatable macro controls are APVTS parameters: rate, swing, velocity humanize, timing humanize, and device active.
- Grid/source pattern state is custom `v:1` JSON, not APVTS parameters.
- Persistence accepts one strict JSON format. Legacy Max/M4L wrapper and chunk formats are intentionally rejected.
- UI, engine, and persistence use channel naming (`channelCount`, `channels`), not lane naming.

See:

- [docs/ui-sync.md](docs/ui-sync.md)
- [docs/native-playback.md](docs/native-playback.md)
- [docs/port-plan.md](docs/port-plan.md)

## Verification

After changes to `source/`, `ui/`, plugin CMake, or CMakeLists:

```sh
cmake --build Builds
./Builds/Tests
```

Reconfigure first if CMake files changed:

```sh
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
```
