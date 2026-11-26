#!/usr/bin/env bash
set -euo pipefail

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

expect_plist() {
    local plist=$1 key=$2 format=$3 expected=$4 actual
    actual=$(plutil -extract "$key" "$format" -o - "$plist") || fail "$plist: could not read $key ($actual)"
    [[ "$actual" == "$expected" ]] || fail "$plist: expected $key = $expected, got $actual"
}

[[ $# == 2 ]] || fail "Usage: $0 {ios|iphone|macos} <KSH.app or KSH.xcarchive>"
platform=$1
app=$2
if [[ "$app" == *.xcarchive ]]; then
    app="$app/Products/Applications/KSH.app"
fi
[[ -d "$app" ]] || fail "Missing app bundle: $app"

case "$platform" in
    ios|iphone)
        families='[1,2]'
        if [[ "$platform" == iphone ]]; then
            families='[1]'
        fi
        extension="$app/PlugIns/KSH.appex"
        [[ -x "$app/KSH" && -x "$extension/KSH" ]] || fail "Missing iOS app or embedded AUv3 executable in $app"
        expect_plist "$app/Info.plist" CFBundleIdentifier raw com.ofsound.ksh
        expect_plist "$app/Info.plist" CFBundleSupportedPlatforms.0 raw iPhoneOS
        expect_plist "$app/Info.plist" UIDeviceFamily json "$families"
        expect_plist "$app/Info.plist" MinimumOSVersion raw 15.0
        expect_plist "$extension/Info.plist" CFBundleIdentifier raw com.ofsound.ksh.kshAUv3
        expect_plist "$extension/Info.plist" NSExtension.NSExtensionPointIdentifier raw com.apple.AudioUnit-UI
        expect_plist "$extension/Info.plist" MinimumOSVersion raw 15.0
        expected_manifest=ios
        manifests=("$app/PrivacyInfo.xcprivacy" "$extension/PrivacyInfo.xcprivacy")
        ;;
    macos)
        [[ -x "$app/Contents/MacOS/KSH" ]] || fail "Missing macOS app executable in $app"
        expect_plist "$app/Contents/Info.plist" CFBundleIdentifier raw com.ofsound.ksh
        expect_plist "$app/Contents/Info.plist" CFBundleSupportedPlatforms.0 raw MacOSX
        expect_plist "$app/Contents/Info.plist" LSMinimumSystemVersion raw 12.0
        expected_manifest=macos
        manifests=("$app/Contents/Resources/PrivacyInfo.xcprivacy")
        ;;
    *) fail "Unknown platform: $platform" ;;
esac

repo_root=$(cd "$(dirname "$0")/.." && pwd)
python3 - "$repo_root/packaging/$expected_manifest/PrivacyInfo.xcprivacy" "${manifests[@]}" <<'PY'
import plistlib
import sys
from pathlib import Path

source, *bundled_manifests = map(Path, sys.argv[1:])
expected = plistlib.loads(source.read_bytes())
for manifest in bundled_manifests:
    try:
        actual = plistlib.loads(manifest.read_bytes())
    except (OSError, ValueError) as error:
        sys.exit(f"{manifest}: {error}")
    if actual != expected:
        sys.exit(f"{manifest}: privacy declarations differ from {source}")
    print(f"Verified {manifest}")
PY
