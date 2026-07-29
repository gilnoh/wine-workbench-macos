#!/bin/bash
# Build environment for the x86_64 Wine runtime.
#
# Usage:  arch -x86_64 zsh
#         source bin/env-build-x86_64.sh
#
# Deliberately does NOT put Homebrew LLVM's lib dir in LDFLAGS -- Wine should
# link against Apple's toolchain libc++, not LLVM's. (The Mesa project's
# env-x86_64.sh does set it, because Mesa needs to link libLLVM.)

if [ "$(uname -m)" != "x86_64" ]; then
    echo "ERROR: not in an x86_64 shell. Run 'arch -x86_64 zsh' first."
    return 1 2>/dev/null || exit 1
fi

hash -r 2>/dev/null

BREW=/usr/local

# bison: system bison is 2.3, Wine needs 3.x
export PATH="$BREW/opt/bison/bin:$BREW/bin:$PATH"

# mingw-w64 cross compilers for the PE-format Wine DLLs (--with-mingw).
# Both i686- and x86_64- targets are needed for --enable-archs=i386,x86_64.
export PKG_CONFIG_PATH="$BREW/lib/pkgconfig:$BREW/share/pkgconfig:${PKG_CONFIG_PATH:-}"

# Headers/libs for freetype, gnutls, sdl2, vulkan-loader from x86_64 Homebrew
export CPPFLAGS="-I$BREW/include"
export LDFLAGS="-L$BREW/lib"

echo "wine build env ready:"
echo "  arch:    $(uname -m)"
echo "  brew:    $($BREW/bin/brew --prefix)"
echo "  bison:   $(bison --version 2>/dev/null | head -1)"
echo "  mingw64: $(command -v x86_64-w64-mingw32-gcc || echo 'NOT FOUND')"
echo "  mingw32: $(command -v i686-w64-mingw32-gcc || echo 'NOT FOUND')"
echo "  clang:   $(clang --version 2>/dev/null | head -1)"
