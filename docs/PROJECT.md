# games — Wine gaming environment on Apple Silicon

A hand-built Wine environment for running Windows games on macOS, deliberately
*not* using a wrapper (CrossOver / Sikarugir / Heroic). The goal is twofold:
play games, and keep every layer open and patchable so fixes can go upstream.

Host: M1 Max, macOS 26.5.2, 219 GiB free.

## Design

Four things vary independently, so each is its own directory:

| Dir | Holds | Shared or per-game |
|---|---|---|
| `runtimes/` | Built Wine trees (`wine-11.14-x86_64`, later arm64ec) | **shared**, versioned |
| `backends/` | Graphics translation DLL sets (DXMT, DXVK, D3DMetal, mesa-gl) | **shared**, versioned |
| `prefixes/` | One `WINEPREFIX` per game (~400 MB each) | **per game** |
| `profiles/` | `<game>.env` — picks runtime + backend + prefix + exe + tuning | **per game** |
| `library/` | The real game data (`steamapps/`) | **shared, outside every prefix** |

`bin/gamerun <profile>` composes them at launch. Swapping a graphics backend
for one game is a one-line profile edit — no reinstall, no new prefix. That
makes A/B testing backends cheap, which is the prerequisite for doing any
useful upstream work.

### Why the Steam library lives outside the prefixes

Per-game prefixes are the right unit for *tuning* (one game's winetricks hack
must not break another), but a naive per-game prefix would duplicate tens of GB
of game data. So `library/` is a real directory on the host, symlinked into
every prefix as **drive S:**. Each prefix has its own small Steam client
install, and each registers `S:\steamapps` as an external Steam library folder.
Game data therefore exists exactly once.

**Constraint:** never run two prefixes' Steam clients at the same time. They
would both write to the same `steamapps` and corrupt `appmanifest` state.

### Runtimes are swappable on purpose

Wine on macOS is currently an x86-64 binary running under Rosetta 2. macOS 27 is
the last release with full Rosetta 2; macOS 28 (2027) keeps only a restricted
subset for legacy games. Whether that subset covers Wine's usage pattern is
unknown. Keeping the runtime a swappable, versioned directory means an ARM64EC
Wine can be dropped in later without touching a single prefix or profile.

## Decisions taken

- **Fully open stack.** DXMT / DXVK / KosmicKrisp / Mesa. D3DMetal (Apple,
  closed) is excluded for now — see below for how to add it later.
- **Upstream Wine, not staging.** Cleaner base for upstreaming patches and a
  reliable reference point when something breaks. A staging runtime can be
  added alongside; runtimes are just directories.
- **`--enable-archs=i386,x86_64`** (new WoW64). Required for Strategic Command,
  a 2014 DX9 title that is almost certainly a 32-bit binary. New WoW64 means the
  unix side stays 64-bit only, so no 32-bit host libraries are needed.
- **Everything built from source**, including DXMT — hence the full Xcode
  requirement (Command Line Tools alone has no `metal` compiler).

## Target games and what each exercises

| Game | Engine / API | Bits | Backend | Why it's in the set |
|---|---|---|---|---|
| Quasimorph | Unity, D3D11 | 64 | DXMT | Light. First bring-up target. |
| Battleship Command | Unity, D3D11 | 64 | DXMT | GPU-heavy; the real perf test. |
| Strategic Command WWII: WiE | Fury engine, **D3D9** | 32 | DXVK-9 or wined3d→GL | DXMT does not do D3D9. Forces the 32-bit path and connects to the Mesa/zink work. |

Strategic Command is the interesting one: the `wined3d` → OpenGL route lands
directly on the `mesa_gl_to_metal` bridge, and it is a far lighter, more
debuggable GL workload than Task Force Admiral was.

## Adding D3DMetal later

Not a restructuring — one more `backends/` entry:

1. Obtain from Apple's GPTK (`brew install apple/apple/game-porting-toolkit`,
   or extract the DMG). It is a `D3DMetal.framework` plus PE DLLs
   (`d3d9/d3d11/d3d12/dxgi`).
2. Lay out as `backends/d3dmetal-3.0/{Frameworks/D3DMetal.framework,
   x86_64-windows/*.dll}` — the same shape CrossOver uses at
   `SharedSupport/CrossOver/lib64/apple_gptk/external/`.
3. `backend.conf` sets `DYLD_FALLBACK_FRAMEWORK_PATH` and
   `BACKEND_OVERRIDES="d3d11,d3d12,dxgi=n,b"`.

**Catch:** D3DMetal expects entry points that Apple's patched `winemac.drv`
provides, so it may not load on stock upstream Wine. Building from source with
a `patches/` dir makes this tractable. DXMT needs a similar `winemac.drv`
Metal-exposure patch, so the two share a mechanism.

## Gotchas

**Never wrap `wine` in `arch -x86_64`.** A thin x86_64 binary is auto-translated
by Rosetta when exec'd directly, so the wrapper buys nothing — and `/usr/bin/arch`
is SIP-protected, so dyld strips every `DYLD_*` variable when spawning through it.
Symptom: 18 "Wine cannot find the FreeType font library" errors per launch, and
Vulkan silently unavailable. `arch -x86_64` is required for *building* only.

**`DYLD_FALLBACK_LIBRARY_PATH` must include the matching Homebrew lib dir.** Wine
`dlopen`s host libraries by bare soname (`libfreetype.6.dylib`, `libvulkan.1.dylib`).
Modern macOS dropped `/usr/local/lib` from dyld's default search path. `bin/common.sh`
sets this per runtime: `/usr/local/lib` for x86_64, `/opt/homebrew/lib` for arm64.

**Always pass `WINEPREFIX`.** Without it Wine silently initialises `~/.wine`,
which is never what this tree wants.

**Steam runs headless if CEF's GPU process dies.** Steam's whole UI is CEF
(`steamwebhelper`). Under Wine, ANGLE cannot initialise — hardware Vulkan is
rejected *and* software SwANGLE fails — so the GPU process exits and no window
ever appears, while `steam.exe` keeps running happily. Symptom: the updater
window flashes, then nothing, no icon, no error. Fix: `-cef-disable-gpu` in the
profile's `ARGS`, which makes CEF rasterise on the CPU. Confirm success via
`logs/steamui_html.txt` — you want `PopupHTMLWindow` followed by `BrowserReady`.

**Wine's macOS windows are registered as "wine", not the app name.** Look for
`wine` in the Dock / Cmd-Tab, not "Steam".

**Homebrew hides the MoltenVK ICD.** It installs the manifest to
`etc/vulkan/icd.d/`, not the standard `share/vulkan/icd.d/`, so loader
autodiscovery finds no driver at all. `bin/common.sh` sets `VK_ICD_FILENAMES`
explicitly. MoltenVK is also flagged `is_portability_driver`, so the loader
still hides it from apps that don't opt in via `VK_KHR_portability_enumeration`
— which is why Steam's ANGLE fails even with the ICD configured.

**macOS ships bash 3.2**, where `${#arr[@]}` on a declared-but-empty array trips
`set -u`. The scripts use `set -eo pipefail` and the `${arr[@]+"${arr[@]}"}` idiom.

## Verified working (2026-07-29)

Wine 11.14, x86_64, `--enable-archs=i386,x86_64`, installed to
`runtimes/wine-11.14-x86_64`:

- 64-bit PE execution — OK
- **32-bit PE execution via new WoW64** — OK (96 builtins in `syswow64`); this is
  the Strategic Command path
- FreeType — 0 load errors
- Shared library visible in-prefix as `S:\steamapps` — OK
- Configured with: gnutls, freetype, vulkan, ffmpeg, both mingw targets

## OPEN: Steam client UI renders black (2026-07-29)

### CORRECTION: it is NOT simply "OpenGL 2.1"

An earlier revision of this file claimed stock winemac.drv exposing GL 2.1 was
the root cause. That was overstated. `test_wine_gl.exe` requests a *legacy*
context, and a legacy context is 2.1 on macOS **by definition** -- the number
proves nothing about what wined3d obtains. wined3d is demonstrably alive: it
creates contexts, identifies the Apple GPU, and reaches draw calls.

### The actual observed fault

    err:d3d:wined3d_check_gl_call >>>>>>> GL_INVALID_FRAMEBUFFER_OPERATION (0x506)
        from glClear @ dlls/wined3d/context_gl.c / 2246

An incomplete framebuffer in wined3d's macOS GL path. Also seen:
`OpenGL implementation does not support GL_PRIMITIVE_RESTART_FIXED_INDEX`
(a GL 4.3 feature; macOS caps at 4.1, so expected).

### What the working CrossOver bottle proves

Its Steam renders on **builtin wined3d** -- d3d11.dll/dxgi.dll are "Wine builtin
DLL", no DXVK/DXMT/D3DMetal override, no Direct3D registry tuning, GPU enabled,
and a completely clean cef_log. So the delta is inside CodeWeavers' patched
Wine, not in bottle configuration.

### Ruled out (measured, not assumed)
- **Wine build correctness** -- notepad renders perfectly; 32- and 64-bit both run.
- **Network** -- `ConnectionCompleted()`, UDP/TCP/WebSocket all reachable.
- **Page load** -- JS executes from `steamloopback.host/library.js`.
- **DirectComposition** -- Wine's dcomp.dll returns E_NOTIMPL from all three
  entry points, so Chromium never takes that path.
- **SDL2.dll** (missing for `gldriverquery.exe`) -- missing in the *working*
  bottle too, so not the differentiator.
- **Dropping in CrossOver's winemac.drv binary** -- impossible across versions:
  `driver wants 108 but win32u has 110`. The driver is refused and no window
  can be created.
- **`MaxVersionGL`** -- no effect.

### Progression of the ANGLE failure as layers were fixed
| Config | ANGLE result |
|---|---|
| builtin vulkan-1 + MoltenVK | `error -9`; SwANGLE also fails; GPU process exits |
| `vulkan-1=n` | switches to D3D11; `GLES 3.0 > max supported (2,0)` |
| KosmicKrisp ICD | identical; ANGLE prefers D3D11, never consults Vulkan |
| **DXMT** | GLES cap **gone**; fails at `SwapChain11::reset` `0x80004005` -> `EGL_BAD_ALLOC` |
| DXMT + `-cef-disable-gpu-compositing` | zero ANGLE/EGL errors -- still black |

**Also required regardless:** native `d3dcompiler_47.dll`. Wine's builtin is a
vkd3d-shader wrapper (1.1 MB); the real Microsoft one is 4.3 MB
("Microsoft (R) Optimizing Compiler"). ANGLE compiles HLSL through it, and the
CrossOver bottle installs it. Now copied into our prefix; formalise via
winetricks `d3dcompiler_47`.

### Next candidates
1. **CrossOver sources.** CodeWeavers publishes them (LGPL). Diff their
   `dlls/winemac.drv` against upstream, port the delta as a patch. Legal,
   maintainable, and exactly the kind of change worth upstreaming.
2. **Fix `GL_INVALID_FRAMEBUFFER_OPERATION` directly** in wined3d's macOS path.
3. **DXMT's DXGI swapchain gap** -- it already clears the feature-level hurdle;
   only `SwapChain11` creation fails. Its debug log stays empty, so this needs
   source-level work.

### Earlier status (superseded)
**Status: was unresolved.** The Steam client runs, connects, loads its UI HTML and
executes its JavaScript — but the window is a black rectangle.

### Ruled out
- **Wine build.** `notepad` renders perfectly (menus, scrollbar, status bar).
  `winemac.so`, `opengl32.so`, `winevulkan.so`, `winecoreaudio.so` all present.
  64-bit and 32-bit PE both execute.
- **Network.** `connection_log.txt` shows `ConnectionCompleted()` and
  "UDP - yes, TCP - yes, WebSocket - yes".
- **Page load.** JS errors are reported from `steamloopback.host/library.js`,
  so the UI document loads and runs.

### Failure chain, and how far each fix got
| Config | ANGLE result |
|---|---|
| builtin `vulkan-1`, MoltenVK ICD | Vulkan `error -9` (`VK_ERROR_INCOMPATIBLE_DRIVER`); SwANGLE also fails; **GPU process exits** |
| `vulkan-1=n` (CEF's own loader) | switches to **D3D11**; `GLES 3.0 > max supported (2,0)` — wined3d feature level too low |
| KosmicKrisp ICD | identical to above; ANGLE prefers D3D11 once reachable, so the Vulkan driver is never consulted |
| **DXMT installed** | GLES cap **gone**; fails later at `SwapChain11::reset` `HRESULT 0x80004005` → `eglCreateWindowSurface: EGL_BAD_ALLOC` |
| DXMT + `-cef-disable-gpu-compositing` | **zero ANGLE/EGL/SwapChain errors** — and still black |

The last row is the important one: CEF reports no rendering errors at all, yet
no pixels reach the window. That points at the CEF→Wine *presentation* path
rather than ANGLE initialisation.

Also tried: `-cef-disable-gpu` (black), Wine virtual desktop via
`explorer /desktop=` (Steam does not finish starting at all).

### Next candidates
- Steam's legacy `cef.win7x64` build (a different, older rendering path) —
  reachable by reporting Windows 7, though Steam dropped Win7 support.
- An older Steam client package.
- Bypass installation entirely with `steamcmd` (see below) and validate DXMT
  against a real game, decoupling progress from this bug.

## Related work on this machine

- `/Users/<user>/progs/mesa_gl_to_metal` — Mesa (zink + kosmickrisp) +
  `libmesa_wine.dylib` GL bridge + patched `winemac.drv`, Wine 11.2. Shelved
  mid-investigation (Metal texture-view sampling returns zeros, blocking
  `applyLut()`). In scope to return to; it is the OpenGL backend for this tree.
- `/Applications/CrossOver.app` (26.0) — kept as a cross-check oracle, not a
  dependency. Its `lib/` ships `dxvk/`, `dxmt/` and `lib64/apple_gptk/` side by
  side, which is where this tree's backend layout comes from.
