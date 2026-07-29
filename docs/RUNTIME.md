# The runtime: what we changed, and why

How to rebuild this environment on a clean machine, and — more importantly —
*why* each deviation from stock exists. `PROJECT.md` holds design and decisions;
`NEXT.md` holds what to do next; this file holds the runtime itself.

Everything here was measured, not assumed. Where a belief turned out to be
wrong, the correction is recorded rather than quietly dropped, because the wrong
beliefs cost more time than the right ones.

---

## 1. The one bug that mattered most

**Wine on macOS `dlopen`s host libraries by bare soname.** A bare soname
resolves only through `DYLD_FALLBACK_LIBRARY_PATH`, and dyld's default fallback
list does not cover Homebrew. Any process Wine spawns *without* that variable
inherited therefore sees the library as absent.

It appears in three independent places, and each fails differently and
**silently**:

| patch | site | what breaks |
| ----- | ---- | ----------- |
| `wine-0002` | `dlls/win32u/vulkan.c` | `vkEnumerateInstanceVersion` missing → caller assumes Vulkan 1.0 → Steam decides the machine has no GPU |
| `wine-0003` | `dlls/win32u/freetype.c` | no GDI fonts in spawned processes |
| `wine-0006` | `dlls/dwrite/freetype.c` | **no text in any DirectWrite application** |

The third is the nastiest. `dwrite` keeps its *own* FreeType handle, separate
from `win32u`'s, so fixing `win32u` repairs GDI text and leaves DirectWrite text
broken. And it does not report an error: `DWriteCreateFactory`,
`CreateFontFace` and `CreateGlyphRunAnalysis` all return `S_OK`, and
`GetAlphaTextureBounds` simply reports `0x0`. Skia asks for the glyph box, is
told the glyph is empty, and never rasterises. A Chromium app then draws its
entire UI — images, gradients, controls, even the blinking text caret — with no
text at all.

Reproduce in isolation, no Steam required:

```sh
tools/probes/build.sh
wine tools/probes/glyphprobe.exe
#  broken: GetAlphaTextureBounds(1x1) hr=0 bounds=(0,0)-(0,0)     0x0
#   fixed: GetAlphaTextureBounds(1x1) hr=0 bounds=(-1,-12)-(11,0) 12x12
#          CreateAlphaTexture(1x1)    hr=0 non-zero bytes = 65 / 144
```

**All three are upstreamable and none is specific to Steam, games, or macOS
gaming.** Any Wine-on-macOS user with Homebrew dependencies hits them. Sending
them upstream is tracked in `NEXT.md`.

Not a regression: pristine upstream Wine 11.0 fails identically. CrossOver
escapes it only by bundling FreeType so their bare soname resolves — their
`dlls/dwrite/freetype.c` is unmodified upstream.

---

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
surfaces in Wine, which is a substantial upstream change. A working hardware
compositing path (e.g. `mesa_gl_to_metal`) might sidestep it, since GPU
presentation does not go through the GDI blit — but that is unproven, and
notably CrossOver has D3DMetal available and still runs Steam in-process
software.

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

**Not yet scripted** — `bin/build-runtime` does not exist. Until it does, the
recipe below is the source of truth, and these traps each cost real time when
they were rediscovered:

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

`bin/mkprefix <name>` creates one. The winetricks provisioning below is
currently **manual** — folding it into `mkprefix` is still owed. Fetch a pinned,
checksum-verified winetricks with `bin/get-winetricks`, then:

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
