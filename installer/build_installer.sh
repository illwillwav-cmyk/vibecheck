#!/bin/bash
# Builds VibeCheck and packages it two ways:
#
#   dist/VibeCheck-<version>.pkg   guided installer (welcome, read me, optional command line tool)
#   dist/VibeCheck-<version>.dmg   drag-to-Applications disk image with an uninstaller beside it
#
# With no options it makes an ad-hoc signed build that runs on the machine that built it. To make
# something other people can open without a warning, give it your Apple developer identities:
#
#   SIGN_APP="Developer ID Application: Your Name (TEAMID)" \
#   SIGN_PKG="Developer ID Installer: Your Name (TEAMID)" \
#   NOTARY_PROFILE="my-notary-profile" \
#   installer/build_installer.sh
#
# NOTARY_PROFILE is a keychain profile made once with:
#   xcrun notarytool store-credentials my-notary-profile --apple-id you@example.com --team-id TEAMID
#
# Other switches (environment variables):
#   UNIVERSAL=1      build for Apple silicon and Intel
#   CONFIG=Release   CMake build type (default Release)
#   BUILD_DIR=...    reuse an existing build tree instead of build-release
#   SKIP_BUILD=1     package what is already built in BUILD_DIR
#   CMAKE_EXTRA_ARGS extra options for the configure step, e.g. "-DVIBECHECK_UPDATE_URL=https://..."
#   DMG_LAYOUT=0     skip arranging the disk image window with Finder
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONFIG="${CONFIG:-Release}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
DIST="$ROOT/dist"
STAGE="$DIST/stage"
IDENTIFIER="com.williamwright.vibecheck"
VERSION="$(perl -ne 'print $1 if /project\(VibeCheck VERSION (\d+\.\d+\.\d+)/' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }

SIGN_APP="${SIGN_APP:-}"
SIGN_PKG="${SIGN_PKG:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

command -v pkgbuild >/dev/null && command -v productbuild >/dev/null && command -v hdiutil >/dev/null \
  || die "pkgbuild, productbuild and hdiutil are needed. They come with macOS and the Xcode command line tools."

# --- 1. Build --------------------------------------------------------------------------------------
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  say "Building VibeCheck $VERSION ($CONFIG)"
  ARCHS="arm64"
  [ "${UNIVERSAL:-0}" = "1" ] && ARCHS="arm64;x86_64"
  # CMAKE_EXTRA_ARGS lets a release build pass extra -D options, such as the update address.
  # shellcheck disable=SC2086
  cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_OSX_ARCHITECTURES="$ARCHS" ${CMAKE_EXTRA_ARGS:-}
  cmake --build "$BUILD_DIR" --config "$CONFIG"
fi

APP_SRC="$(find "$BUILD_DIR/VibeCheck_artefacts" -maxdepth 3 -name VibeCheck.app -type d | head -1)"
[ -d "$APP_SRC" ] || die "No VibeCheck.app under $BUILD_DIR. Build it first, or drop SKIP_BUILD."

ARCH_LIST="$(lipo -archs "$APP_SRC/Contents/MacOS/VibeCheck")"
HOST_ARCHS="$(echo "$ARCH_LIST" | tr ' ' ',')"
echo "App: $APP_SRC ($ARCH_LIST)"

# --- 2. Stage and sign -----------------------------------------------------------------------------
say "Staging"
rm -rf "$DIST"
mkdir -p "$STAGE/app/Applications" "$STAGE/cli/usr/local/bin" "$STAGE/pkgs" "$STAGE/resources"
ditto "$APP_SRC" "$STAGE/app/Applications/VibeCheck.app"
APP="$STAGE/app/Applications/VibeCheck.app"

# Keep only what the app needs.
find "$APP" -name ".DS_Store" -delete

if [ -n "$SIGN_APP" ]; then
  say "Signing the app with: $SIGN_APP"
  codesign --force --deep --options runtime --timestamp \
           --entitlements "$ROOT/installer/VibeCheck.entitlements" --sign "$SIGN_APP" "$APP"
else
  say "Signing the app ad hoc (no SIGN_APP given)"
  warn "An ad hoc build opens on this Mac, but other Macs will show a Gatekeeper warning. See the top of this script."
  codesign --force --deep --sign - --entitlements "$ROOT/installer/VibeCheck.entitlements" "$APP"
fi
codesign --verify --deep --strict "$APP" && echo "Signature verified."

# --- 3. Installer package --------------------------------------------------------------------------
say "Building the installer package"

# The app must always land in /Applications, so switch off relocation: otherwise the installer
# would update a copy of VibeCheck found anywhere else on the disk, such as a build folder.
COMPONENT_PLIST="$STAGE/component.plist"
pkgbuild --analyze --root "$STAGE/app" "$COMPONENT_PLIST" >/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST"

pkgbuild --root "$STAGE/app" \
         --component-plist "$COMPONENT_PLIST" \
         --identifier "$IDENTIFIER.app" \
         --version "$VERSION" \
         --install-location / \
         --scripts "$ROOT/installer/scripts" \
         "$STAGE/pkgs/VibeCheck-app.pkg" >/dev/null

# Optional command line tool: a link to the binary inside the app, so it always matches the app.
ln -s "/Applications/VibeCheck.app/Contents/MacOS/VibeCheck" "$STAGE/cli/usr/local/bin/vibecheck"
pkgbuild --root "$STAGE/cli" \
         --identifier "$IDENTIFIER.cli" \
         --version "$VERSION" \
         --install-location / \
         "$STAGE/pkgs/VibeCheck-cli.pkg" >/dev/null

cp "$ROOT"/installer/resources/*.html "$STAGE/resources/"
sips -Z 380 "$ROOT/assets/icon/icon_1024.png" --out "$STAGE/resources/background.png" >/dev/null

cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>VibeCheck</title>
    <organization>com.williamwright</organization>
    <welcome file="welcome.html" mime-type="text/html"/>
    <readme file="readme.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <background file="background.png" alignment="bottomleft" scaling="proportional" mime-type="image/png"/>
    <background-darkAqua file="background.png" alignment="bottomleft" scaling="proportional" mime-type="image/png"/>
    <options customize="allow" require-scripts="false" hostArchitectures="$HOST_ARCHS"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="app"/>
        <line choice="cli"/>
    </choices-outline>
    <choice id="app" title="VibeCheck" description="The application, installed in Applications." start_selected="true" enabled="false">
        <pkg-ref id="$IDENTIFIER.app"/>
    </choice>
    <choice id="cli" title="Command line tool" description="Adds a vibecheck command to /usr/local/bin for scripts and CI, for example vibecheck --selftest or vibecheck --scan." start_selected="false">
        <pkg-ref id="$IDENTIFIER.cli"/>
    </choice>
    <pkg-ref id="$IDENTIFIER.app" version="$VERSION" onConclusion="none">VibeCheck-app.pkg</pkg-ref>
    <pkg-ref id="$IDENTIFIER.cli" version="$VERSION" onConclusion="none">VibeCheck-cli.pkg</pkg-ref>
</installer-gui-script>
XML

PKG_OUT="$DIST/VibeCheck-$VERSION.pkg"
PRODUCT_ARGS=(--distribution "$STAGE/distribution.xml" --package-path "$STAGE/pkgs" --resources "$STAGE/resources")
if [ -n "$SIGN_PKG" ]; then
  PRODUCT_ARGS+=(--sign "$SIGN_PKG" --timestamp)
else
  warn "The package is unsigned (no SIGN_PKG given). macOS will ask the user to allow it in Privacy and Security."
fi
productbuild "${PRODUCT_ARGS[@]}" "$PKG_OUT" >/dev/null
echo "Wrote $PKG_OUT"

# --- 4. Disk image ---------------------------------------------------------------------------------
say "Building the disk image"
DMG_ROOT="$STAGE/dmg"
VOLNAME="VibeCheck $VERSION"
mkdir -p "$DMG_ROOT/.background"
ditto "$APP" "$DMG_ROOT/VibeCheck.app"
ln -s /Applications "$DMG_ROOT/Applications"
cp "$ROOT/assets/icon/dmg_background.tiff" "$DMG_ROOT/.background/background.tiff"
cp "$ROOT/installer/resources/READ ME FIRST.txt" "$DMG_ROOT/READ ME FIRST.txt"
# The manual is inside the app too (the Manual button opens it); this copy is for reading before installing.
[ -f "$ROOT/docs/VibeCheck-Manual.pdf" ] && cp "$ROOT/docs/VibeCheck-Manual.pdf" "$DMG_ROOT/VibeCheck Manual.pdf"

# An uninstaller you can double-click, that reads the same script as the repository.
{
  echo '#!/bin/bash'
  echo 'cd "$(dirname "$0")" 2>/dev/null'
  sed '1d' "$ROOT/installer/uninstall.sh"
  echo 'echo; read -r -p "Press Return to close." _'
} > "$DMG_ROOT/Uninstall VibeCheck.command"
chmod +x "$DMG_ROOT/Uninstall VibeCheck.command"

RW_DMG="$STAGE/rw.dmg"
DMG_OUT="$DIST/VibeCheck-$VERSION.dmg"
hdiutil create -srcfolder "$DMG_ROOT" -volname "$VOLNAME" -fs HFS+ -format UDRW -ov "$RW_DMG" >/dev/null

if [ "${DMG_LAYOUT:-1}" = "1" ]; then
  MOUNT="/Volumes/$VOLNAME"
  hdiutil detach "$MOUNT" -quiet 2>/dev/null || true
  hdiutil attach "$RW_DMG" -noverify -noautoopen -mountpoint "$MOUNT" >/dev/null

  # Arrange the window with Finder. This needs permission to automate Finder and can be refused
  # or time out, in which case the image still works, just with icons in default places.
  if perl -e 'alarm 45; exec @ARGV' osascript >/dev/null 2>&1 <<APPLESCRIPT
tell application "Finder"
    tell disk "$VOLNAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 860, 520}
        set theOptions to the icon view options of container window
        set arrangement of theOptions to not arranged
        set icon size of theOptions to 112
        set text size of theOptions to 13
        set background picture of theOptions to file ".background:background.tiff"
        set position of item "VibeCheck.app" of container window to {170, 210}
        set position of item "Applications" of container window to {490, 210}
        set position of item "Uninstall VibeCheck.command" of container window to {250, 340}
        set position of item "READ ME FIRST.txt" of container window to {410, 340}
        try
            set position of item "VibeCheck Manual.pdf" of container window to {330, 340}
        end try
        close
        open
        update without registering applications
        delay 2
        close
    end tell
end tell
APPLESCRIPT
  then
    sync; sleep 1
    echo "Window layout applied."
  else
    warn "Finder did not arrange the disk image window. The image is fine; the icons just sit in default places."
  fi

  hdiutil detach "$MOUNT" -quiet || hdiutil detach "$MOUNT" -force -quiet
fi

hdiutil convert "$RW_DMG" -format UDZO -imagekey zlib-level=9 -ov -o "$DMG_OUT" >/dev/null
[ -n "$SIGN_APP" ] && codesign --force --sign "$SIGN_APP" --timestamp "$DMG_OUT"
echo "Wrote $DMG_OUT"

# --- 5. Notarise -----------------------------------------------------------------------------------
if [ -n "$NOTARY_PROFILE" ]; then
  [ -n "$SIGN_APP" ] && [ -n "$SIGN_PKG" ] || die "Notarising needs both SIGN_APP and SIGN_PKG."
  for file in "$PKG_OUT" "$DMG_OUT"; do
    say "Notarising $(basename "$file")"
    xcrun notarytool submit "$file" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$file"
  done
fi

# --- 6. Checksums and clean up ---------------------------------------------------------------------
rm -rf "$STAGE"
( cd "$DIST" && shasum -a 256 "VibeCheck-$VERSION.pkg" "VibeCheck-$VERSION.dmg" > "VibeCheck-$VERSION.sha256" )

say "Done"
ls -lh "$DIST"
