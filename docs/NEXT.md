# Continue here

`PROJECT.md` holds design and decisions. `RUNTIME.md` holds what this
environment changes from stock Wine and why. This file is only "what to do
next".

## State

**Steam works.** `bin/gamerun steam` brings up the client, logs in, and renders
the store — text and all. It needs no environment variables: the runtime carries
its own libraries (`RUNTIME.md` §1).

**Strategic Command WWII is installed and launches, but renders black.** It is
the first non-Chromium target, and the first 32-bit one.

Working and verified:
- `runtimes/wine-11.14-x86_64` — upstream Wine, `--enable-archs=i386,x86_64`,
  bundled host libraries, one workaround patch
- `backends/dxmt-0.80` (D3D10/11 → Metal), `backends/dxvk-3.0.2` (D3D9 → Vulkan)
- `prefixes/steam-base` — Steam, corefonts, `d3dcompiler_47`, Wine Mono,
  VC++ 2022 runtimes
- `library/SteamLibrary/` — shared game library, mounted as `S:` in every prefix
- scripts: `provision-host`, `build-runtime`, `bundle-libs`, `get-addons`,
  `get-winetricks`, `mkprefix`, `gamerun`, `killwine`, `backend-install`,
  `compare-bottle`
- `tools/probes/` — one probe per question, see `RUNTIME.md` §6

## Next actions, in order

1. **Strategic Command: find out why it never draws.**

   What is known:
   - It launches, spins at ~100% CPU indefinitely, shows a black fullscreen
     window with an hourglass cursor. CrossOver runs the same game, from the
     same files, reaching the menu in ~30 s.
   - It **never reaches asset loading** — `lsof` shows only its own DLLs open
     (`SDL.dll`, `SDL_ttf`, `SDL_image`, `SDL_mixer`, `SDL_gfx`, `steam_api`),
     with read offsets static across 15 s. No campaign data, no bitmaps.
   - `WINEDEBUG=+file` shows the last modules it touches are **`ddraw.dll` and
     `opengl32.dll`** — it is an **SDL 1.2 / DirectDraw** title, *not* D3D9.
     That is why `wined3d` and DXVK behaved identically: neither was ever in
     the path. Do not re-test graphics backends without first confirming which
     API the game actually uses.
   - `WINEDEBUG=+wgl,+opengl,+ddraw` produced **no GL or ddraw calls at all**,
     so it stalls before it draws anything.

   Re-run that trace on the current runtime before going further: the earlier
   trace was taken when gnutls was failing to load 25 times per launch, and
   those failures were not necessarily innocent. The bundle fixed that.

2. **Then the other two games** — Quasimorph and Battleship Command, both Unity
   D3D11, so DXMT is the intended backend. They exercise a completely different
   path from Strategic Command and may well be easier.

3. **Fold prefix provisioning into a fresh prefix.** `bin/mkprefix` now installs
   corefonts, `d3dcompiler_47` and Wine Mono, but it has never been run
   end to end — `steam-base` predates it and was provisioned by hand. Making a
   per-game prefix is the natural time to exercise it.

## Known-good launch recipes

```sh
bin/gamerun steam                 # client; renders, logs in
bin/gamerun strategic-command     # launches via steam.exe -applaunch 593030
```

Steam games must be launched **through Steam**, not by running the game exe:
`steam_api.dll` re-launches the game via `steam://run/<appid>` and exits 53, and
that hand-off goes through Wine's URL handler, which does not inherit our
environment. See `RUNTIME.md` §9.

## Open questions

- **Why does CEF add `--in-process-gpu` under CrossOver and not for us?** We
  force it via `WINE_APPEND_ARGS`; the trigger is still unknown. Ruled out:
  bundled MoltenVK, the Apple GPTK hook, bottle registry, environment
  variables, D3DMetal DLLs, the AMD GPU identity spoof, wined3d's Vulkan
  renderer, and Chromium's GPU blocklist. See `RUNTIME.md` §7 for the
  observability flags to start from.
- **Why did our build of CrossOver's own published source not behave like their
  shipped binaries?** Same tree, but GPU identification fell back to a dummy
  card. Build configuration or bundled dependencies differ in some way we did
  not identify.

## Method note

Every real finding in this project came from comparing against a working
reference or measuring directly — `tools/probes/`, surface dumps, per-PID
traces, call-set diffs against CrossOver. Inferring causes from an
application's error messages produced a string of confident, wrong theories.
The refuted list is kept below so they are not chased twice.

## Do not re-chase these (measured and refuted)

- *"The dlopen failures are Wine bugs to fix upstream."* No — Wine dlopens by
  soname by design and macOS lacks `/etc/ld.so.conf.d/`. Five sites of one
  pattern is a policy. Fixed by bundling; patches retracted to
  `patches/retracted/`.
- *"Strategic Command is a D3D9 game, so the backend matters."* It is SDL 1.2 /
  DirectDraw. `wined3d` and DXVK produce identical symptoms because neither is
  in the path.
- *"The missing VC++ 2022 runtime is why the game hangs."* It was a genuine
  difference from CrossOver's bottle and is now installed, but the game still
  hangs identically.
- *"`gldriverquery.exe` failing is the differentiator"* — it fails identically
  in CrossOver's working bottle (`STATUS_DLL_NOT_FOUND`, missing `SDL2.dll`).
  It is not even the binary that runs the GPU topology query; `steamsysinfo.exe`
  is.
- *"`-cef-in-process-gpu` is a Steam option"* — not in this build. The complete
  set is `-cef-disable-gpu`, `-cef-disable-gpu-sandbox`, `-cef-disable-sandbox`,
  `-cef-disable-seccomp-sandbox`, `-cef-force-accessibility`, `-cef-force-gpu`,
  `-no-cef-sandbox`. (`-noreactlogin` and `-cef-force-32bit` from old forum
  threads are absent too.)
- *"steam.exe appends `--in-process-gpu`"* — impossible; the string exists only
  in `libcef.dll`. CEF appends it to its own command line in-process.
- *"Steam persists the GPU-off decision"* — no. CrossOver's bottle has never
  logged `Disabling GPU` and never spawned a `--type=gpu-process`.
- *"Shim `steamwebhelper.exe` to force the flag"* — Steam self-heals its
  binaries, restoring size and mtime before launching.
- *"A better renderer would remove the need for `--in-process-gpu`"* — no.
  `winemac.drv/opengl.c` carries the same per-process gate as the GDI path and
  says so: `FIXME("DC for window %p of other process: not implemented")`.
- *"winemac.drv never requests a core profile"* — false; a trace shows wined3d
  getting a 4.1 context.
- *"DirectComposition swallows frames"* — Wine's `dcomp.dll` returns
  `E_NOTIMPL` from all three entry points, so Chromium never uses it.
- *"Drop in CrossOver's winemac.drv binary"* — impossible: user-driver ABI 108
  vs our 110. The same applies to their `dxgi`/`wined3d`: those are internal
  interfaces with no cross-version stability, and transplanting them hangs.
