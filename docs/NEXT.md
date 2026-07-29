# Continue here

Read `docs/PROJECT.md` first — design, decisions, gotchas, and the full Steam
investigation including refuted theories. This file is only "what to do next".

## State: the Steam client window renders, text and all

`bin/gamerun steam` produces a complete, readable Steam login window: account
name and password fields with labels, "Remember me", the Sign in button, the QR
panel and its instructions, and the footer links.
Evidence: `docs/evidence/steam-login-rendered.png`.

Working and verified:
- `runtimes/wine-11.14-x86_64` — upstream Wine, `--enable-archs=i386,x86_64`,
  64-bit **and** 32-bit PE execution, ffmpeg/gnutls/freetype/vulkan/mingw,
  plus our local patches (see "Fixed")
- `backends/dxmt-0.80` — built from source against our own Wine tree
- `bin/mkprefix`, `bin/gamerun`, `bin/backend-install`, `bin/killwine`,
  `bin/compare-bottle`, `bin/vendor/winetricks`, `tools/probes/`
- `prefixes/steam-base` — Steam installed, updates, connects, renders

## Next actions, in order

1. **Log in and confirm the client works end to end** — library, downloads,
   launching a game.

2. **Formalise:** fix `bin/gamerun` (it still sets `VK_ICD_FILENAMES` to the
   portability-flagged MoltenVK ICD, the very thing that hides the driver — it
   should not set the variable at all), delete the inert
   `patches/wine-0001-macdrv-force-fwd-compat-gl-ctx.patch`, and drop the
   debugging TRACE/dump patch `wine-0005` once it is no longer wanted.
   Stale DXMT DLLs sit in `prefixes/steam-base/drive_c/windows/system32`
   (`dxgi`, `d3d11`, `d3d10core`) from an old backend install; Wine loads its
   builtins so they are inert, but they should be cleaned up.

3. **Send the three dlopen fixes upstream** — `wine-0002` (win32u vulkan),
   `wine-0003` (win32u freetype), `wine-0006` (dwrite freetype). One bug class,
   three sites, all real and none macOS-Steam-specific.

4. **Then the games** — Quasimorph (DXMT), Battleship Command (DXMT),
   Strategic Command WWII (D3D9, 32-bit → DXVK first for a baseline, then the
   `wined3d`→GL route into `mesa_gl_to_metal`).

## THE text bug: dwrite has its own FreeType handle

`dlls/dwrite/freetype.c` does `dlopen(SONAME_LIBFREETYPE)` — a **second,
independent** FreeType loader from the one in `dlls/win32u/freetype.c`. Patching
only win32u fixed GDI text and left DirectWrite text broken.

It fails **silently and invisibly**: `DWriteCreateFactory`, `CreateFontFace` and
`CreateGlyphRunAnalysis` all return `S_OK`, and `GetAlphaTextureBounds` simply
reports `0x0`. Skia asks for the glyph box, is told the glyph is empty, and never
rasterises. Result: a Chromium app draws its entire UI — images, controls,
gradients, even the text caret — with no text at all, while GDI text elsewhere in
the same prefix works perfectly.

Fix: `patches/wine-0006-dwrite-freetype-absolute-dylib-fallback.patch`.
Verify with `tools/probes/glyphprobe.cpp`, which rasterises one glyph the way
Skia does:

```
broken:  GetAlphaTextureBounds(1x1) hr=0 bounds=(0,0)-(0,0)   0x0
fixed:   GetAlphaTextureBounds(1x1) hr=0 bounds=(-1,-12)-(11,0) 12x12
         CreateAlphaTexture(1x1)    hr=0 non-zero bytes = 65 / 144
```

The fixed numbers are identical to CrossOver's, byte for byte.

**Not a regression** — pristine upstream Wine 11.0 fails the same way. CrossOver
escapes it only because they bundle FreeType, so their bare soname resolves.
Their `dlls/dwrite/freetype.c` is unmodified from upstream.

The clue was in the trace all along: `process_attach` appeared in CrossOver's
dwrite call set and never in ours. That *is* this function.

## Fixed this session (keep these)

- **Wine's window surfaces are per-process — this was the black window.**
  Proven with `WINEDEBUG=+pid,+bitblt`: the CEF browser process (pid `0138`)
  creates and flushes the login window's surface, while the viz/GPU process
  (pid `01e8`) performs all 459 content blits including six full 700x440
  paints. `winemac.drv/surface.c` hands a foreign hwnd a throwaway surface
  (`if (!(data = get_win_data(hwnd))) return TRUE; /* use default surface */`),
  so the page was drawn perfectly into a bitmap nobody displays. Windows allows
  cross-process window painting; Wine silently discards it.
  `--in-process-gpu` makes viz a *thread* of the browser process, which is why
  CrossOver works. Injected via
  `patches/wine-0004-kernelbase-append-args-env.patch` (`WINE_APPEND_ARGS`),
  because Steam restores its own binaries on launch and a shim exe cannot
  survive. **This is a workaround; the real fix is cross-process window
  surfaces in Wine, which is a substantial upstream change.**

- **Wine's FreeType dlopen has the same bare-soname bug as Vulkan.**
  `patches/wine-0003-win32u-freetype-absolute-dylib-fallback.patch`. Also
  upstreamable. (It was not the cause of the missing text — `notepad` proves
  Wine text works — but it is a real bug: any process spawned without
  `DYLD_FALLBACK_LIBRARY_PATH` had no FreeType at all.)

- **Wine `dlls/win32u/vulkan.c` dlopens a bare soname.** `dlopen("libvulkan.1.dylib")`
  resolves only through `DYLD_FALLBACK_LIBRARY_PATH`, and dyld's default
  fallback does not cover Homebrew (`~/lib` was tested too — also not searched).
  Any process Wine spawns without that variable inherited therefore sees *no*
  Vulkan: `vkEnumerateInstanceVersion` is missing, the caller assumes 1.0, and
  gives up. Steam's `steamsysinfo.exe` GPU-topology helper is spawned exactly
  that way, which is why it logged `vulkan instance version too old: 1.0.0`.
  Fix: retry absolute Homebrew paths — `patches/wine-0002-win32u-vulkan-absolute-dylib-fallback.patch`.
  **Genuinely upstreamable**, and it fixes far more than Steam.
  Rebuild: only `win32u.so` is affected (patch the source, *not* `config.h` —
  touching `config.h` forces a global rebuild).

- **MoltenVK's ICD is `is_portability_driver: true`.** The Khronos loader hides
  portability drivers from apps that do not opt into
  `VK_KHR_portability_enumeration`; Steam does not, hence `VK_ERROR_INCOMPATIBLE_DRIVER`
  (`res -9`). `/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json` was a Homebrew
  symlink into the Cellar; it is now a regular file with the flag removed.
  Revert with `brew reinstall molten-vk`. **This is host state, not in the repo —
  re-apply it after any `molten-vk` upgrade.**

Together these make the topology query return `k_EGpuDriverId_MoltenVK`, exit 0,
with no environment variables set at all.

## Do not re-chase these (measured and refuted)

- *"`gldriverquery.exe` failing is the differentiator"* — it fails **identically**
  in CrossOver's working bottle: exit `3221225781` = `STATUS_DLL_NOT_FOUND`
  (the missing `SDL2.dll`). Also, it is not even the binary that runs the GPU
  topology query — **`steamsysinfo.exe`** is.
- *"`-cef-in-process-gpu` is a Steam option"* — not in this build. The complete
  set is `-cef-disable-gpu`, `-cef-disable-gpu-sandbox`, `-cef-disable-sandbox`,
  `-cef-disable-seccomp-sandbox`, `-cef-force-accessibility`, `-cef-force-gpu`,
  `-no-cef-sandbox`. (`-noreactlogin` and `-cef-force-32bit` from old Reddit
  threads are also absent — Valve removed them years ago.)
- *"steam.exe appends `--in-process-gpu`"* — impossible: the string exists only
  in `libcef.dll`, not in `steam.exe` or `steamui.dll`. **CEF appends it to its
  own command line in-process**, which is why it lands after every other switch
  and why children inherit it past `/prefetch:N`. There is no config, registry
  key or CLI option to set it, and CrossOver's *first ever* run already had it.
- *"Steam persists the GPU-off decision"* — no. CrossOver's bottle has never
  logged `Disabling GPU` and has never spawned a `--type=gpu-process`.
  `steam.exe`/`steamui.dll` are byte-identical to ours; registries match.
- *"Shim `steamwebhelper.exe` to force the flag"* — **Steam self-heals its
  binaries.** It restored both `steamwebhelper.exe` and `steamsysinfo.exe` to
  their original size and mtime before launching. `chflags uchg` prevents the
  restore, but then Steam refuses to launch the wrong-sized file; padding it to
  the exact original size gets it launched but the process then produces no
  window and no log. Not a viable route without more work.

- *"winemac.drv never requests core profile"* — false; a trace shows wined3d
  getting a **4.1** context. The 2.1 reading came from a legacy context.
- *"Missing forward-compatible bit"* — ported CrossOver's hack; it never fires.
- *"DirectComposition swallows frames"* — Wine's `dcomp.dll` returns
  `E_NOTIMPL` from all three entry points, so Chromium never uses it.
- *"Drop in CrossOver's winemac.drv binary"* — impossible: user-driver ABI 108
  vs our win32u 110; the driver is refused and no window can be created.
- *"CrossOver passes the flag"* — no. Zero matches across their entire source
  tarball and app bundle; their shortcut passes no arguments.
- *"wined3d Vulkan renderer (CW HACK 18311) fixes Steam"* — it is real and
  `renderer=vulkan` enables it without any patch, but it caps at D3D feature
  level 9_3 because Metal has no geometry shaders, and CrossOver does not patch
  that check either. Relevant to **games**, not to Steam's UI.
- *"Missing SDL2.dll is the differentiator"* — missing in the working bottle too.

## Method note

The single highest-value move all session was probing CrossOver's **running**
Steam (`ps -ww -o args=`, its `logs/webhelper.txt`, `logs/webhelper_gpu.txt`).
Reading a working reference beat inferring causes from error messages, which
produced three wrong theories. When stuck again: find the working system and
read what it actually does.

## Open question: why does CEF add `--in-process-gpu` for CrossOver and not us?

Unresolved, and worth knowing rather than working around. CEF appends the
switch to its own command line in-process (the string exists only in
`libcef.dll`). CrossOver gets it on a *fresh* bottle's first run; we never do.

Tested and refuted: that Chromium decides from a browser-side D3D/DXGI probe.
Disabling `dxgi`/`d3d11`/`d3d10` for the prefix did **not** trigger it — it made
Chromium crash-loop the GPU process 11 times instead.

## Host state that is NOT in this repo

- `/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json` — was a Homebrew symlink with
  `is_portability_driver: true`; replaced by a regular file without the flag.
  **Re-apply after any `brew upgrade molten-vk`** (`brew reinstall molten-vk`
  restores the original).
- `cabextract` (Homebrew) — required by winetricks.
- Fonts installed into `prefixes/steam-base` with
  `bin/vendor/winetricks -q corefonts`.

## Runtime backups

Patched binaries in `runtimes/wine-11.14-x86_64` keep their originals beside
them: `win32u.so.pre-vkfix`, `ntdll.so.pre-vkfix`, `winemac.so.pre-trace`,
`kernelbase.dll.pre-inject` (both `x86_64-windows` and `i386-windows`).
`patches/wine-0005-winemac-surface-flush-trace.patch` is a debugging TRACE only
-- drop it once the text bug is closed.

## Also refuted this round (environmental guesses, all reverted)

- *"upstream regression between 11.0 and 11.14"* — no. Pristine upstream 11.0
  behaves identically to our 11.14: `Disabling GPU acceleration due to runtime
  detect`, 3 gpu-process spawns, no `--in-process-gpu`.
- *"CrossOver injects the switch somewhere"* — no. **Zero** matches for
  `in-process-gpu` in their entire Wine source. Their `kernelbase/process.c`
  patch is game hacks only (Epic launcher desktop, a powershell block keyed on
  one `SteamGameId`, Wow64 spoofing).
- *"the AMD GPU spoof is the trigger"* — CrossOver reports vendor `0x1002`
  device `0x66af` ("AMD Compatibility Mode") where we report Apple `0x106b`.
  Reproduced exactly via Wine's own `HKCU\Software\Wine\Direct3D`
  `VideoPciVendorID`/`VideoPciDeviceID` — CEF still did not add the switch.
  (It did help: gpu-process spawns 3 -> 1, and the runtime-detect churn stopped.)
- *"CW HACK 18311 / wined3d Vulkan renderer"* — reproduced without patching via
  `HKCU\Software\Wine\Direct3D` `renderer=vulkan`. Identical CEF errors.
  Note CrossOver does **not** patch `feature_level_10_supported()`; they make it
  stricter, disabling `multiViewport` under MoltenVK. Their D3D11 is also capped
  at feature level 9_3 — they succeed *despite* the same limit we have.
- *"disable dxgi/d3d11 so Chromium finds no GPU"* — made it worse: 11
  gpu-process crash loops.
