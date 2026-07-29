#!/bin/bash
# Shared helpers for the games tree. Sourced by mkprefix / gamerun / backend-install.

# No `set -u`: macOS ships bash 3.2, where ${#arr[@]} on a declared-but-empty
# array trips "unbound variable". The env arrays below are routinely empty.
set -eo pipefail

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

# Wine dlopen()s host libraries by bare name (libfreetype.6.dylib,
# libvulkan.1.dylib, ...). Modern macOS dropped /usr/local/lib from dyld's
# default search path, so without this Wine silently loses fonts and Vulkan.
# x86_64 runtimes need the x86_64 Homebrew at /usr/local; an arm64 runtime
# would need /opt/homebrew instead.
runtime_dyld_path() {
    case "$1" in
        *-x86_64) echo "/usr/local/lib" ;;
        *)        echo "/opt/homebrew/lib" ;;
    esac
}

# There is deliberately no runtime_vk_icd() helper any more, and nothing here
# sets VK_ICD_FILENAMES.
#
# The earlier belief -- that Homebrew installs MoltenVK's manifest somewhere the
# loader cannot autodiscover -- was wrong. The loader searches
# /usr/local/etc/vulkan/icd.d and finds it; `vulkaninfo` with no environment set
# enumerates the GPU correctly.
#
# Pointing VK_ICD_FILENAMES at MoltenVK's own manifest was actively harmful,
# because that manifest carries is_portability_driver:true and the loader then
# hides the driver from applications that do not request
# VK_KHR_portability_enumeration. Steam does not, so it got
# VK_ERROR_INCOMPATIBLE_DRIVER and concluded the machine had no GPU.
#
# bin/provision-host strips that flag from the installed manifest; see
# docs/RUNTIME.md.

# NOTE: do NOT wrap wine in `arch -x86_64`.
#
# It is unnecessary -- a thin x86_64 binary is auto-translated by Rosetta when
# exec'd directly from an arm64 process -- and it is actively harmful: /usr/bin/arch
# is SIP-protected, so dyld strips every DYLD_* variable when spawning through it.
# That silently breaks the runtime_dyld_path above, costing fonts and Vulkan.
#
# Verified 2026-07-29: with the arch wrapper, wine emits 18 FreeType load errors
# per launch; without it, zero. `arch -x86_64` is still required for *building*
# (see bin/env-build-x86_64.sh), just never for running.

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
    WINEDLLOVERRIDES=""
    PROFILE_ENV=()
    # shellcheck disable=SC1090
    source "$f"
    PREFIX_PATH="$PREFIXES_DIR/$PREFIX"
}
