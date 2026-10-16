#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then echo 'The companion currently requires macOS.' >&2; exit 1; fi
app="${PASSPORT_APP_OUTPUT:-${PWD}/build/Passport Companion.app}"
mkdir -p "${app}/Contents/MacOS" "${PWD}/build/swift-cache"
swiftc -swift-version 5 -O -module-cache-path "${PWD}/build/swift-cache" \
    companion/macos/Protocol.swift companion/macos/AudioOutput.swift \
    companion/macos/DesktopStore.swift companion/macos/DesktopApplication.swift companion/macos/main.swift \
    -o "${app}/Contents/MacOS/PassportCompanion"
install -m 0644 companion/macos/Info.plist "${app}/Contents/Info.plist"
codesign --force --sign - --identifier com.folotoy.passport.companion "${app}"
echo "${app}"
