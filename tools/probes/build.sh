#!/bin/bash
# Build the Windows-side probes used to compare our Wine against a reference
# (CrossOver). Each answers one question from inside a prefix, which is far more
# reliable than inferring capability from an application's error messages.
#
#   vkprobe    which vulkan-1.dll loads, and what instance version it reports
#   dwprobe    whether DirectWrite enumerates a system font collection
#   dxgiprobe  which GPU adapter (vendor/device) Wine reports to the app
#
# Run e.g.:
#   WINEPREFIX=... DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib:/usr/lib \
#     runtimes/wine-11.14-x86_64/bin/wine tools/probes/dxgiprobe.exe
set -eu
cd "$(dirname "$0")"
CC=${CC:-/opt/homebrew/bin/x86_64-w64-mingw32-gcc}
CXX=${CXX:-/opt/homebrew/bin/x86_64-w64-mingw32-g++}
"$CC"  -O1 -municode -o vkprobe.exe   vkprobe.c
"$CXX" -O1 -municode -o dwprobe.exe   dwprobe.cpp  -ldwrite -lole32
"$CXX" -O1 -municode -o dxgiprobe.exe dxgiprobe.cpp -ldxgi  -lole32
echo "built: vkprobe.exe dwprobe.exe dxgiprobe.exe"
