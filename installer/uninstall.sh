#!/bin/bash
# Removes VibeCheck: the app, the command line tool, the installer receipts, and (with --purge)
# the settings and plugin list. Asks before it does anything.
#
#   ./uninstall.sh            remove the app and tool
#   ./uninstall.sh --purge    also remove settings and the cached plugin list
set -u

APP="/Applications/VibeCheck.app"
TOOL="/usr/local/bin/vibecheck"
SETTINGS="$HOME/Library/Application Support/VibeCheck"
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

echo "This will remove:"
[ -e "$APP" ]  && echo "  $APP"
[ -L "$TOOL" ] && echo "  $TOOL"
echo "  the installer receipts for com.williamwright.vibecheck"
[ "$PURGE" = 1 ] && [ -e "$SETTINGS" ] && echo "  $SETTINGS"
echo
read -r -p "Continue? [y/N] " answer
case "$answer" in y|Y|yes|YES) ;; *) echo "Nothing changed."; exit 0 ;; esac

# Quit it first, so files are not in use.
osascript -e 'tell application "VibeCheck" to quit' >/dev/null 2>&1 || true
sleep 1

run() { "$@" 2>/dev/null || sudo "$@"; }

[ -e "$APP" ]  && run rm -rf "$APP"
[ -L "$TOOL" ] && run rm -f "$TOOL"

for id in $(pkgutil --pkgs 2>/dev/null | grep '^com\.williamwright\.vibecheck'); do
  sudo pkgutil --forget "$id" >/dev/null 2>&1
done

[ "$PURGE" = 1 ] && rm -rf "$SETTINGS"

echo "VibeCheck has been removed."
