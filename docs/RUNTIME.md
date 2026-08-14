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

### ⚠️ The one place bundling makes things worse: SDL2

**Measured 2026-07-31, on both `wine-11.14-x86_64` and `winecx-26.3.0-x86_64`.**
Bundling SDL2 does not currently buy working gamepads — it buys a crash:

```
30  winebus.so   sdl_bus_init + 76
29  dyld         dlopen_from  ->  runInitializers / forEachInitializer
    *** NSInternalInconsistencyException: NSWindow should only be instantiated
        on the main thread!
    libc++abi: terminating due to uncaught exception of type NSException
```

`winebus` dlopens `libSDL2` from a worker thread; SDL2's dyld initializer touches
`NSWindow`, and macOS 26 makes that a hard exception instead of a warning. The
`winebus` service therefore dies at startup —
`Auto-start service L"winebus" failed to start: 1053` — so controllers do not work
*and* every prefix bring-up logs a crash that looks alarming and is unrelated to
whatever is being debugged at the time.

Not yet fixed, and there is a real choice to make:
- **drop SDL2 from the bundle** — honest, since gamepads do not work either way, and
  it removes a confusing crash from every log; or
- **keep it and make the dlopen safe** — the interesting fix, and the one worth
  sending somewhere: either `winebus` should initialise the SDL backend on the main
  thread, or SDL2's Cocoa initialisation should not run at `dlopen` time. This wants
  a look at which of the two is actually at fault before blaming either.

Do not "fix" it by removing SDL2 from `WANT` without recording which option was
chosen and why — the reason SDL2 is in that list at all is that its absence was
itself a silent failure.

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

**`CX HACK 23950` is NOT that fix. Tested 2026-07-31 — this section previously
claimed it was, and that was wrong.**

The hack is real and is in the base: when a window's *parent* lives in another
process, CrossOver hands it a shared-memory-backed surface instead of Wine's
throwaway `dummy_surface` and flushes it by sending `WM_WINE_FLUSHSHMSURFACE` to
the owning process. Scope: `win32u/dce.c`, `win32u/window.c`, `win32u/message.c`,
`win32u_private.h`, `include/ntuser.h`, `server/window.c`.

```c
/* CX HACK 23950: provide a default shm surface for windows with parents in other process */
```

But it does not address Steam, and three measurements say so:

1. **The pivot test failed.** On `winecx-26.3.0-x86_64` with `wine-0004` absent (so
   `WINE_APPEND_ARGS` is inert), Steam's client window is **black — no content at
   all**, confirmed visually.
2. **The path never runs.** With `WINEDEBUG=+win` and 6 `steamwebhelper.exe`
   processes alive: **0** `shm_surface`, **0** `FLUSHSHMSURFACE` — in a 3.5 MB log
   carrying 5,559 `trace:win:` lines from 78 wine pids, and one that *does* contain
   `CefBrowserWindow` and `Chrome_WidgetWin_0/1`, so the Chromium windows were
   covered. (Both the control and the window-class check matter: the first version of
   this measurement had neither, and a bare "0 hits" proves nothing.)
3. **CrossOver never touched the file that drops the pixels.**
   `winemac.drv/surface.c` is byte-identical to upstream 11.0 in their tree, and
   still reads `if (!(data = get_win_data(hwnd))) return TRUE; /* use default
   surface */`.

The reason is a distinction the earlier grep-based reading missed: 23950 keys on a
foreign **parent**, while Steam's black window comes from a foreign **painter** —
the window is owned by the browser process and drawn into by the viz/GPU process.
Different shapes; only the first is covered.

**So `wine-0004` stays**, now as one of our own deltas rather than a stopgap awaiting
23950. And the question this section declared closed is **open again**: why does CEF
run its GPU in-process under CrossOver (their bottle never spawns a
`--type=gpu-process`) and out-of-process for us? The flag, not the surface, is the
difference — so reading `libcef.dll` around its `in-process-gpu` string is still the
honest next step.

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
(skipping class `debug` unless `--with-debug`), builds, installs, and writes a
`PROVENANCE` file. It **fails the build** rather than shipping a silently-wrong
runtime when:

| check | the failure it prevents |
| ----- | ----------------------- |
| any soname comes out bare | no vulkan/freetype/gnutls, looking like app bugs (§1) |
| i386 PE float math is x87, not SSE | ~60x slower float code under Rosetta (below) |
| the source tree is not a git repo at its own root | `git apply` skips every hunk and exits 0 — an unpatched runtime that looks patched |
| the tree differs from its tag by anything outside `patches/series` | changes that vanish on a clean rebuild |
| `--winecx <ver>` disagrees with the installed CrossOver | an A/B reference that quietly lies |
| `PROVENANCE` is missing a field | an unlabelled runtime |

The recipe below is kept because it documents *why* each flag is there, and these
traps each cost real time when they were rediscovered:

- **⚠️ The PE compiler must emit SSE float math, not x87 — use clang.** Measured
  2026-07-31, and it cost most of an evening. With Homebrew's
  `i686-w64-mingw32-gcc`, our 32-bit PE DLLs compiled floating point to **x87**:

  | `i386-windows/dsound.dll` | x87 | SSE |
  | ------------------------- | --- | --- |
  | CrossOver's shipped build | 52 | **741** |
  | ours, mingw-gcc | **1334** | **0** |

  **Rosetta 2 emulates x87 badly and SSE well.** The consequence was not a build
  failure but a runtime that worked and was ~60x slower in float-heavy 32-bit code:
  `wine_dsound_mixer` burned a whole core (100% vs 1.5% with CrossOver's DLL) and the
  game's audio jittered audibly. GCC targeting i386 defaults to x87; clang targeting
  `i686-windows` defaults to SSE. 64-bit PE builds are unaffected — x86_64 mandates
  SSE2 — so this only ever bites 32-bit titles.

  `bin/build-runtime` now passes `--with-mingw=/usr/local/opt/llvm/bin/clang`
  (`configure.ac:414` accepts a path; `:466` derives `--target=i686-windows` itself),
  falls back to mingw-gcc **plus `-msse2 -mfpmath=sse`** if clang is absent, records
  `pe_compiler` in `PROVENANCE`, and **fails the build** if the i386 `dsound.dll` comes
  out without SSE. Override with `PE_CC=`.

  Check any runtime by hand:

  ```sh
  objdump -d runtimes/<rt>/lib/wine/i386-windows/dsound.dll | grep -cE '\b(mulss|mulps|addss|addps)\b'  # want >0
  objdump -d runtimes/<rt>/lib/wine/i386-windows/dsound.dll | grep -cE '\b(fmul|fadd|fld)[a-z]*\b'      # want low
  ```

  Upstream Wine recommends clang + lld for PE builds; mingw-gcc is the less-tested
  path, and this is what that costs. CrossOver's own configure prefers clang too.
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
--prefix=<runtimes/NAME> --disable-tests
--with-vulkan --with-freetype --with-gnutls --without-x
--with-mingw=/usr/local/opt/llvm/bin/clang        # or, with --pe-gcc:
--with-mingw  i386_CFLAGS='-g -O2 -msse2 -mfpmath=sse'
```

  plus `ac_cv_lib_soname_*=<absolute path into lib/bundled>` for each bundled
  library (§1). The exact line used for a given runtime is recorded in its
  `PROVENANCE`, along with `pe_compiler` and `deployment_target`.

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
`d3dcompiler_47` via a pinned winetricks, then Wine Mono from the runtime's
`share/wine/mono/`. `--no-provision` skips the whole provisioning step. It does
**not** install Steam; that is still manual, and mounting `S:` is not the same as
registering it as a Steam library folder — see `bin/mkprefix`'s own header.

**Run `bin/get-addons <runtime>` before `mkprefix`**, or there is no Mono MSI to
install. A source build ships none.

Measured end to end for the first time on 2026-08-01 (WineCX 26.3.0, wb-test):
31 fonts, Wine Mono 10.4.1, `S:` resolving. Note what an unprovisioned prefix
looks like, because it is not what the name suggests: `C:\windows\Fonts` is
**empty** — zero files, confirmed through Wine, not just on disk. Wine's own 56
stock fonts (`tahoma.ttf`, `symbol.ttf`, `marlett.ttf`, the `.fon` bitmaps …)
live in the *runtime's* `share/wine/fonts/` and are never copied into a prefix.
So a font count taken inside a prefix measures provisioning only, which is what
makes it a usable check.

### Verifying a prefix

Two checks that can actually fail, and the traps around them.

**Pairing — is this prefix the one that runtime booted?** `wineboot` copies the
runtime's PE builtins into the prefix, so their hashes match:

```sh
shasum -a 256 runtimes/<rt>/lib/wine/x86_64-windows/ntdll.dll \
              prefixes/<pfx>/drive_c/windows/system32/ntdll.dll   # must be equal
shasum -a 256 runtimes/<rt>/lib/wine/i386-windows/ntdll.dll \
              prefixes/<pfx>/drive_c/windows/syswow64/ntdll.dll   # and this pair
```

⚠️ **`wine --version` does not test this.** It reports what that binary is and
short-circuits before opening a prefix, so it passes identically against a prefix
built by a completely different Wine. Keep `PROVENANCE` alongside the hashes and
the chain is source → runtime → prefix, each link checkable.

**`S:` — is the mount real?** A dangling `dosdevices/s:` is the failure this
project keeps meeting: `ln -sfn` to a missing target succeeds, `ls -l` on the
result exits 0 and prints a normal-looking line, and only dereferencing fails.

⚠️ **Correcting an earlier claim of ours** (commit `de7ed14`) that
`wine cmd /c dir S:\` is "the meaningful check". It discriminates, but it does
**not** fail loudly, and the difference matters. Measured on WineCX 26.3.0:

| | good `s:` | dangling `s:` |
| - | --------- | ------------- |
| `dir S:\` output | volume header **+ file entries** | volume header, **no entries** |
| wine process exit status | 0 | **1** |
| `ls -l dosdevices/s:` | 0 | **0** — cannot fail |

So `dir S:\` on a broken mount still prints `Volume in drive S has no label` and
`Directory of S:` with no error text at all. Glance at the header and it looks
mounted. Use the process exit status, or better:

```sh
wine cmd /c 'if exist S:\<known-file> (echo FOUND) else (echo MISSING)'
```

⚠️ And do **not** reach for `%errorlevel%` to check it. Measured: `dir S:\ >nul
2>&1 & echo %errorlevel%` prints `0` for both the good and the dangling case,
because `cmd` expands `%errorlevel%` when it *parses* the compound statement,
before `dir` has run. That is another control that cannot fail — the same family
as the pipeline exit-code trap in `AGENT_START_HERE.md`.

Finally, if you break `s:` deliberately to run this control, restore it from a
shell `trap` on `EXIT INT TERM` and re-assert the positive case afterwards. An
interrupted negative control leaves a working prefix broken, and the damage
outlives the test — that has already happened here once, under build load.

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

`bin/mkprefix` installs Mono into new prefixes when the runtime has it. **This
sentence was false from the day it was written until 2026-08-01**, and it is worth
knowing why, because the shape recurs.

The lookup built its path as `"$(dirname "$BIN")/../share/wine/mono/"`. `$BIN` is
already `runtimes/<rt>/bin`, so that resolved one level too high —
`runtimes/share/wine/mono/` — and never matched an MSI that was sitting exactly
where `get-addons` had put it. The miss was then *fatal and silent*: `common.sh`
sets `set -eo pipefail`, so the empty `ls` at the head of
`mono_msi=$(ls <glob> 2>/dev/null | head -1)` exited 1, pipefail carried that past
the successful `head`, and `set -e` killed the script on the assignment. With
`2>/dev/null` swallowing the message, `mkprefix` simply stopped after "fonts
installed", printing no error and never reaching `prefix ready`.

The `else` branch immediately below the lookup exists to warn *"no wine-mono in
the runtime — run bin/get-addons"*. It was unreachable in precisely the case it
was written for: **a guard that can only fire when it is not needed.**

Fixed by naming the runtime root once (`RT_ROOT`) and replacing the pipeline with
a glob loop, which cannot fail under `set -e` and so keeps the branch reachable.
See the pipeline-exit-code rule in `AGENT_START_HERE.md`.

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

A `*-direct.env` debug profile needs a **`steam_appid.txt`** in the game
directory containing the appid, or the exe exits 53 as described above even with
the client already online. Both games here have one.

### ⚠️ `BACKEND=` in a profile does NOT reach a Steam-launched game

This is the consequence of "does not inherit our environment", and it went
unnoticed for as long as it did because nothing needed a backend until
2026-08-15. It is not a subtlety — on macOS it is the difference between a game
running and not running at all.

`BACKEND=` works by exporting `WINEDLLOVERRIDES`, which is inherited down a
process tree. A Steam game's real parent is **the Steam client**, by either
route:

- the profile launches `steam.exe -applaunch <appid>`, and if a client is
  already running that call just forwards the request to it; or
- `steam_api` re-launches the exe through `steam://run/<appid>` (the exit-53
  path above).

Either way the game is spawned by the client — and `profiles/steam.env` sets
`BACKEND="none"`. So the game gets **builtin d3d11, i.e. wined3d**, which on
this platform caps at feature level 9_3 and fails device creation outright with
`0x80004005` → *"Failed to initialize graphics"* (section 10). Measured: with
DXMT installed and `BACKEND="dxmt-…"` set in its own profile,
`bin/gamerun iron-nest` still failed against a running client.

It is easy to be fooled here, and we were. A direct launch into a *warm* Steam
session is a real child of `gamerun`, inherits the environment, and works — so
the backend looks fine until the launch happens to go through Steam. The tell
that it went through Steam is the client's **"Launch Game with custom
arguments"** dialog, which only appears when Steam itself is starting the game.

**The fix is Wine's per-application key**, which is keyed on the executable and
so applies however the process is started. It is consulted *before* the global
`DllOverrides` key (`dlls/ntdll/unix/loadorder.c`):

```
HKCU\Software\Wine\AppDefaults\<Game.exe>\DllOverrides
    "dxgi"      = "native,builtin"
    "d3d11"     = "native,builtin"
    "d3d10core" = "native,builtin"
```

`bin/backend-install --app '<Game.exe>'` writes exactly that from the backend's
own `BACKEND_OVERRIDES`, and asserts it by reading it back through Wine — note
`regedit` exits 0 even when it writes nothing, and the registry is not flushed
to `user.reg` while a wineserver is live, so grepping the file is not a check.

Verified with the strongest available control: `BACKEND=none`, i.e. **no
environment overrides at all**, launched via `-applaunch` against a running
client → `Direct3D 11.0 [level 11.1]`.

> **Rule: a Steam game that needs a backend needs the AppDefaults key.**
> Setting `BACKEND=` in its profile is necessary for direct launches and not
> sufficient for anything Steam starts.

**Confirmed across engines, which is what makes the rule trustworthy.** The
same before/after on XCOM: Chimera Squad (Unreal Engine 3), a title that got
none of Iron Nest's debugging:

| | AppDefaults key | outcome |
| - | --------------- | ------- |
| Iron Nest (Unity 6) | absent | `d3d11: failed to create device and context (80004005)` |
| Chimera Squad (UE3) | absent | **"Minimum of DirectX 11 Required."**, process idles at 0–1% CPU |
| either | present | runs — 11.1 / 2K logo, fullscreen, ~158% CPU |

Two different engines phrase the failure differently and fail for one reason.

⚠️ **The key goes on the executable that creates the D3D device**, which is not
always the one you first see. Chimera Squad ships `Launcher/launcher.exe`
alongside `Binaries/Win64/XCom.exe`; the key belongs on `XCom.exe`. (2K have
since removed that launcher — see `2kLauncherRemoved.txt` in the game directory
— but the trap stands for any title with a launcher, and a launcher that ships
`QtWebEngineProcess.exe` is Chromium, i.e. the same class as the
`steamwebhelper` black-window problem.)

Use `--app-only` to register a further game against a backend that is already
installed. It writes the registry through the running wineserver and copies no
DLLs, so unlike a full install it is safe against a live prefix and needs no
Steam restart.

### An `-applaunch` sent too soon after login can be swallowed

Measured twice on 2026-08-15: a `steam.exe -applaunch <appid>` issued ~5 s after
an **already-running** client finished logging in produced **nothing at all** —
no game process, no entry in `logs/gameprocess_log.txt`, no error. The request
was simply dropped.

⚠️ But this is not universal, and an earlier version of this section said
"silently drops" flatly, which overstates it. When **no client is running**,
`bin/gamerun <game>` starts one and the launch is *queued*, firing correctly once
the bootstrap finishes — verified 2026-08-15 with a launch that sat for ~2
minutes and then ran. The failing case is specifically "client already up and
just logged in"; the working case is "client not running at all".

Either way, do not assume the command took: confirm with the
`adding PID … as a tracked process` line in `logs/gameprocess_log.txt`.

### A cold Steam start in this prefix takes about two minutes

And it looks like a hang while it does, because most of it is
`Verifying all executable checksums` in `logs/bootstrap_log.txt` — **I/O-bound,
so the process sits near 0% CPU**. Do not read low CPU as "stuck" here; that
mistake was made on 2026-08-15 and the client came up fine a minute later.

The distinction that actually separates working from hung, for any process in
this workbench, is **accumulated CPU time, not instantaneous `%CPU`**:

```sh
ps -o pid=,%cpu=,etime=,utime=,stime= -p <pid>
# utime+stime climbing in step with etime = busy, whatever %CPU says
```

`ps`'s `%CPU` on macOS is a decaying average and reads low for a process that is
genuinely working — a game 33 s into loading showed `13.9%` while having burned
31 s of CPU in those 33 s.

## 10. D3D11 on macOS: wined3d cannot do it, and never will

**Measured 2026-08-14, on WineCX 26.3.0 with Iron Nest (Unity 6, D3D11).** This
is a property of the platform, not of a game or of our build, so it belongs
here rather than in `NEXT.md`.

wined3d does select the Vulkan renderer on macOS — that is CodeWeavers'
`CW HACK 18311` in `dlls/wined3d/directx.c:3531`, which tries
`wined3d_adapter_vk_create` first and logs
`err:winediag:wined3d_adapter_create Using the Vulkan renderer for d3d10/11
applications.` It gets a real adapter, correctly identified
(`Apple M1 Max`, `106b:1a050207`, 64 GB). And it is still useless for D3D11:

```
trace:d3d:wined3d_driver_info_init GPU maximum feature level 0x9300     <- 9_3
fixme:winediag:wined3d_select_feature_level None of the requested D3D feature
    levels is supported on this GPU with the current shader backend.
```

`wined3d_state_create` (`dlls/wined3d/stateblock.c:2450`) returns `E_FAIL` when
no requested level is `<=` the adapter's, so `D3D11CreateDevice` fails with
`0x80004005` for any application asking for 10_0 or above — which is all of
them.

The cap comes from `feature_level_10_supported()` (`adapter_vk.c:2213`), and
three of its eleven required Vulkan features are missing in MoltenVK. Measured
with `vulkaninfo` against the x86_64 MoltenVK the runtime bundles (the bundled
copy differs from `/usr/local/opt/molten-vk`'s by 1056 bytes, all
`install_name_tool` re-pathing, so the query is representative):

| feature | MoltenVK | needed for FL 10 |
| ------- | -------- | ---------------- |
| `geometryShader` | **false** | yes |
| `pipelineStatisticsQuery` | **false** | yes |
| `shaderCullDistance` | **false** | yes |
| the other eight | true | yes |

**`geometryShader` will never become true** — Metal has no geometry-shader
stage. So this is not a version problem to wait out.

**Consequence: every D3D10/11 title needs a translation layer.** For this
workbench that means DXMT, and it is a requirement rather than a performance
option. With `BACKEND="dxmt-0.80"` the same game reports
`Direct3D 11.0 [level 11.1]` and plays.

### D3D12 is not an escape hatch either

Forcing Unity to D3D12 (`-force-d3d12`) gets further than expected — vkd3d
reports **feature level 11.0**, well above wined3d's 9_3 — but Unity's D3D12
backend then needs the D3D11-on-12 interop device, and

```
dlls/d3d11/d3d11_main.c:381   D3D11On12CreateDevice   FIXME "... stub!"  -> E_NOTIMPL
```

is a stub in CrossOver's tree, so it fails with `0x80004001` and Unity falls
back to plain D3D11, which fails as above.

### The DXMT packaging trap

DXMT had never once loaded here, and the reason was ours, not DXMT's: a
**unixlib's PE half must be installed into the runtime as well as the prefix.**
Full mechanism and the misleading error message are in `backends/README.md`;
`bin/backend-install` now handles it. Worth stating the refuted hypothesis too —
DXMT 0.80 is built against Wine 11.14 and this runtime is Wine 11.0, and that
mismatch was *predicted to be the blocker and was not*: `winemetal.so` loads
into the 11.0 runtime without complaint.
