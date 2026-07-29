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

1. **Chase the `gldriverquery.exe` difference.** Our Steam logs
   `ChildProcessQuery: failed with code -2` / `CGpuTopology: failed to run gpu
   topology query` on every launch; CrossOver's logs nothing. That helper
   imports `SDL2.dll`, which is missing from *both* prefixes — so why theirs
   succeeds is unexplained and worth 20 minutes. Steam's `--in-process-gpu` and
   `--enable-features=PlatformHEVCDecoderSupport` are both capability-detection
   outcomes, so this is the likely lever.
   Probe: run `Steam/bin/gldriverquery.exe` directly under both Wines and diff.

2. **If that dead-ends, force the flag with a shim.** Rename
   `steamwebhelper.exe` → `steamwebhelper_real.exe` and drop in a small mingw
   launcher that re-execs it with `--in-process-gpu` appended (only when the
   command line has no `--type=`, so Chromium children are untouched). This
   confirms whether the flag is genuinely the last piece. If it renders, then go
   back and find the clean trigger.

3. **Then formalise:** move `d3dcompiler_47` to winetricks, fold the working
   flags into `profiles/steam.env`, and delete the inert
   `patches/wine-0001-macdrv-force-fwd-compat-gl-ctx.patch` (it never fires —
   wined3d already gets a 4.1 core context).

4. **Then the games** — Quasimorph (DXMT), Battleship Command (DXMT),
   Strategic Command WWII (D3D9, 32-bit → DXVK first for a baseline, then the
   `wined3d`→GL route into `mesa_gl_to_metal`).

## Do not re-chase these (measured and refuted)

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
