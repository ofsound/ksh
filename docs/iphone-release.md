# Landscape iPhone release

KSH has a dedicated compact native editor optimized for the iPhone 14 Pro Max in landscape. The reference screen is 932 × 430 points, with an 814 × 409 point content area after landscape safe-area insets. The interface uses the existing engine, controller, undo history, host parameters and strict v:1 project format.

## Editing

- All eight channels and 16 steps are visible above an always-visible 84-point inspector. Grid cells are approximately 42 × 30 points at the reference size; the eight-row requirement determines their height. There is no vertical scrolling in the primary editor.
- Tap to toggle cells, drag across a row to paint, and drag vertically to edit velocity or probability. Hold a cell to select it, or latch Select and tap/drag a selection. Deselect returns to drawing. Copy enables duplication while moving a selection without a hardware modifier key.
- The inspector adjusts selected enabled cells together: relative velocity, relative probability, roll, cycle length and eight cycle beats. The compact cycle length has its own numeric control instead of the desktop's narrow drag handle.
- Patterns of 17–32 steps use explicit 1–16 and 17–32 pages. Playback continues across both pages; page changes are manual so the grid does not move underneath a finger.
- Tap a channel name to audition, power to mute, or its `...` button for note, name, loop start/length, direction, ping-pong, solo, clear and reset.
- The pattern bank opens from P1/P2/etc. Select + pattern chooses a copy source; choosing a destination copies and returns to editing. Done or tapping the bank button again cancels a pending copy.
- Controls opens Project, Pattern, Feel, Generate, Channel and Help pages. They replace the grid while leaving the inspector and transport visible. Generate scrolls to the generated-output preview below its mode and channel locks. Done returns to the grid.
- Project files use the native Files picker. The standalone publishes `KSH Output`; choose that MIDI source in the receiving music app. AUv3 uses its host's transport and MIDI routing.

Compact geometry is independent of saved desktop UI/pattern zoom and is not persisted in the musical project. Wide desktop/iPad geometry is retained. On iOS a landscape editor viewport up to 1100 × 500 points chooses the compact layout; smaller landscape views scale down from its minimum 814 × 390 design area. The standalone respects the safe area in both landscape orientations. AUv3 follows the bounds supplied by its host.

Resize, page changes, auxiliary panel switches and focus loss cancel active editing gestures through the existing conflict-aware controller. External recording and document replacement retain the existing conflict rules.

## Build

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

`NativeIphoneTests.cpp` covers eight-channel geometry, inspector/control containment, auxiliary pages, pattern selection, page-two cell addressing and undo, resize cancellation and inspector gesture recovery. Set `KSH_NATIVE_CAPTURE_DIR` while running `./Builds/Tests '[iphone]'` to capture the native layout and auxiliary pages at 2× resolution.

Verified on 2026-10-17:

- Desktop Debug build completed and installed the local plugin formats without compiler warnings.
- Full suite: 177 test cases passed; the final focused iPhone run passed all four test cases (including copy, paging, inspector recovery and screenshots).
- iPhone Release device app and embedded AUv3 built successfully as arm64 with signing disabled. The app plist contains device family 1 and only landscape-left/landscape-right orientations.
- iPhone 14 Pro Max simulator on iOS 17.5: launched in landscape, visually checked safe-area placement and eight-row/inspector visibility, tapped four steps, selected a cell, opened/closed Controls and extended its cycle via the inspector's beat buttons.
- Physical-device MIDI delivery, AUv3 hosting in an iPhone DAW, device signing and TestFlight distribution remain unverified.

Reference captures: [editor](iphone-reference/editor.png), [channel settings](iphone-reference/channel.png), [generation controls](iphone-reference/generate.png). These are native component renders at the reference safe-area size, with a populated test pattern. Test outputs are retained in `iphone-reference/`.
