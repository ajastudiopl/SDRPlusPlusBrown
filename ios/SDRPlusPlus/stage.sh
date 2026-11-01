#!/bin/bash
# Collects the iOS build of SDR++ (core + modules) into Stage/ for the Xcode project.
# Usage: ./stage.sh [cmake build dir, default ~/ios-sdr-kit/b-sdrpp]
set -e
B="${1:-$HOME/ios-sdr-kit/b-sdrpp}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
rm -rf "$HERE/Stage"
mkdir -p "$HERE/Stage/Frameworks"
cp "$B/core/libsdrpp_core.dylib" "$HERE/Stage/Frameworks/"
for m in radio ios_audio_sink sdrpp_server_source frequency_manager websdr_view reports_monitor; do
    f=$(find "$B" -name "$m.dylib" | head -1)
    [ -n "$f" ] && cp "$f" "$HERE/Stage/Frameworks/"
done
cp -R "$ROOT/root/res" "$HERE/Stage/res"
