#!/bin/bash
# Build the Windows-side probes used to compare our Wine against a reference
# (CrossOver). Each answers one question from inside a prefix, which is far more
# reliable than inferring capability from an application's error messages.
#
#   vkprobe    which vulkan-1.dll loads, and what instance version it reports
#   dwprobe    whether DirectWrite enumerates a system font collection
#   dxgiprobe  which GPU adapter (vendor/device) Wine reports to the app
#   xinputprobe what XInput reports -- slots, buttons, sticks, triggers
#   dinputprobe what DirectInput8 enumerates, acquires and reads
#
# Run xinputprobe and dinputprobe as a pair. A pad can be perfect on one API and
# absent on the other, because winexinput.sys splits an XInput-capable gamepad
# into an &XI_00 node and an &IG_00 node -- and joy.cpl files anything with
# &ig_ under its XInput list, leaving its DInput list empty by design. An empty
# list there is a UI classification, not a DirectInput failure.
#
# One probe here is host-side rather than Windows-side, because the thing it
# tests happens before Wine is involved:
#
#   sdlopen    whether the bundled libSDL2 survives a dlopen off the main
#              thread, which is exactly what winebus does at service start
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

# -mconsole, not -municode: with mingw-w64 14 these link against the windows-
# subsystem CRT and fail on an undefined wWinMain. They want stdout anyway.
"$CC"  -O1 -mconsole -o xinputprobe.exe xinputprobe.c -lxinput
"$CC"  -O1 -mconsole -o dinputprobe.exe dinputprobe.c -ldinput8 -ldxguid -lole32

# Host-side, and x86_64 on purpose: the runtime runs under Rosetta and the two
# Homebrew prefixes hold different SDL builds.
clang -arch x86_64 -O0 -o sdlopen sdlopen.c

echo "built: vkprobe.exe dwprobe.exe dxgiprobe.exe xinputprobe.exe dinputprobe.exe sdlopen"
