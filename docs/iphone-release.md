# Landscape iPhone release

KSH has a dedicated compact native editor optimized for the iPhone 14 Pro Max in landscape. The reference screen is 932 × 430 points, with an 814 × 409 point content area after landscape safe-area insets. The interface uses the existing engine, controller, undo history, host parameters and strict v:1 project format.

## Editing

- All eight channels and 16 steps are visible between a 34-point toolbar and an always-visible 48-point inspector. Grid cells are approximately 42 × 36 points at the reference size, about 20% taller than the previous layout. There is no vertical scrolling in the primary editor.
- Tap to toggle cells, drag across a row to paint, and drag vertically to edit velocity or probability. Hold a cell to select it, or latch Select and tap/drag a selection. Done clears the selection and returns to drawing. Done and Copy appear when selection is active; Copy enables duplication while moving selected cells without a hardware modifier key. Record is directly accessible in the editing toolbar.
- The single-row inspector places labels beside values and adjusts selected enabled cells together: relative velocity, relative chance, timing nudge, roll, cycle length and eight cycle beats. The compact cycle length has its own numeric control instead of the desktop's narrow drag handle.
- Patterns of 17–32 steps use explicit 1–16 and 17–32 pages. Playback continues across both pages; page changes are manual so the grid does not move underneath a finger.
- The numbered pad to the left of each channel name plays that row immediately on touch-down at velocity 100, using the same lock-free MIDI/recording path as ASDFGHJK. Each row can be played independently; holding, dragging or releasing a pad does not repeat the hit. Pads work with transport stopped; recording requires Record to be armed and transport running, and writes into the armed pattern with the current quantization/offset and recording undo. Pads follow their channel when rows are reversed and hide when that channel is removed. The full iPad layout has the same pads; desktop geometry is unchanged.
- Tap a channel name to audition, power to mute, or its `...` button for note, name, loop start/length, direction, ping-pong, solo, clear and reset.
- Pattern 1/2/etc. opens a dedicated four-by-four picker over the grid. Each slot shows its note count or Empty, and the current pattern has a colored marker. A pattern tap always selects, regardless of cell-selection mode. Duplicate Pn explicitly starts a copy: choose a destination and confirm Copy to Pn. Cancel copy returns to navigation; the close button, an outside tap or Escape dismisses without copying. Mute patterns is a separate labeled action. Replacing the document or resizing dismisses a pending copy.
- Settings opens Project, Pattern, Feel, Generate, View and Help pages and remembers the last tab. These pages replace the grid and selection inspector, leaving transport and undo/redo accessible. Channel settings opens contextually from a row and has no unrelated tabs. Generate scrolls to its output preview; View groups channel colors, graphical values, row order, accent and hardware-keyboard preferences. Done returns to the grid. Pattern settings separates grid length/division, recording/quantization and shift/clear actions. Timing options opens recording strength/offset choices without requiring a secondary click. Clearing a pattern requires a second, explicitly labeled confirmation; changing the pattern or document cancels that confirmation.
- Project files use the native Files picker. The standalone publishes `KSH Output`; choose that MIDI source in the receiving music app. AUv3 uses its host's transport and MIDI routing.

Compact geometry is independent of saved desktop UI/pattern zoom and is not persisted in the musical project. Wide desktop/iPad geometry is retained. On iOS a landscape editor viewport up to 1100 × 500 points chooses the compact layout; smaller landscape views scale down from its minimum 814 × 390 design area. The standalone respects the safe area in both landscape orientations. AUv3 follows the bounds supplied by its host.

Resize, page changes, auxiliary panel switches and focus loss cancel active editing gestures through the existing conflict-aware controller. External recording and document replacement retain the existing conflict rules.

## Shared iCloud project library

The iPhone and iPad app, including AUv3, use the fixed document container
`iCloud.com.ofsound.ksh`. Open and first Save / Save As start in its Documents
folder, shown as **KSH** in iCloud Drive. Devices signed into the same Apple
account with iCloud Drive enabled see the same library. The iPhone-only preset
uses this same container; it does not create a separate phone library.

When iCloud is unavailable, the default is the local app Documents/KSH folder,
visible through Files under On My iPhone/iPad. Existing local projects stay
local; use Save As and choose iCloud Drive to copy them into the shared library.
The Files picker can also open projects from other document providers.

Cloud discovery uses a metadata query so the library includes files that have
not downloaded yet. Downloads, coordinated reads and coordinated atomic writes
run on a background queue. Save compares the last opened/saved content digest
and checks unresolved file versions before replacing a file. If another device
changed it, Save refuses to overwrite it: use Save As to preserve your edits,
then reopen the original. This is file synchronization, not simultaneous editing.
Downloaded files remain usable offline, subject to iOS's file availability.

**Share** exports a self-contained `.kshproject` copy using the native share
sheet (AirDrop, Messages, Save to Files, etc.). It does not change the current
save destination. Save received copies in Files and open them with KSH’s Load action. AUv3 host-session state remains independent of exported files.
File access bookmarks and the last saved content digest live only in standalone
app preferences; they never enter project files or AUv3 host state. Replacing the
project cancels outstanding file dialogs. Edits made during an asynchronous save
remain marked as unsaved.

Signing setup: register `iCloud.com.ofsound.ksh` in the Apple Developer account
and enable iCloud Documents access to that container for **both** the containing
app and AUv3 App IDs/provisioning profiles. CMake points both targets at
`packaging/ios-projects.entitlements`; keep this fixed container when selecting
a signing team in Xcode. Unsigned simulator/device builds cannot verify iCloud
sync. Before release, test on signed iPhone and iPad devices using the same Apple
account: cross-device creation/opening, offline editing/reconnection, conflicting
saves, cancellation, standalone relaunch, AUv3 editor reopening, and sharing.

## Build

The iOS app and AUv3 use the opaque, full-bleed `packaging/icon-ios.png`; the padded desktop icon remains separate. Its vector source is `packaging/icon-ios.svg`; regenerate the PNG with `rsvg-convert packaging/icon-ios.svg -o packaging/icon-ios.png`.

The normal `ios-xcode` preset builds a universal iPhone/iPad app; iPhone orientations are landscape only and iPad orientations remain unchanged. The separate release preset limits the product to the iPhone device family:

```sh
cmake --preset ios-iphone-release
cmake --build --preset ios-iphone-release -- CODE_SIGNING_ALLOWED=NO
```

This produces an **unsigned** optimized arm64 app with its AUv3 extension embedded:

`Builds-iPhone/KSH_artefacts/Release/Standalone/KSH.app`

Open `Builds-iPhone/KSH.xcodeproj` in Xcode, select KSH_Standalone and choose a signing team for both the app and AUv3 extension to install on a physical phone or archive for TestFlight. Simulator artifacts are separate from device artifacts and cannot be installed on a phone. No App Store/TestFlight submission is performed by the build.

For a simulator build, use the same scheme with configuration Debug, SDK iphonesimulator and an installed iPhone simulator destination. Run simulator and device Xcode builds sequentially because CMake's project uses one build database.

## Verification

`NativeIphoneTests.cpp` covers eight-channel geometry, inspector/control containment, auxiliary pages, pattern selection independent of cell-selection mode, explicit pattern duplication/cancel/undo, document replacement during copying, page-two cell addressing, toolbar non-overlap at the minimum standalone size, remembered settings, clear confirmation, resize cancellation and inspector gesture recovery. Set `KSH_NATIVE_CAPTURE_DIR` while running `./Builds/Tests '[iphone]'` to capture the native layout and auxiliary pages at 2× resolution.

Verified on 2026-10-17:

- Desktop Debug build completed and installed the local plugin formats without compiler warnings.
- Full suite: 177 test cases passed; the final focused iPhone run passed all four test cases (including copy, paging, inspector recovery and screenshots).
- iPhone Release device app and embedded AUv3 built successfully as arm64 with signing disabled. The app plist contains device family 1 and only landscape-left/landscape-right orientations.
- iPhone 14 Pro Max simulator on iOS 17.5: launched in landscape, visually checked safe-area placement and eight-row/inspector visibility, tapped four steps, selected a cell, opened/closed Controls and extended its cycle via the inspector's beat buttons.
- Physical-device MIDI delivery, AUv3 hosting in an iPhone DAW, device signing and TestFlight distribution remain unverified.

Reference captures: [editor](iphone-reference/editor.png), [channel settings](iphone-reference/channel.png), [generation controls](iphone-reference/generate.png). These are native component renders at the reference safe-area size, with a populated test pattern. Test outputs are retained in `iphone-reference/`.

### iCloud integration verification (2026-10-21)

- Desktop Debug build and installed AU/VST3/CLAP passed without compiler warnings;
  full suite passed 245 test cases (325,870 assertions), including rejected
  downloaded projects retaining the current document and save association.
- Universal iPhone/iPad simulator Debug and arm64 device Release builds include
  the AUv3 extension. Generated app/extension plists expose the same named KSH
  container; both Xcode targets use the fixed iCloud entitlements file.
- iPhone 14 Pro Max / iOS 17.5 simulator: first Save opened the local KSH fallback
  folder, wrote a valid strict v:1 project (2,174 bytes), and Share opened the
  native sheet with that project. An external edit to the disposable saved file
  caused Save to display the conflict message and preserved the external content.
- Real iCloud account synchronization, offline reconnection and cross-device
  conflict propagation still require signed iPhone/iPad devices.
