#!/bin/bash
# Shared helpers for the games tree. Sourced by mkprefix / gamerun / backend-install.

set -euo pipefail

GAMES_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export GAMES_ROOT

RUNTIMES_DIR="$GAMES_ROOT/runtimes"
BACKENDS_DIR="$GAMES_ROOT/backends"
PREFIXES_DIR="$GAMES_ROOT/prefixes"
PROFILES_DIR="$GAMES_ROOT/profiles"
LIBRARY_DIR="$GAMES_ROOT/library"

# Default runtime, overridable per profile.
DEFAULT_RUNTIME="wine-11.14-x86_64"

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# Resolve a runtime name to its bin dir, verifying it exists.
runtime_bin() {
    local rt="$1"
    [ -x "$RUNTIMES_DIR/$rt/bin/wine" ] \
        || die "runtime '$rt' not found or not built (expected $RUNTIMES_DIR/$rt/bin/wine)"
    echo "$RUNTIMES_DIR/$rt/bin"
}

# x86_64 runtimes must be invoked through Rosetta from an arm64 shell.
# Emits the arch prefix needed for a given runtime, or nothing if native.
arch_prefix() {
    case "$1" in
        *-x86_64) echo "arch -x86_64" ;;
        *)        echo "" ;;
    esac
}

load_profile() {
    local name="$1"
    local f="$PROFILES_DIR/$name.env"
    [ -f "$f" ] || die "no profile '$name' (expected $f)"
    # Profile defaults; the profile may override any of these.
    RUNTIME="$DEFAULT_RUNTIME"
    BACKEND="none"
    PREFIX="$name"
    EXE=""
    ARGS=""
    WORKDIR=""
    # shellcheck disable=SC1090
    source "$f"
    PREFIX_PATH="$PREFIXES_DIR/$PREFIX"
}
