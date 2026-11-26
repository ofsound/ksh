# KSH internal TestFlight

| Platform | CMake configure preset | Xcode project | Archive contents |
| --- | --- | --- | --- |
| iPhone and iPad | `ios-xcode` | `Builds-iOS/KSH.xcodeproj` | `KSH.app` with the KSH AUv3 extension |
| macOS 12 or later | `macos-testflight` | `Builds-native-macos-testflight/KSH.xcodeproj` | Sandboxed `KSH.app` only |

Both apps use `com.ofsound.ksh`. The iOS extension uses `com.ofsound.ksh.kshAUv3`.
The desktop AU, VST3, CLAP, and AUv3 builds are available through their existing build configurations.

1. In App Store Connect, create or open the KSH record for `com.ofsound.ksh`. Add the macOS platform to that record for the native Mac build.
2. Run `cmake --preset ios-xcode` or `cmake --preset macos-testflight`. Open the corresponding project and choose the paid Apple Developer team under Signing & Capabilities. On iOS, set the same team on `KSH_Standalone` and `KSH_AUv3`. To keep signing settings through CMake regeneration, pass `-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<team-id>` when configuring.
3. In Xcode, select `KSH_Standalone` and the generic device destination for the platform, then choose **Product → Archive**. The iOS preset targets both iPhone and iPad. Add the optional Ableton LinkKit SDK once the app's provisioning profile supports the multicast entitlement.
4. Validate the signed archive with the matching command below. The check reads the built app and, on iOS, its embedded AUv3 and privacy manifests.
5. In Organizer, choose **Distribute App → TestFlight Internal Only**. Apple restricts builds uploaded this way to internal tester groups. Add the processed build to an internal group in App Store Connect.

```sh
bash scripts/verify-apple-bundle.sh ios /path/to/KSH.xcarchive
bash scripts/verify-apple-bundle.sh iphone /path/to/iphone-only/KSH.xcarchive
bash scripts/verify-apple-bundle.sh macos /path/to/KSH-macOS.xcarchive
```

The **Apple archive validation** GitHub Actions workflow runs the same checks on unsigned archives. The checked-in manifests cover KSH's default build. If the archive includes the optional Ableton SDK, review that exact signed archive's privacy report in Xcode Organizer before uploading it.

For terminal uploads, use `xcodebuild -exportArchive` with the signed archive and `-exportOptionsPlist packaging/TestFlightInternalExportOptions.plist`. Xcode can manage build numbers at upload time. Fastlane is optional.

For the Mac build, verify MIDI routing, project open and save, and Ableton Link when included. App Sandbox changes the app's access to files outside its container, including projects saved by the regular desktop build.
