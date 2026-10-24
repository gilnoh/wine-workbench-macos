#!/bin/bash
# Put CrossOver's original files back, undoing install.command.
#
#   bash restore.command [/path/to/CrossOver.app]     (default /Applications/CrossOver.app)
#
# Uses the backup install.command made, and only restores a file whose backup
# matches CrossOver's original checksum in MANIFEST.
set -eo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${1:-/Applications/CrossOver.app}"
LIB="$APP/Contents/SharedSupport/CrossOver/lib/wine"
BACKUP_ROOT="$HOME/Library/Application Support/wine-workbench/crossover-dropin-backup"

say()  { echo "==> $*"; }
fail() { echo; echo "STOPPED: $*"; exit 1; }
sha()  { shasum -a 256 "$1" | cut -d' ' -f1; }

[ -f "$HERE/MANIFEST" ] || fail "MANIFEST not found next to this script. Run it from the unzipped folder."
WANT_BUILD=$(sed -n 's/^crossover_build=//p' "$HERE/MANIFEST")
BACKUP="$BACKUP_ROOT/$WANT_BUILD"

[ -d "$LIB" ] || fail "CrossOver not found at $APP. If it is elsewhere, run:  bash restore.command /path/to/CrossOver.app"
BUILD=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Contents/Info.plist" 2>/dev/null || true)
[ "$BUILD" = "$WANT_BUILD" ] || fail "this package is for CrossOver $WANT_BUILD, but $APP is $BUILD. A CrossOver update already replaced the files; nothing to restore."

if pgrep -f "$APP/Contents/" >/dev/null; then
    fail "CrossOver is running. Quit every program in your bottles, then quit CrossOver (Cmd-Q), and run this again."
fi

TODO=""
while read -r kind path orig new; do
    [ "$kind" = file ] || continue
    cur=$(sha "$LIB/$path")
    if [ "$cur" = "$orig" ]; then
        say "already original: $path"
    elif [ "$cur" = "$new" ]; then
        [ -f "$BACKUP/$path" ] && [ "$(sha "$BACKUP/$path")" = "$orig" ] \
            || fail "no valid backup of $path in $BACKUP. Reinstall CrossOver $WANT_BUILD to get the original back."
        TODO="$TODO $path"
    else
        fail "$path in CrossOver is neither CrossOver's original nor ours; leaving it alone."
    fi
done < "$HERE/MANIFEST"
[ -n "$TODO" ] || { say "CrossOver already has its original files."; exit 0; }

for path in $TODO; do
    if ! cp "$BACKUP/$path" "$LIB/$path" 2>/dev/null; then
        echo
        echo "macOS did not allow changing CrossOver.app. Allow the app you ran this from"
        echo "(usually Terminal) under System Settings > Privacy & Security > App Management,"
        echo "quit and reopen it, and run this script again."
        open "x-apple.systempreferences:com.apple.preference.security?Privacy_AppBundles" 2>/dev/null || true
        fail "no permission to modify $APP"
    fi
    [ "$(sha "$LIB/$path")" = "$(sha "$BACKUP/$path")" ] || fail "$path did not verify after restoring."
    say "restored: $path"
done

echo
say "CrossOver's original files are back."
