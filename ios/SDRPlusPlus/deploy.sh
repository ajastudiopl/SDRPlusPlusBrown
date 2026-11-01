#!/bin/bash
# Rebuilds the iOS libraries, packs and installs the app on the iPad, then launches it with the console.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HOME/ios-sdr-kit/b-sdrpp"
DEV_UDID=255d3776ca5b0bc76a2e17d371d3fafc8b31beef
DEV_ID=B94ADC23-1F53-557F-93C9-B3BD12CE770E
(cd "$B" && make -j8 sdrpp_core radio ios_audio_sink sdrpp_server_source websdr_view reports_monitor 2>&1 | grep -E "error|\*\*\*" || true)
"$HERE/stage.sh" "$B"
cd "$HERE"
xcodebuild -project SDRPlusPlus.xcodeproj -scheme SDRPlusPlus -configuration Debug -destination "id=$DEV_UDID" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration DEVELOPMENT_TEAM=JT7QP9G358 build 2>&1 | grep -E "error:|BUILD"
APP=$(ls -dt "$HOME"/Library/Developer/Xcode/DerivedData/SDRPlusPlus-*/Build/Products/Debug-iphoneos/*.app | head -1)
TMP=$(mktemp -d); cp -R "$APP" "$TMP/SDRpp.app"
xcrun devicectl device install app --device $DEV_ID "$TMP/SDRpp.app" 2>&1 | grep -iE "error|installed"
xcrun devicectl device process launch --console --terminate-existing --device $DEV_ID org.sdrppbrown.app
