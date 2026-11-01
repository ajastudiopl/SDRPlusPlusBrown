#!/bin/bash
# Builds the iPad app (only what changed), installs it on the connected iPad and starts it.
# The iPad has to be connected by cable (or Wi-Fi pairing) and unlocked, otherwise starting fails; the app is installed anyway.
#   ./run-ipad.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="$HOME/ios-sdr-kit"
B="$SDK/b-sdrpp"
DEV_UDID=255d3776ca5b0bc76a2e17d371d3fafc8b31beef
DEV_ID=B94ADC23-1F53-557F-93C9-B3BD12CE770E

# new source files are picked up by re-running cmake
(cd "$B" && cmake -DOPT_BUILD_FREQUENCY_MANAGER=ON . > /dev/null 2>&1)
(cd "$B" && make -j"$(sysctl -n hw.ncpu)" sdrpp_core radio ios_audio_sink sdrpp_server_source frequency_manager websdr_view reports_monitor 2>&1 | grep -E "error:|\*\*\*" || true)
"$HERE/ios/SDRPlusPlus/stage.sh" "$B"
cd "$HERE/ios/SDRPlusPlus"
BUILD_LOG=$(mktemp)
xcodebuild -project SDRPlusPlus.xcodeproj -scheme SDRPlusPlus -configuration Debug -destination "id=$DEV_UDID" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration DEVELOPMENT_TEAM=JT7QP9G358 build > "$BUILD_LOG" 2>&1 || true
grep -E "error:|BUILD" "$BUILD_LOG" || true
if ! grep -q "BUILD SUCCEEDED" "$BUILD_LOG"; then
    echo "The build failed, nothing was installed."
    exit 1
fi
APP=$(ls -dt "$HOME"/Library/Developer/Xcode/DerivedData/SDRPlusPlus-*/Build/Products/Debug-iphoneos/*.app | head -1)
TMP=$(mktemp -d); cp -R "$APP" "$TMP/SDRpp.app"
xcrun devicectl device install app --device "$DEV_ID" "$TMP/SDRpp.app" 2>&1 | grep -iE "error|installed"
xcrun devicectl device process launch --terminate-existing --device "$DEV_ID" org.sdrppbrown.app 2>&1 | grep -iE "error|launched" || true
