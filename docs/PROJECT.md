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

### BREAKTHROUGH: CrossOver uses wined3d's **Vulkan** renderer (CW HACK 18311)

Diffing CrossOver 26's sources (LGPL, `media.codeweavers.com/pub/crossover/source/`)
against upstream Wine 11.0 found the mechanism, in `dlls/wined3d/directx.c`:

```c
-    return wined3d_adapter_gl_create(ordinal, wined3d_creation_flags);
+    if (wined3d_settings.renderer == WINED3D_RENDERER_OPENGL)
+        return wined3d_adapter_gl_create(...);
+    /* CW HACK 18311: Use the Vulkan renderer on macOS. */
+    if ((adapter = wined3d_adapter_vk_create(...)))
+        ERR_(winediag)("Using the Vulkan renderer for d3d10/11 applications.\n");
```

They run wined3d over **Vulkan**, not OpenGL, for d3d10/11. **Upstream already
supports this with no patch at all:**

    [HKEY_CURRENT_USER\Software\Wine\Direct3D]
    "renderer"="vulkan"

Diff sizes vs upstream 11.0: `dxgi` **0 files changed**; `wined3d` 366
insertions/13 files; `winemac.drv` 1175 insertions/23 files (mostly
game-specific hacks, D3DMetal glue, keyboard/window work).

Their `adapter_vk.c` additionally expects `VK_DRIVER_ID_MOLTENVK` and disables
`multiple_viewports` on MoltenVK ("causes glitches in multiple games").

### Current state of the Vulkan attempt

`renderer=vulkan` is confirmed active (`winediag: Using the Vulkan renderer.`)
and Vulkan works inside Wine -- KosmicKrisp enumerates a physical device
(`VK_EXT_external_memory_metal` present). **But ANGLE still reports
`GLES 3.0 > max supported (2,0)`.**

- **KosmicKrisp**: Vulkan device enumerates; only the GLES cap remains.
- **MoltenVK** (CrossOver's choice): *worse* -- 34 errors, back to
  `SwANGLE failed / EGL_NOT_INITIALIZED`. The loader's portability filter
  hides it, as it did for ANGLE earlier.

**Leading hypothesis:** wined3d-on-Vulkan reports D3D feature level < 10_0
because Metal has no geometry shaders (FL 10_0 requires them), so ANGLE caps at
GLES 2.0. Next step is to confirm the feature level wined3d advertises and
compare with what CrossOver's patched wined3d does.

### Two GL theories raised and REFUTED (recorded so they are not re-chased)

1. *"winemac.drv never requests core profile."* False. A trace shows wined3d
   creating a probe context at 2.1 and then a real context at **4.1**:
   `trace:opengl:make_context_current context ... version 4.1`.
   The GL 2.1 reading came from `test_wine_gl.exe`, which creates a *legacy*
   context -- 2.1 by definition on macOS, and no evidence about wined3d.
2. *"Forward-compatible bit is missing."* CrossOver does carry such a hack
   (CW Hack 24834, `CX_FWD_COMPAT_GL_CTX`), ported here as
   `patches/wine-0001-macdrv-force-fwd-compat-gl-ctx.patch` and gated behind
   `WINEMAC_FWD_COMPAT_GL_CTX=1`. **It never fires** -- wined3d already gets
   4.1 -- so it is inert. Kept only for completeness; not the fix.

### FEATURE LEVEL ANSWERED: capped at 9_3 by missing geometry shaders

`dlls/wined3d/adapter_vk.c`, `feature_level_10_supported()`:

```c
return shader_model >= 4
        && info->features2.features.multiViewport
        && info->features2.features.geometryShader      /* <-- Metal has none */
        && ...
```

Metal has no geometry shader stage, so any Vulkan-on-Metal driver (MoltenVK or
KosmicKrisp) reports `geometryShader = FALSE`. wined3d therefore caps at
**feature level 9_3**, ANGLE maps that to **GLES 2.0**, and Chromium needs 3.0.

### The contradiction this creates

CodeWeavers do **not** patch this check, and do **not** patch `winevulkan`
(0 files changed) or `dxgi` (0 files changed). Their only nearby change sets
`vk_info->multiple_viewports`, which is a different field and does not feed the
feature-level test. So CrossOver's wined3d-on-Vulkan should cap at 9_3 too --
yet their Steam renders with a completely clean cef_log.

**Conclusion: their ANGLE is not going through D3D11 at all.** Most likely it
succeeds on SwiftShader or its Vulkan backend. Our ANGLE keeps choosing D3D11
and failing there.

Note CrossOver ships `lib64/libMoltenVK.dylib` and **no ICD manifest**, so their
winevulkan binds MoltenVK directly (Wine's configure falls back to
`WINE_CHECK_SONAME(MoltenVK, ...)` when no loader is present) and never meets
the loader's portability filter. Reproduced here with a shim -- symlinking
`libvulkan.1.dylib -> libMoltenVK.dylib` in `runtimes/<rt>/vkshim/` and putting
it first on `DYLD_FALLBACK_LIBRARY_PATH`. It binds, but ANGLE still selects
D3D11, so it did not change the outcome. Clearing Chromium's GPUCache /
ShaderCache / `Local State` did not change the selection either.

### THE TARGET CONFIGURATION IS NOW KNOWN

Probing CrossOver's *running* Steam settled it. Their Steam has **no GPU process
at all** -- only `utility` and `renderer`. It renders in software, in-process.
Their webhelper command line (from `logs/webhelper.txt`):

    --no-sandbox  --in-process-gpu  --disable-gpu
    --use-angle=swiftshader-webgl  --use-gl=angle  --disable-gpu-compositing

and `logs/webhelper_gpu.txt` says `Disabling GPU acceleration: Disabled/CommandLine`.

So the fix was never a graphics translation layer. It is getting Chromium into
software-SwiftShader mode with the GPU work inside the browser process.

**Our working launch line so far:**

    WINEDLLOVERRIDES="d3dcompiler_47=n;vulkan-1=n"
    steam.exe -cef-disable-gpu -cef-disable-sandbox

`vulkan-1=n` is essential and non-obvious: SwiftShader loads through CEF's own
bundled `vulkan-1.dll`, and Wine's builtin winevulkan hijacks that name and then
offers no usable device. `-cef-disable-gpu` alone leaves SwiftShader unable to
initialise; `vulkan-1=n` alone lets ANGLE pick D3D11 instead. **Only both
together** produce `Disabled/CommandLine` + zero graphics errors.

That combination now reproduces every flag CrossOver has **except one**:

| Flag | CrossOver | Ours |
|---|---|---|
| `--disable-gpu` / `--disable-gpu-compositing` | yes | yes |
| `--use-angle=swiftshader-webgl` / `--use-gl=angle` | yes | yes |
| `--no-sandbox` | yes | yes |
| **`--in-process-gpu`** | yes | **NO** |
| GPU process running | none | still spawns |

Steam does not map `-cef-in-process-gpu`, so we cannot ask for it directly.
Their Steam adds it on its own; ours does not. The window is still black, and
this is the only remaining difference.

### ANSWER: CrossOver does NOT pass the flag -- Steam emits it itself

Searched exhaustively:
- `grep -c in-process-gpu` over the **entire CrossOver 26 source tarball**: **0**
- Same for the whole `/Applications/CrossOver.app` bundle: **0**
- Their bottle's Steam shortcut is a plain
  `wine --bottle Steam --start ...Steam.lnk` with **no arguments**
- No GPU keys in their `user.reg` / `system.reg`, and their `config.vdf`
  matches ours exactly

So Steam itself decides to add `--in-process-gpu`, based on what it detects
about the environment. `-cef-in-process-gpu` **is** a genuine Steam launch
option ("runs CEF GPU processing as thread of browser process"), but in our
prefix Steam drops it -- passing it alongside `-cef-disable-gpu` yields neither.

Steam has (at least) three GPU-disable paths, distinguishable in
`logs/webhelper_gpu.txt`:
- `Disabled/CommandLine`   -- from `-cef-disable-gpu`
- `Disabled/RuntimeDetect` -- Steam noticed the GPU process failing and gave up
  (observed here after passing `-cef-in-process-gpu` alone)

Their flag set (`--disable-gpu --in-process-gpu`) differs from what our Steam
produces from `-cef-disable-gpu` (`--disable-gpu --disable-gpu-compositing
--use-gl=angle --use-angle=swiftshader-webgl`). Same client build 1785187029,
same `-uimode=7 -launcher=0 -startcount=0`. Steam also gives them
`--enable-features=PlatformHEVCDecoderSupport`, another *capability-detection*
outcome -- further evidence that Steam probes the environment and our prefix
answers differently.

Known detection difference: our `gldriverquery.exe` fails to launch
(`ChildProcessQuery: failed with code -2`, `CGpuTopology: failed to run gpu
topology query`) because it imports `SDL2.dll`, which is absent. Their bottle
logs **no such failure**. But `SDL2.dll` is missing from *both* prefixes and
both Steam trees are byte-identical in `bin/`, so why theirs succeeds is still
unexplained. **This is the most promising remaining thread.**

### Next step
Force `--in-process-gpu` into the webhelper. Steam builds that command line, so
the practical route is a shim: rename `steamwebhelper.exe` and drop in a small
launcher that re-execs the real binary with the extra flag appended. If the
window renders, the flag is confirmed as the last piece, and we then look for a
cleaner way to make Steam choose it (likely tied to GPU detection --
`gldriverquery.exe` fails to load in our prefix, so Steam never learns there is
no usable GPU).

### Next probe
Determine which backend the *working* CrossOver Steam actually uses -- run its
bottle and `vmmap` the live `steamwebhelper` for `vk_swiftshader` vs `d3d11`.
That single observation decides whether to chase software rendering or D3D11.

### Also required regardless
Native `d3dcompiler_47.dll`. Wine's builtin is a vkd3d-shader wrapper (1.1 MB);
the real Microsoft one is 4.3 MB ("Microsoft (R) Optimizing Compiler"). ANGLE
compiles HLSL through it and the CrossOver bottle installs it. Copied into our
prefix; formalise via winetricks `d3dcompiler_47`.

### Reference diffs kept
- `patches/crossover26-wined3d.diff`
- `patches/crossover26-winemac.drv.diff`

## Related work on this machine

- `/Users/<user>/progs/mesa_gl_to_metal` — Mesa (zink + kosmickrisp) +
  `libmesa_wine.dylib` GL bridge + patched `winemac.drv`, Wine 11.2. Shelved
  mid-investigation (Metal texture-view sampling returns zeros, blocking
  `applyLut()`). In scope to return to; it is the OpenGL backend for this tree.
- `/Applications/CrossOver.app` (26.0) — kept as a cross-check oracle, not a
  dependency. Its `lib/` ships `dxvk/`, `dxmt/` and `lib64/apple_gptk/` side by
  side, which is where this tree's backend layout comes from.
