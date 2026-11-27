# KSH

KSH by ofsound is a JUCE 8 MIDI effect plugin port of a Max for Live prototype. It uses the Pamplejuce CMake layout, a pure C++ engine in `source/engine/`, and a fully native JUCE frontend in `source/ui/`.

## Formats

- Logic: load the AU in a MIDI FX slot before the instrument.
- Ableton Live: use the VST3. Live does not expose AU MIDI-out, and third-party MIDI generators need to be routed from an instrument slot track into another MIDI track.
- The default Ninja build produces Standalone, AU, VST3, and CLAP. AUv3 is configured too, but only the Xcode generator builds it (desktop or iOS).

## Build

```sh
git submodule update --init --recursive
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Builds
./Builds/Tests
```

The build copies `KSH.component`, `KSH.vst3`, and `KSH.clap` into `~/Library/Audio/Plug-Ins/` when `KSH_COPY_PLUGIN_AFTER_BUILD` is enabled (the default on macOS). The Debug standalone app is `Builds/KSH_artefacts/Debug/Standalone/KSH.app`.

The frontend uses JUCE components, custom grid painting, embedded Gantari fonts, and the fixed orange Alternative palette. The build has no web frontend: Node, npm, a browser, and a UI server are not required.

### Ableton Link in standalone

Open **Ableton Link** next to MIDI Output in **Menu → Settings** on iPhone, or next to **MIDI Out** in the iPad and Mac footer. Enable Link to share tempo and beat phase with peers on the local network. **Start/Stop Sync** is optional. With peers connected, play starts on the next four-beat boundary. The Stop button flashes while a launch is pending and cancels the launch when pressed.

- macOS 11 or later: configure with `-DKSH_ABLETON_LINK_SOURCE_DIR=/path/to/ableton-link`. Use the official C++ SDK checkout with its Asio submodule. Proprietary releases require the appropriate license from [Ableton](https://ableton.github.io/).
- iPhone and iPad: extract [LinkKit 4.1.3](https://github.com/Ableton/LinkKit/releases/tag/LinkKit-4.1.3) into `_deps/linkkit` before configuring a new build with CMake 3.28 or newer. To update an existing build or use another SDK location, configure with `-DKSH_LINKKIT_SDK_DIR=/path/to/extracted/LinkKit`. The standalone app's provisioning profile needs Apple's multicast networking entitlement. The user must also allow Local Network access for peer discovery.

The SDKs are local build inputs. Configure with an empty SDK path for builds without Link. Hosted versions follow the DAW playhead. Link controls timing, and the MIDI output menu still controls where KSH sends notes.

## iPhone

A landscape iPhone editor keeps eight channels, 16 steps and the inspector visible, with secondary controls in touch-accessible pages. The separate `ios-iphone-release` CMake preset produces an iPhone-only app with its AUv3 extension. See [iPhone build, controls and validation](docs/iphone-release.md).

## Native editor

- Click cells to toggle; drag horizontally to paint or vertically to edit values.
- Shift selects cells; drag a selection to move it, Option-drag to copy it, and Shift-drag to change its values.
- Option temporarily edits probability; Shift-Option edits roll. Keys 1–4 choose a layer.
- Right-click a cell opens its cycle editor. Drag loop handles to resize a channel loop.
- Shift-click a pattern chooses a copy source; click a destination to copy it.
- Click channel labels to audition; double-click to rename. ASDFGHJK plays channels.
- Click/drag channel power controls to mute; Shift-click solos; right-click resets a row.
- Hold Shift (or latch Select) and hover a channel power control to reveal its gold S solo cue.
- Numeric controls support dragging, scrolling, arrow keys, double-click entry, and right-click reset. A burst of scroll-wheel ticks is one undo step.
- Cmd-Z / Cmd-Shift-Z undo/redo; Escape cancels a grid gesture; Delete clears selected cells.
- Standalone provides Play/Stop, tempo, and Space transport. The DC control switches channel colors; UI scale is 50–120%.

## Architecture

- `KickSnareHatEngine` owns pattern state, source generation, persistence, and preview state.
- The processor publishes immutable `PlaybackSnapshot` objects to the audio thread through `RealtimeMailbox`.
- `processBlock` never locks or touches mutable engine state. It evaluates MIDI live from the current snapshot with fixed storage and a realtime-safe RNG in `MidiPlaybackRunner`.
- Audio-to-UI note hits are sent through a lock-free FIFO and drained on the message thread by a timer. Other message-thread work is requested with an atomic flag polled by the same timer; nothing posts to the OS message queue from the audio thread.
- `KshUiBridge` only carries a revision counter, a document epoch, and the note-hit callback. The native editor reads typed engine snapshots and sends edits through `KshEditorController`.
- Host-automatable macro controls are APVTS parameters: rate, swing, velocity humanize, timing humanize, and device active.
- Grid/source pattern state is custom `v:1` JSON, not APVTS parameters.
- Persistence accepts one strict JSON format. Legacy Max/M4L wrapper and chunk formats are intentionally rejected. Retired editor keys in older saves (`projectThemeMode`, `patternViewScale`, `standaloneTempo`) are ignored.
- UI, engine, and persistence use channel naming (`channelCount`, `channels`), not lane naming.

See:

- [docs/ui-sync.md](docs/ui-sync.md)
- [Native frontend implementation and validation](docs/native-ui-implementation-report.md)
- [docs/native-playback.md](docs/native-playback.md)
- [docs/port-plan.md](docs/port-plan.md)

## Verification

After changes to `source/`, `tests/`, assets, or CMake files, build every format and run the tests:

```sh
cmake --build Builds
./Builds/Tests
```

Warnings are treated as errors. After a rebuild, quit and reopen the standalone app, and reload the plugin in your DAW. Reconfigure first if CMake files changed:

```sh
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

GitHub Actions runs **KSH CI** for pull requests and pushes to `main`. The macOS job runs CTest, pluginval and `auval`, then uploads the universal standalone, AU, VST3 and CLAP bundles. The iOS job builds the iPhone/iPad standalone and checks the embedded AUv3. The manually triggered **Apple archive validation** workflow checks unsigned release archives for iOS, iPhone-only and macOS TestFlight. See [TestFlight](docs/testflight.md) for signed archive verification.
