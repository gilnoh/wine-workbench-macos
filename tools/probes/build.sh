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
#   mfprobe    whether Media Foundation can open AND decode a file: resolver,
#              source reader, native stream types, then 30 decoded video frames.
#              Opening and decoding fail separately -- c00d36c4 (no byte stream
#              handler) vs c00d5212 (no decoder) -- so it reports both.
#   oggprobe   how fast an APPLICATION'S OWN vorbisfile.dll decodes a file
#              (32-bit; x87-only codecs are ~16x slower under Rosetta)
#   imgprobe   how long GDI+ takes to load and decode an image (32 + 64-bit)
#   waitprobe  msync wait-registration stress: WaitForMultipleObjects({quit,
#              work}) ping-pong; with WINEMSYNC_STATS=1 on a wine-0006 runtime,
#              a rising in_use is the node-pool leak that wine-0007 fixes
#   existprobe whether a file is reported as existing under a root whose
#              directories do not exist, with N threads at once (and a real
#              root as the control) -- Sea Power's ini preloader, 2026-10-10
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
"$CC"  -O1 -mconsole -o vkprobe.exe   vkprobe.c    # main(), not wmain()
"$CXX" -O1 -municode -o dwprobe.exe   dwprobe.cpp  -ldwrite -lole32
"$CXX" -O1 -municode -o dxgiprobe.exe dxgiprobe.cpp -ldxgi  -lole32

# -mconsole, not -municode: with mingw-w64 14 these link against the windows-
# subsystem CRT and fail on an undefined wWinMain. They want stdout anyway.
"$CC"  -O1 -mconsole -o xinputprobe.exe xinputprobe.c -lxinput
"$CC"  -O1 -mconsole -o dinputprobe.exe dinputprobe.c -ldinput8 -ldxguid -lole32

# Host-side, and x86_64 on purpose: the runtime runs under Rosetta and the two
# Homebrew prefixes hold different SDL builds.
# Pin the SDK to the selected toolchain's own: with CommandLineTools' MacOSX27
# SDK and Xcode 26.6's linker every link fails ("tapi error: malformed file").
SDK="$(DEVELOPER_DIR="$(xcode-select -p)" xcrun --sdk macosx --show-sdk-path)"
clang -arch x86_64 -isysroot "$SDK" -O0 -o sdlopen sdlopen.c

"$CC"  -O1 -municode -o mfprobe.exe mfprobe.c -lmfplat -lmfreadwrite -lmfuuid -lole32 -luuid
CC32=${CC32:-/opt/homebrew/bin/i686-w64-mingw32-gcc}
"$CC32" -O1 -mconsole -o oggprobe32.exe oggprobe.c
"$CC32" -O1 -municode -o imgprobe32.exe imgprobe.c -lgdiplus
"$CC"   -O1 -municode -o imgprobe64.exe imgprobe.c -lgdiplus
"$CC"   -O1 -mconsole -o waitprobe.exe waitprobe.c
"$CC"   -O1 -mconsole -o existprobe.exe existprobe.c

echo "built: vkprobe.exe dwprobe.exe dxgiprobe.exe xinputprobe.exe dinputprobe.exe sdlopen mfprobe.exe oggprobe32.exe imgprobe32.exe imgprobe64.exe waitprobe.exe existprobe.exe"
