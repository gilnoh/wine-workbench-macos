# The runtime: what we changed, and why

How to rebuild this environment on a clean machine, and — more importantly —
*why* each deviation from stock exists. `PROJECT.md` holds design and decisions;
`NEXT.md` holds what to do next; this file holds the runtime itself.

Everything here was measured, not assumed. Where a belief turned out to be
wrong, the correction is recorded rather than quietly dropped, because the wrong
beliefs cost more time than the right ones.

---

## 1. Bundled libraries — the platform mismatch, and the real fix

Wine `dlopen`s optional libraries **by bare soname, by design**:
`WINE_CHECK_SONAME` in `aclocal.m4` records only the soname, never a path, and
delegates resolution to the dynamic loader. That is correct on Linux, which has
`/etc/ld.so.conf.d/` to extend the search path. **macOS has no equivalent**, and
dyld searches only `/usr/lib` plus the shared cache:

```
$ env -u DYLD_FALLBACK_LIBRARY_PATH ./dltest
libgnutls.30.dylib   tried: '/usr/lib/libgnutls.30.dylib' (no such file, not in dyld cache)
/usr/local/lib/libgnutls.30.dylib   OK
```

Note `/usr/local/lib` is **not** searched, despite older documentation listing
`$HOME/lib:/usr/local/lib:/lib:/usr/lib` as the default fallback.

So every library Wine dlopens goes silently missing in any process that does not
inherit `DYLD_FALLBACK_LIBRARY_PATH`. The failures are quiet and look like
unrelated application bugs:

| library | what it looks like when missing |
| ------- | ------------------------------- |
| vulkan | `vkEnumerateInstanceVersion` absent → caller assumes Vulkan 1.0 → Steam decides the machine has no GPU |
| freetype (win32u) | no GDI fonts in spawned processes |
| freetype (dwrite) | **no text at all in DirectWrite apps** — every call returns `S_OK`, `GetAlphaTextureBounds` just reports `0x0`, so Skia is told the glyph is empty and never rasterises |
| gnutls | no TLS; `crypt32` and `secur32` both fail |

**The fix is bundling, not patching.** `bin/bundle-libs` copies the libraries
into `runtimes/<name>/lib/bundled` together with their transitive dependencies,
rewrites install names to `@loader_path`, and re-signs them ad hoc.
`bin/build-runtime` then seeds configure's soname cache:

```
./configure ac_cv_lib_soname_gnutls=<bundle>/libgnutls.30.dylib ...
```

`SONAME_LIB*` becomes an absolute path into the bundle, so **every** dlopen site
resolves — including ones nobody has hit yet — with no source change and no
environment variable. `build-runtime` fails the build if any soname comes out
bare, since that would silently produce a degraded runtime.

Verified with no environment set at all: glyph rasterisation returns a 12x12
box with 65 non-zero bytes, Vulkan reports 1.4.0, gnutls loads, and Steam's GPU
topology query returns `k_EGpuDriverId_MoltenVK` with exit code 0.

**Auditing for gaps.** Any `SONAME_*` left as a bare name is only safe if the
library lives in `/usr/lib`. Check with:

```sh
grep -E '^#define SONAME_' build/wine-<ver>-x86_64/include/config.h
env -u DYLD_FALLBACK_LIBRARY_PATH <test-program> libfoo.dylib   # reachable bare?
```

Currently bundled: vulkan, freetype, gnutls, SDL2 (and MoltenVK, pulled in as a
dependency). Left bare deliberately: `libcups` (ships in `/usr/lib`), `libodbc`
(not installed). SDL2 was found this way *after* the first bundling pass —
Wine uses it for gamepads, so without it controllers silently do not work.

`bin/gamerun` deliberately does **not** set `DYLD_FALLBACK_LIBRARY_PATH`.
Setting it would mask a bundle regression, and this class of bug is invisible
when masked: no error, just no Vulkan, or no fonts, or no TLS.

We previously carried five per-site patches hardcoding Homebrew paths. They
worked, but five sites of one pattern is a policy, not five bugs — they are
retracted, in `patches/retracted/`. This is also why CrossOver needs no such
patches: they bundle.

## 2. The workaround we still carry

**Wine's window surfaces are per-process.** `winemac.drv/surface.c` hands a
foreign `HWND` a throwaway surface:

```c
if (!(data = get_win_data(hwnd))) return TRUE; /* use default surface */
```

Chromium's software compositor runs in the **viz** process and blits the
rendered page into a window owned by the **browser** process. Wine discards
those pixels, so the page is drawn perfectly into a bitmap nobody displays —
a black window. Proven with `WINEDEBUG=+pid,+bitblt`: pid A creates and flushes
the surface while pid B performs all 459 content blits, including six full
700×440 paints.

This is **not a GPU problem**. It happens with the GPU fully disabled, because
the fault is the process boundary, not the renderer.

`--in-process-gpu` makes viz a *thread* of the browser process, so the blit
lands in the real surface. CEF appends that switch itself under CrossOver and
never for us; the trigger is still unknown (see "Open questions"). Steam
restores its own binaries on launch, so a shim executable cannot survive —
the switch is injected through Wine instead:

```
patches/wine-0004-kernelbase-append-args-env.patch
WINE_APPEND_ARGS="steamwebhelper.exe=--no-sandbox --in-process-gpu --disable-gpu"
```

**This is a workaround, not a fix.** The real fix is cross-process window
drawing in Wine.

**Update 2026-07-30: CrossOver has already implemented that fix, and it is
`CX HACK 23950`.** When a window's parent lives in another process, they hand it a
shared-memory-backed surface instead of Wine's throwaway `dummy_surface`, and
flush it by sending `WM_WINE_FLUSHSHMSURFACE` to the owning process, which maps
the section and blits it. In their own words:

```c
/* CX HACK 23950: provide a default shm surface for windows with parents in other process */
```

Scope: `win32u/dce.c`, `win32u/window.c`, `win32u/message.c`, `win32u_private.h`,
`include/ntuser.h`, `server/window.c`. Note this section previously called the fix
"a substantial upstream change" — it is substantial, but it exists and is on disk
at `src/crossover/sources/wine/`.

**So `wine-0004` and the whole `WINE_APPEND_ARGS` mechanism should become
deletable.** The first test after the rebase (see `docs/NEXT.md`, "The pivot") is
simply: does Steam render on a CrossOver-based runtime with `WINE_APPEND_ARGS`
unset? Untested so far.

A better renderer would **not** help, and this is settled rather than assumed:
`winemac.drv/opengl.c` carries the same per-process gate and states it outright —

```c
if (!(data = get_win_data(hwnd)))
{
    FIXME("DC for window %p of other process: not implemented\n", hwnd);
    return FALSE;
}
```

GDI fails silently with a throwaway surface; OpenGL fails loudly. So neither
full OpenGL support nor `mesa_gl_to_metal` would let Chromium run without
`--in-process-gpu`. The single condition is that Wine learn to present into a
window owned by another process — shared window surfaces or foreign-HWND
drawables, the same gap either way. Consistent with this, CrossOver has D3DMetal
available and still runs Steam in-process software.

---

## 3. Host state — the part that is *not* in this repo

Run `bin/provision-host` on a clean machine. It is idempotent and has a
`--check` mode.

- **MoltenVK's ICD carries `is_portability_driver: true`.** The Khronos loader
  hides portability drivers from applications that do not request
  `VK_KHR_portability_enumeration`. Steam does not, so it got
  `VK_ERROR_INCOMPATIBLE_DRIVER` and concluded there was no GPU.
  `provision-host` strips the flag from
  `/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json`.
  **Re-apply after any `brew upgrade molten-vk`** — `brew reinstall molten-vk`
  restores the original.
- **Do not set `VK_ICD_FILENAMES`.** The loader's default search finds MoltenVK
  correctly; pointing the variable at MoltenVK's own manifest is what *hides*
  it. `bin/gamerun` used to do this and it actively caused the failure above.
- Homebrew (x86_64, `/usr/local`): `molten-vk`, `vulkan-loader`, `freetype`,
  `gnutls`, `bison`, `mingw-w64`, `sdl2`.
- Homebrew (arm64, `/opt/homebrew`): `cabextract` (winetricks needs it).

---

## 4. Building the runtime

**`bin/build-runtime` does this** — it configures, applies `patches/series`
(skipping class `debug` unless `--with-debug`), builds and installs, and fails the
build if any soname comes out bare. The recipe below is kept because it documents
*why* each flag is there, and these traps each cost real time when they were
rediscovered:

- **`gcc -m64` means *arm64* on Apple Silicon.** Build tools compiled this way
  silently mix architectures and the link fails with "found architecture arm64,
  required architecture x86_64". Always pass `CC="gcc -arch x86_64"` — **to
  `configure` as well as `make`**. Passing it only to `make` lets configure's
  link tests run as arm64, which silently drops gnutls, freetype and vulkan.
- **`arch -x86_64` strips every `DYLD_*` variable** (it is SIP-protected). Use
  it for *building* only; never wrap the `wine` binary in it.
- **Do not edit `include/config.h` to change a soname.** It forces a global
  rebuild. Patch the source file instead — then only the affected module
  rebuilds.
- Configure line (from the working build):

```
--build=x86_64-apple-darwin --enable-archs=i386,x86_64
--prefix=<runtimes/wine-VERSION-x86_64> --disable-tests
--with-vulkan --with-mingw --with-freetype --with-gnutls --without-x
```

Apply the patches listed in `patches/series` (skip class `debug`), then build.
Only the touched module needs rebuilding, e.g.
`make CC="gcc -arch x86_64" dlls/win32u/win32u.so` — but note the host tools are
rebuilt too, so a stray `make` outside the x86_64 shell will poison the tree
with arm64 objects.

`patches/series` classifies each patch:

| class | meaning |
| ----- | ------- |
| `fix` | upstreamable; goal is to send it away and delete it here |
| `workaround` | ours to carry until upstream changes |
| `debug` | diagnostic only, not applied by default |

---

## 5. Prefixes

`bin/mkprefix <name>` creates one, mounts `S:`, and provisions corefonts +
`d3dcompiler_47` via a pinned winetricks (plus Wine Mono when the runtime has it).
`--no-provision` skips the winetricks step. **It has never been run end to end** —
`steam-base` predates it and was provisioned by hand — so expect to debug it the
first time. Note it does **not** install Steam; that is still manual.

The manual equivalent, for reference (fetch winetricks with `bin/get-winetricks`):

```sh
export WINEPREFIX=$PWD/prefixes/<name>
export WINE=$PWD/runtimes/wine-11.14-x86_64/bin/wine
export DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib:/usr/lib
export PATH="$PWD/runtimes/wine-11.14-x86_64/bin:/opt/homebrew/bin:$PATH"
bin/vendor/winetricks -q corefonts d3dcompiler_47
```

- `corefonts` — Wine ships only 6 fonts; Chromium needs real families.
- `d3dcompiler_47` — Wine's builtin is a vkd3d-shader wrapper; CEF needs the
  genuine Microsoft compiler. winetricks also registers the native override in
  the prefix registry, so nothing needs forcing at launch.

The Steam library is shared and lives outside every prefix, mounted as `S:`.
Never run two prefixes' Steam clients at once — they share `S:\steamapps`.

---

## 6. Diagnostics

`tools/probes/` — small Windows binaries that answer one question each from
*inside* a prefix. Build with `tools/probes/build.sh`.

| probe | question |
| ----- | -------- |
| `vkprobe` | which `vulkan-1.dll` loads, and what instance version it reports |
| `dwprobe` | does DirectWrite enumerate a system font collection |
| `dxgiprobe` | which GPU adapter does Wine report to the application |
| `glyphprobe` | **can DirectWrite rasterise a glyph at all** |
| `fontsmooth` | the font-smoothing parameters Skia reads |

Plus the surface dump (`patches/wine-0005`, class `debug`): set
`WINE_DUMP_SURFACE=<dir>` and render the raw BGRA with
`tools/probes/rawtopng.py`. This is what proved the text was never rasterised
rather than lost in presentation — see `docs/evidence/`.

**Method note.** Every genuine finding this project has produced came from
comparing against a working reference or measuring directly. Inferring causes
from an application's error messages produced a string of confident, wrong
theories; the refuted list in `NEXT.md` exists so they are not chased twice.

---

## 7. Open questions

- **Why does CEF add `--in-process-gpu` for CrossOver and not for us?** Their
  Wine source contains zero occurrences of the switch, so CEF decides it from
  something it probes. Ruled out: bundled MoltenVK, the Apple GPTK hook, bottle
  registry, environment variables, D3DMetal DLLs (not loaded), the AMD GPU
  identity spoof, and `wined3d`'s Vulkan renderer.

  **Chromium's GPU blocklist is not the mechanism either.** With
  `--vmodule=gpu_control_list*=3` Chromium names every rule it matches:

  | reported GPU identity | matched `gpu_blocklist` rules |
  | --------------------- | ----------------------------- |
  | Apple `0x106b` (ours) | 59, 90, 91, 122, 142, 164, 175, 182 |
  | AMD `0x1002/0x66af` (CrossOver's identity, spoofed onto ours) | 122, 124, 139, 144, 166, 167 |

  The sets differ, but **neither disables the GPU** — Chromium keeps it and
  spawns a GPU process in both cases, and the window is black either way
  (confirmed visually, not just from logs). So the AMD identity, which looked
  like the strongest lead for a long time, is definitively not the cause.

  That flag combination is the way to get observability here; it is worth
  reaching for first if this is picked up again. What remains unexplained is
  CEF's own decision logic, and the honest next step is probably reading
  `libcef.dll` around its `in-process-gpu` string rather than more black-box
  probing.
- **Why does our build of CrossOver's own published source not behave like
  their shipped binaries?** Same tree, but GPU identification falls back to a
  dummy card. Their build configuration or bundled dependencies differ in a way
  we have not identified.

---

## 8. Addons a source build does not include

A distribution Wine ships Wine Mono and Gecko; **building from source does
not**. `bin/get-addons` fetches them into `runtimes/<name>/share/wine/`, reading
the required versions straight out of `dlls/appwiz.cpl/addons.c` so they match
the runtime — Wine refuses an MSI whose version it did not ask for.

This matters more than it sounds. Plenty of games ship a small .NET launcher:
Strategic Command's `Launcher.exe` imports **only** `mscoree.dll` and
`user32.dll`. With no .NET runtime it exits **255, silently, with no window** —
which looks exactly like the game being incompatible rather than an addon being
absent. Steam's `console_log.txt` showed the tell: `Game process added` followed
by `Game process removed` two seconds later.

`bin/mkprefix` installs Mono into new prefixes when the runtime has it.

## 9. Launching Steam games

Two things that are not obvious:

- **Launch through Steam, not the game exe.** Running the game binary directly
  makes `steam_api.dll` re-launch it via `steam://run/<appid>` and exit — the
  original process returns 53 and nothing appears to happen. Worse, the hand-off
  goes through Wine's URL handler, which does **not** inherit our environment,
  so the Steam it starts has no `WINE_APPEND_ARGS` and its window is black.
  Profiles therefore use `EXE=steam.exe` with `ARGS="-applaunch <appid>"`.
- **`WINE_APPEND_ARGS` is set by `bin/gamerun` for every launch**, not per
  profile. The injection is keyed on image name so it is inert for anything that
  is not `steamwebhelper.exe`, and scoping it to the Steam profile only meant
  any game that brought up the client got a black Steam.

Also note Steam creates a `SteamLibrary/steamapps/` tree under whatever mount
point is registered, so the shared library path is `S:\SteamLibrary\steamapps\`.
Mounting `S:` is not enough on its own — the drive must be added in the client
(Settings > Storage > '+'), or games install into the prefix instead.
