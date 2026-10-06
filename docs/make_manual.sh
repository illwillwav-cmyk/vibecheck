#!/bin/bash
# Turns docs/manual.html into docs/VibeCheck-Manual.pdf, the copy that ships inside the installers.
# Run this after editing the manual, and commit the PDF: the release build only copies it.
#
#   docs/make_manual.sh
#
# Needs Google Chrome (or Chromium / Edge), which does the page layout.
set -euo pipefail
cd "$(dirname "$0")"

for candidate in \
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  "/Applications/Chromium.app/Contents/MacOS/Chromium" \
  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
  "$(command -v google-chrome || true)" "$(command -v chromium || true)"; do
  [ -n "$candidate" ] && [ -x "$candidate" ] && { CHROME="$candidate"; break; }
done
[ -n "${CHROME:-}" ] || { echo "Chrome was not found. Install it, or print docs/manual.html to PDF by hand." >&2; exit 1; }

OUT="$PWD/VibeCheck-Manual.pdf"
rm -f "$OUT"
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer \
  --user-data-dir="$(mktemp -d)" \
  --print-to-pdf="$OUT" "file://$PWD/manual.html" >/dev/null 2>&1 &
PID=$!

# Headless Chrome sometimes lingers after writing the file, so wait for the file rather than for
# Chrome, then stop that one process.
for _ in $(seq 1 120); do
  [ -s "$OUT" ] && sleep 1 && break
  kill -0 "$PID" 2>/dev/null || break
  sleep 0.5
done
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

[ -s "$OUT" ] || { echo "No PDF was written." >&2; exit 1; }
echo "Wrote $OUT ($(du -h "$OUT" | cut -f1 | tr -d ' '))"
