#!/bin/bash
# Draws the app icon and the disk-image artwork with the app itself, then builds VibeCheck.icns.
#
# The artwork is drawn in code from assets/brand, so it cannot drift from the brand files, and no
# image tooling beyond what ships with macOS (sips, iconutil, tiffutil) is needed. Run this after
# changing src/ui/AppIcon.cpp; the results in assets/icon are committed so a normal build does not
# need to.
#
#   installer/make_assets.sh [path/to/VibeCheck.app]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:-$ROOT/build/VibeCheck_artefacts/RelWithDebInfo/VibeCheck.app}"
BIN="$APP/Contents/MacOS/VibeCheck"
OUT="$ROOT/assets/icon"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -x "$BIN" ] || { echo "No built app at $APP. Build first: cmake --build build" >&2; exit 1; }

echo "Drawing artwork..."
"$BIN" --render-assets="$WORK" &
PID=$!
for _ in $(seq 1 60); do
  [ -f "$WORK/dmg_background@2x.png" ] && break
  sleep 0.5
done
sleep 0.5
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
[ -f "$WORK/icon_1024.png" ] || { echo "The app did not write its artwork." >&2; exit 1; }

echo "Building VibeCheck.icns..."
SET="$WORK/VibeCheck.iconset"
mkdir -p "$SET"
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" "$WORK/icon_1024.png" --out "$SET/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z "$double" "$double" "$WORK/icon_1024.png" --out "$SET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT/VibeCheck.icns"

echo "Building the disk image background..."
tiffutil -cathidpicheck "$WORK/dmg_background.png" "$WORK/dmg_background@2x.png" -out "$OUT/dmg_background.tiff" >/dev/null 2>&1 \
  || cp "$WORK/dmg_background@2x.png" "$OUT/dmg_background.tiff"
cp "$WORK/icon_1024.png" "$OUT/icon_1024.png"

echo "Wrote $OUT/VibeCheck.icns, dmg_background.tiff, icon_1024.png"
