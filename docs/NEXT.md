# Continue here

Read `docs/PROJECT.md` first — design, decisions, gotchas, and the full Steam
investigation including refuted theories. This file is only "what to do next".

## State: everything works except the Steam client window

Working and verified:
- `runtimes/wine-11.14-x86_64` — upstream Wine, `--enable-archs=i386,x86_64`,
  64-bit **and** 32-bit PE execution, ffmpeg/gnutls/freetype/vulkan/mingw
- `backends/dxmt-0.80` — built from source against our own Wine tree
- `bin/mkprefix`, `bin/gamerun`, `bin/backend-install`, `bin/killwine`,
  `bin/compare-bottle`
- `prefixes/steam-base` — Steam installed, updates, connects, loads its UI

Not working: the Steam client renders a **black window**. Everything else about
it is healthy — it connects, runs its JavaScript, creates the window.

## The one remaining difference

CrossOver's working Steam runs its CEF browser process with `--in-process-gpu`.
Ours never does. Every other flag now matches.

Our best launch line so far (matches CrossOver except that one flag, and
produces `Disabling GPU acceleration: Disabled/CommandLine` with **zero**
graphics errors):

```sh
WINEDLLOVERRIDES="d3dcompiler_47=n;vulkan-1=n" \
  runtimes/wine-11.14-x86_64/bin/wine \
  "C:\Program Files (x86)\Steam\steam.exe" -cef-disable-gpu -cef-disable-sandbox
```

Both overrides are required and neither is obvious:
- `d3dcompiler_47=n` — Wine's builtin is a vkd3d-shader wrapper; ANGLE needs the
  real 4.3 MB Microsoft compiler. Currently copied from the CrossOver bottle;
  **replace with winetricks `d3dcompiler_47`** for a clean provenance.
- `vulkan-1=n` — SwiftShader loads through CEF's *own* bundled `vulkan-1.dll`;
  Wine's builtin winevulkan hijacks that name and then offers no usable device.

## Next actions, in order

1. **Find out why the shown window paints nothing.** This is now the whole
   problem, and it is *not* a GPU-initialisation problem any more. With the
   Vulkan fix below plus `-cef-disable-gpu -cef-disable-sandbox` we get:
   - `cef_log.txt` graphics errors: **0** — the log is line-for-line identical
     in shape to CrossOver's working one (no ANGLE, no EGL, no swapchain)
   - the login window is created, titled `'Sign in to'`, and `WasHidden 0`
   - and it is still a black rectangle.

   So CEF believes it is compositing in software and presenting. The next place
   to look is the *presentation* path, not the GL/Vulkan stack: how CEF's
   software frame reaches the HWND, and what `winemac.drv` does with it.
   Suggested probe: `WINEDEBUG=+bitblt,+winemac` on the browser process and see
   whether any blit reaches the window at all; compare against CrossOver.

2. **Then formalise:** move `d3dcompiler_47` to winetricks, fold the working
   flags into `profiles/steam.env`, fix `bin/gamerun` (see below), and delete
   the inert `patches/wine-0001-macdrv-force-fwd-compat-gl-ctx.patch` (it never
   fires — wined3d already gets a 4.1 core context).

   **`bin/gamerun` bug:** it unconditionally sets `VK_ICD_FILENAMES` to the
   portability-flagged MoltenVK ICD, which is exactly what hides the driver from
   the Vulkan loader. It should not set the variable at all — the loader's
   default search finds MoltenVK correctly.

4. **Then the games** — Quasimorph (DXMT), Battleship Command (DXMT),
   Strategic Command WWII (D3D9, 32-bit → DXVK first for a baseline, then the
   `wined3d`→GL route into `mesa_gl_to_metal`).

## Fixed this session (keep these)

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
