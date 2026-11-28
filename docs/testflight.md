# KSH TestFlight and App Store

| Target | CMake preset | Delivered product |
| --- | --- | --- |
| `ios` | `ios-xcode` | iPhone and iPad app with the embedded KSH AUv3, as an `.ipa` |
| `macos` | `macos-testflight` | Sandboxed macOS 12+ standalone app for Apple silicon and Intel, as a `.pkg` |

Both apps use the KSH record for `com.ofsound.ksh` in App Store Connect.
The iOS extension uses `com.ofsound.ksh.kshAUv3`. Desktop plug-in distribution follows the desktop build.

## One-time setup

1. Use a Mac with Xcode signed in to your paid Apple Developer team. Set `KSH_APPLE_TEAM_ID` to that team's 10-character ID. Xcode handles signing and provisioning for the app and the iOS extension.
2. Use Ruby 3.2 or newer and Bundler. Run `bundle install` in the release checkout to install the pinned Fastlane version.
3. For archiving and uploading, use an [App Store Connect team API key](https://developer.apple.com/documentation/appstoreconnectapi/creating-api-keys-for-app-store-connect-api) with Developer access or higher. Keep the `.p8` file outside the checkout and set these shell variables.

```sh
export KSH_APPLE_TEAM_ID='YOUR_TEAM_ID'
export APP_STORE_CONNECT_API_KEY_KEY_ID='YOUR_KEY_ID'
export APP_STORE_CONNECT_API_KEY_ISSUER_ID='YOUR_ISSUER_ID'
export APP_STORE_CONNECT_API_KEY_KEY_FILEPATH="$HOME/path/to/AuthKey_KEY_ID.p8"
```

## Build once and upload

Use a clean checkout of the CI-validated source with its submodules initialized.
The root `VERSION` file sets the shared release version for the iOS app and AUv3, the macOS app, and the desktop plug-ins. Update and commit it when starting a new release. Keep the same `VERSION` when uploading another beta for that release.

```sh
bundle exec fastlane archive
```

Fastlane reads the build history across iOS and macOS in App Store Connect, reserves the next integer above that history and any local reservations, and archives both platforms from the same checkout with that number. For example, a release can go from `0.1.0 (3)` to `0.1.0 (4)` without editing `VERSION`. A failed archive can leave a gap in the counter. Use one release checkout for allocation and upload older builds first if you have more than one pair waiting. To archive a single platform with a fresh number, pass `target:ios` or `target:macos`.

Fastlane prints the signed package paths under `Builds/apple-release/` with the version, build number, and source commit. Use the printed paths to upload the packages.

```sh
bundle exec fastlane upload target:ios package:/path/to/KSH.ipa
bundle exec fastlane upload target:macos package:/path/to/KSH.pkg
```

The archive step runs the same bundle checks as GitHub Actions. To opt into the optional features used by local builds, set `KSH_LINKKIT_SDK_DIR` or `KSH_IOS_ICLOUD_ENABLED=ON` for iOS, or `KSH_ABLETON_LINK_SOURCE_DIR` for macOS before archiving. Use the required provisioning capabilities and review the signed archive's privacy report when including an optional SDK.

The standard iOS and Mac App Store builds include `ITSAppUsesNonExemptEncryption=false` in the standalone app's `Info.plist`. If you build with Ableton Link, review that build's encryption use and answer the export compliance questions in App Store Connect.

## Test and submit

1. After Apple processes each upload, add the build to your internal TestFlight group. On iOS, test the standalone on iPhone and iPad and load the AUv3 in a host. On the Mac, test MIDI routing and project open and save in the sandboxed standalone.
2. In the App Store Connect version for each platform, select the tested build and complete the required metadata. Use **Add for Review**, then **Submit for Review**. Choose manual release if you want to choose when the approved version goes live.

Xcode Organizer is the window for Xcode's manual archive and distribution flow. **TestFlight Internal Only** in Organizer, or via `packaging/TestFlightInternalExportOptions.plist`, is useful for internal-only builds. Those builds are ineligible for App Review. The Fastlane export above allows the same tested build to be submitted.
