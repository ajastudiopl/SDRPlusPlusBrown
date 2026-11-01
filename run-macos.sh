#!/bin/bash
# Runs the newest version of the app on this Mac: rebuilds only what changed (nothing to do when the code is
# the same), refreshes the bundle in /Applications and starts it.
#   ./run-macos.sh            build if needed, install, start the app
#   ./run-macos.sh --server   same, but also restart the background server service so it gets the new code
set -e
cd "$(dirname "$0")"
ARCH=$(uname -m)
BUILD="build-macos-$ARCH"
APP="SDRpp_by_AJA.app"

cmake --build "$BUILD" -j "$(sysctl -n hw.ncpu)" 2>&1 | grep -E "error:|\*\*\*" || true
bash make_macos_bundle.sh "$BUILD" "$APP" > /dev/null 2>&1
rsync -a --delete "$APP/" "/Applications/SDR++ by AJA.app/"

if [ "$1" = "--server" ]; then
    PLIST="$HOME/Library/LaunchAgents/pl.aja.sdrpp-server.plist"
    launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
    sleep 2
    rsync -a --delete "$APP/" "$HOME/Applications/SDR++ by AJA.app/"
    launchctl bootstrap "gui/$(id -u)" "$PLIST"
fi

open -n "/Applications/SDR++ by AJA.app"
