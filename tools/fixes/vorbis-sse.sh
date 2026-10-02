#!/bin/bash
# Replace a game's x87-only 32-bit vorbis.dll with an SSE build of libvorbis.
#
#   tools/fixes/vorbis-sse.sh build                 # -> build/vorbis-sse/vorbis.dll
#   tools/fixes/vorbis-sse.sh install <game dir>    # backs up vorbis.dll -> vorbis.dll.orig
#   tools/fixes/vorbis-sse.sh restore <game dir>    # puts the original back
#   tools/fixes/vorbis-sse.sh status  <game dir>
#
# WHY (measured 2026-10-02, Strategic Command WWII)
#
# The game ships libVorbis "20050304" (1.1.x) built with x87 float math only:
# 3013 x87 instructions, 0 SSE. Rosetta 2 emulates x87 badly, and the game decodes
# whole music tracks on its main thread, so every menu/map transition and Quit
# froze the UI for tens of seconds. tools/probes/oggprobe, the game's own
# vorbisfile.dll, Music/loop3.ogg (256 s):
#
#   game vorbis.dll, our Wine        51.45 s  (5.0x realtime)
#   game vorbis.dll, CrossOver 26    51.66 s  -- identical: not our runtime
#   this build, our Wine              3.29 s  (78x realtime)
#
# Output is the same audio: identical sample count, max difference 1 LSB in 0.05%
# of samples (float rounding), ~101 dB below the signal.
#
# libvorbis's public ABI has been stable since 1.0, which is what makes 1.3.7 a
# drop-in for 1.1.x: vorbisfile.dll, the only importer, uses 21 public vorbis_*
# functions. libogg is linked statically, and the CRT is msvcrt.dll, matching the
# game's own DLLs. (-mcrtdll=msvcrt is NOT enough with this mingw: its
# libmsvcrt.a is an alias for UCRT. msvcrt-os is the real msvcrt.dll.)
#
# Wine cannot override a DLL that sits in the application's own directory, so the
# fix is a file swap. Steam's "Verify integrity" or a game update restores the
# original -- re-run `install` after either.

set -euo pipefail
GAMES_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

VORBIS=libvorbis-1.3.7; VORBIS_SHA=b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b
OGG=libogg-1.3.5;       OGG_SHA=c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705
OUT="$GAMES_ROOT/build/vorbis-sse"
DLL="$OUT/vorbis.dll"
CC="${CC:-/opt/homebrew/bin/i686-w64-mingw32-gcc}"

fetch() {  # name sha url
    local f="$OUT/$1.tar.xz"
    [ -f "$f" ] || curl -fsSL -o "$f" "$3" || die "download failed: $3"
    echo "$2  $f" | shasum -a 256 -c - >/dev/null || die "$1: sha256 mismatch"
    [ -d "$OUT/$1" ] || tar -C "$OUT" -xf "$f"
}

do_build() {
    mkdir -p "$OUT"
    fetch "$VORBIS" "$VORBIS_SHA" "https://downloads.xiph.org/releases/vorbis/$VORBIS.tar.xz"
    fetch "$OGG"    "$OGG_SHA"    "https://downloads.xiph.org/releases/ogg/$OGG.tar.xz"
    sed -e 's/@INCLUDE_INTTYPES_H@/1/;s/@INCLUDE_STDINT_H@/1/;s/@INCLUDE_SYS_TYPES_H@/1/' \
        -e 's/@SIZE16@/int16_t/;s/@USIZE16@/uint16_t/;s/@SIZE32@/int32_t/;s/@USIZE32@/uint32_t/' \
        -e 's/@SIZE64@/int64_t/;s/@USIZE64@/uint64_t/' \
        "$OUT/$OGG/include/ogg/config_types.h.in" > "$OUT/$OGG/include/ogg/config_types.h"
    # Exports: libvorbis's own Windows lists (win32/vorbis.def + vorbisenc.def),
    # which together match the original's 45. NOT the public headers: those miss
    # vorbis_window, which vorbisfile.dll imports -- a header-derived list built
    # a DLL the game could not load (caught by the import check in `install`).
    {
        echo "LIBRARY vorbis.dll"; echo "EXPORTS"
        cat "$OUT/$VORBIS/win32/vorbis.def" "$OUT/$VORBIS/win32/vorbisenc.def" \
            | grep -v -E '^\s*(;|LIBRARY|EXPORTS|$)' | awk '{print $1}' | sort -u
    } > "$OUT/vorbis.def"
    local srcs=()
    for f in "$OUT/$VORBIS"/lib/*.c; do
        case "${f##*/}" in barkmel.c|tone.c|psytune.c) continue ;; esac
        srcs+=("$f")
    done
    info "building $VORBIS + $OGG (i686, SSE math, msvcrt.dll)"
    env -u CPATH -u LIBRARY_PATH "$CC" -mcrtdll=msvcrt-os -O2 -msse2 -mfpmath=sse -DNDEBUG \
        -I"$OUT/$OGG/include" -I"$OUT/$VORBIS/include" -I"$OUT/$VORBIS/lib" \
        -shared -static-libgcc -o "$DLL" "${srcs[@]}" \
        "$OUT/$OGG/src/bitwise.c" "$OUT/$OGG/src/framing.c" "$OUT/vorbis.def"

    # Assert what the fix depends on: SSE float code, and the game's CRT.
    local od=/opt/homebrew/opt/llvm/bin/llvm-objdump sse
    sse=$("$od" -d "$DLL" | grep -cE '\s(mulss|mulps|addss|addps)\s' || true)
    [ "$sse" -gt 100 ] || die "built vorbis.dll has no SSE float math ($sse)"
    "$od" -p "$DLL" | grep -q 'DLL Name: msvcrt.dll' || die "built vorbis.dll does not link msvcrt.dll"
    "$od" -p "$DLL" | grep -q 'api-ms-win-crt' && die "built vorbis.dll links UCRT, not msvcrt.dll"
    info "built $DLL ($sse SSE float instructions, msvcrt.dll)"
}

need_dir() { [ -n "${1:-}" ] && [ -f "$1/vorbis.dll" ] || die "no vorbis.dll in '${1:-}'"; }

# Every vorbis_* function the game's own importers use must be exported by ours,
# or the game fails to load the DLL at all.
check_imports() {
    local od=/opt/homebrew/opt/llvm/bin/llvm-objdump f missing
    local have; have=$("$od" -p "$DLL" | sed -n '/Export Table/,/^$/p' | grep -oE '\b_?[a-z_]*vorbis_[a-z_]+|\b_[a-z_]+_P\b' | sort -u || true)
    [ -n "$have" ] || die "$DLL exports no vorbis_* functions"
    for f in "$1"/*.dll "$1"/*.exe; do
        [ -f "$f" ] && [ "${f##*/}" != vorbis.dll ] || continue
        "$od" -p "$f" 2>/dev/null | grep -qi 'DLL Name: vorbis.dll' || continue
        missing=$(comm -23 <("$od" -p "$f" | grep -oE '\bvorbis_[a-z_]+' | sort -u || true) <(echo "$have") | tr '\n' ' ')
        [ -z "$missing" ] || die "${f##*/} imports functions our vorbis.dll does not export: $missing"
        info "  ${f##*/}: every vorbis.dll import is exported"
    done
}
is_ours()  { cmp -s "$1/vorbis.dll" "$DLL"; }

case "${1:-}" in
    build) do_build ;;
    install)
        need_dir "${2:-}"; [ -f "$DLL" ] || do_build
        check_imports "$2"
        if is_ours "$2"; then info "already installed"; exit 0; fi
        [ -f "$2/vorbis.dll.orig" ] || cp -p "$2/vorbis.dll" "$2/vorbis.dll.orig"
        cp -f "$DLL" "$2/vorbis.dll"
        info "installed; original kept as vorbis.dll.orig" ;;
    restore)
        need_dir "${2:-}"; [ -f "$2/vorbis.dll.orig" ] || die "no vorbis.dll.orig to restore"
        mv -f "$2/vorbis.dll.orig" "$2/vorbis.dll"; info "original restored" ;;
    status)
        need_dir "${2:-}"
        if [ -f "$DLL" ] && is_ours "$2"; then echo "SSE build installed"; else echo "original (or unknown) vorbis.dll"; fi ;;
    *) die "usage: vorbis-sse.sh build | install <dir> | restore <dir> | status <dir>" ;;
esac
