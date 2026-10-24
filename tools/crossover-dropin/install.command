#!/bin/bash
# Install the wine-workbench drop-in files into CrossOver.
#
#   bash install.command [/path/to/CrossOver.app]     (default /Applications/CrossOver.app)
#
# Replaces only the files listed in MANIFEST, and only if each one is exactly
# CrossOver's original (checked by sha256). The originals are copied to a backup
# folder first; restore.command puts them back.
set -eo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${1:-/Applications/CrossOver.app}"
LIB="$APP/Contents/SharedSupport/CrossOver/lib/wine"
BACKUP_ROOT="$HOME/Library/Application Support/wine-workbench/crossover-dropin-backup"

say()  { echo "==> $*"; }
fail() { echo; echo "STOPPED: $*"; echo "Nothing in CrossOver was changed."; exit 1; }
sha()  { shasum -a 256 "$1" | cut -d' ' -f1; }

[ -f "$HERE/MANIFEST" ] || fail "MANIFEST not found next to this script. Run it from the unzipped folder."
NAME=$(sed -n 's/^name=//p' "$HERE/MANIFEST")
WANT_BUILD=$(sed -n 's/^crossover_build=//p' "$HERE/MANIFEST")
say "$NAME"

[ -d "$LIB" ] || fail "CrossOver not found at $APP. If it is elsewhere, run:  bash install.command /path/to/CrossOver.app"
BUILD=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Contents/Info.plist" 2>/dev/null || true)
[ "$BUILD" = "$WANT_BUILD" ] || fail "these files are for CrossOver $WANT_BUILD, but $APP is $BUILD. Installing them into another version would break it."

if pgrep -f "$APP/Contents/" >/dev/null; then
    fail "CrossOver is running. Quit every program in your bottles, then quit CrossOver (Cmd-Q), and run this again."
fi

# Check every file before changing any.
TODO=""
while read -r kind path orig new; do
    [ "$kind" = file ] || continue
    [ -f "$HERE/files/$path" ] || fail "missing from this package: files/$path"
    [ "$(sha "$HERE/files/$path")" = "$new" ] || fail "files/$path in this package is damaged (checksum mismatch). Download the package again."
    cur=$(sha "$LIB/$path")
    if [ "$cur" = "$new" ]; then
        say "already installed: $path"
    elif [ "$cur" = "$orig" ]; then
        TODO="$TODO $path"
    else
        fail "$path in CrossOver is neither CrossOver's original nor ours. Was CrossOver modified or updated? Reinstall CrossOver $WANT_BUILD first."
    fi
done < "$HERE/MANIFEST"
[ -n "$TODO" ] || { say "everything is already installed."; exit 0; }

# Downloaded files carry a quarantine flag that can stop CrossOver loading them.
xattr -dr com.apple.quarantine "$HERE/files" 2>/dev/null || true

BACKUP="$BACKUP_ROOT/$WANT_BUILD"
for path in $TODO; do
    mkdir -p "$(dirname "$BACKUP/$path")"
    cp -p "$LIB/$path" "$BACKUP/$path"
done
say "originals backed up to: $BACKUP"

DONE=""
for path in $TODO; do
    if ! cp "$HERE/files/$path" "$LIB/$path" 2>/dev/null; then
        for undo in $DONE; do cp "$BACKUP/$undo" "$LIB/$undo" 2>/dev/null || true; done
        echo
        echo "macOS did not allow changing CrossOver.app (\"Operation not permitted\")."
        echo "Allow it once: System Settings > Privacy & Security > App Management, and switch on"
        echo "the app you ran this script from (usually Terminal). Then quit and reopen that app,"
        echo "and run this script again."
        open "x-apple.systempreferences:com.apple.preference.security?Privacy_AppBundles" 2>/dev/null || true
        fail "no permission to modify $APP"
    fi
    DONE="$DONE $path"
done

while read -r kind path orig new; do
    [ "$kind" = file ] || continue
    [ "$(sha "$LIB/$path")" = "$new" ] || echo "WARNING: $path did not verify after copying. Run restore.command."
done < "$HERE/MANIFEST"

echo
say "installed. Start CrossOver and play."
say "to undo: run restore.command from this folder."
