# Continue here

> **New session?** Read `README.md` for what this project is, then
> `AGENT_START_HERE.md` for how to operate it — both are durable and do not change.
> **This file is the only one that tracks current state**, so it is where anything
> time-varying belongs: what is working now, what is open, and what has been refuted.

## Current state (2026-08-14)

- **Two games play.** Strategic Command WWII (32-bit, wined3d) and **IRON NEST:
  Heavy Turret Simulator** (64-bit Unity 6 / D3D11, **DXMT**) — the latter added
  2026-08-14, first mission played through, reported working well. The D3D11
  class is therefore open, and the finding that opened it is durable enough to
  live in `RUNTIME.md` §10 rather than here:

  > **wined3d cannot create a D3D11 device on macOS, for any game, ever.**
  > MoltenVK reports `geometryShader` / `pipelineStatisticsQuery` /
  > `shaderCullDistance` = false; `adapter_vk.c:2213` needs all three for
  > feature level 10; the adapter caps at **9_3** and `D3D11CreateDevice`
  > returns `E_FAIL`. Metal has no geometry-shader stage, so waiting does not
  > fix it. **DXMT is a requirement for D3D10/11 titles, not an optimisation.**

  Also settled on the way: D3D12 is not an alternative either —
  `D3D11On12CreateDevice` is an `E_NOTIMPL` stub in the CX tree, so Unity's D3D12
  backend cannot build its interop device. (vkd3d itself reports feature level
  11.0, much healthier than wined3d's 9_3.)

- **`bin/backend-install` had a real bug, now fixed:** a unixlib's PE half must be
  installed into the **runtime**, not only the prefix, or Wine cannot pair
  `winemetal.dll` with `winemetal.so` and the process dies at startup with
  `err:module:loader_init "winemetal.dll" failed to initialize` / `c0000142`.
  This is why `backends/dxmt-0.80` had never loaded once. Mechanism in
  `backends/README.md`. **Refuted while chasing it:** the predicted blocker was
  DXMT being built against Wine 11.14 versus this runtime's 11.0 — that mismatch
  is fine, `winemetal.so` loads into 11.0 without complaint.

- **DXMT is rebuildable, and now targets Wine 11.0.** `bin/build-backend dxmt`
  builds `src/dxmt` against `build/<runtime>` and writes a `PROVENANCE` like a
  runtime's. `backends/dxmt-0.80-winecx-26.3.0-sse` is the result, installed and
  **verified running the game** (`Direct3D 11.0 [level 11.1]`, 6/6 cold launches).
  Three of its five artifacts are byte-size identical to the 11.14-built ones;
  only `winemetal.dll` differs, which is exactly the file generated against
  `wine_build_path` — so the retarget changed the unixlib shim and nothing in the
  D3D translation itself. `backends/dxmt-0.80` is kept as the fallback.

  Two traps found by doing it, both worth more than the retarget:
  - **the meson pin is load-bearing.** Homebrew's 1.10.1 dies configuring DXMT's
    cross build with `KeyError: ... build.cpp_importstd` / "Unhandled python
    exception". `build-backend` recreates `toolchains/venv-meson` at **1.9.1**
    itself rather than trusting whatever is on `PATH`.
  - **`toolchains/` was a third casualty of the checkout rename.** A venv's
    scripts are shebanged with an absolute path to their own interpreter, so
    `toolchains/venv-meson/bin/meson` had been dead since the rename. See
    `AGENT_START_HERE.md`, "A built tree is not relocatable" — `toolchains/` is
    now named there alongside runtimes, prefixes and build trees.

- **✅ SOLVED (2026-08-15): the "intermittent" D3D11 failure was `BACKEND=` not
  reaching Steam-launched games.** Not a flake, not DXMT, not the install. A
  Steam game's parent is the Steam client, `WINEDLLOVERRIDES` does not cross that
  hand-off, and `profiles/steam.env` sets `BACKEND="none"` — so the game came up
  on wined3d, which caps at 9_3 and fails with `0x80004005`. Same error code as
  the wined3d baseline measured hours earlier, which should have been the tell.

  **Fixed with Wine's per-application key**, keyed on the exe so it survives any
  launch path: `bin/backend-install --app '<Game.exe>'` writes and asserts
  `HKCU\Software\Wine\AppDefaults\<exe>\DllOverrides`. Verified with the
  strongest control available — `BACKEND=none`, no environment overrides at all,
  via `-applaunch` against a running client → `Direct3D 11.0 [level 11.1]`.
  Full write-up in `RUNTIME.md` §9.

  **How it was actually found, because the method is the lesson.** Gil watched
  the screen and reported the sequence: Steam came up, a dialog asked *"Launch
  Game with custom arguments"*, he clicked Continue, and the failure followed.
  That dialog only appears when **Steam itself** is starting the game — i.e. it
  was direct evidence that our process was not the one running it. Both of my
  hypotheses (a `cp -f` inode hazard; something in the rebuilt DXMT) were wrong,
  and both were about the thing I had just changed rather than about the
  observation. **The user's description of what the screen did was worth more
  than every log I read**, and the answer was already in this repo's own docs
  (`RUNTIME.md` §9, "does not inherit our environment") — written down, but only
  as a reason to prefer `-applaunch`, never as a constraint on backends.

  Three controls in this session failed to detect what they were aimed at: the
  6/6 flake test (warm Steam, wrong condition), the first `ps -M` parse (wrong
  field mapping), and an `echo $?` after a pipeline (reported `head`'s status).
  *A control that cannot fail is not a control* — the rule earned its place
  again.

  Retracted with it: **"DXMT rebuild verified running the game" was too broad.**
  It verified the direct-launch path only; the `-applaunch` path was broken at
  the time that was written.

- **DXMT performance: first profile taken, no pathology.** Iron Nest in-game,
  `ps -M` on the game pid correlated with `sample`:

  | %CPU | PRI | thread |
  | ---- | --- | ------ |
  | 86.7 | 31T | main loop |
  | 22.3 | **63R** | `dxmt-encode-thr` (Metal command encoding) |
  | 11.4 | 31T | `Job.Worker 1` |
  | 11.1 | 31T | `UnityGfxDeviceWorker` |
  | 5.1 | **63R** | `FMOD mixer thread` |

  Process totals `utime 1:52.80 / stime 0:28.55` (~80% userspace) over 93–96
  threads. **Nothing is spinning**, and both realtime-band threads stay in `63R`
  rather than being demoted to `63T` — the exact inverse of the dsound
  pathology in section 4 of `RUNTIME.md`. The higher system-time share than
  Strategic Command's 98.8% userspace is expected: Metal command submission is
  syscalls, and SC is a 2D title.

  ⚠️ Caveat on the names: `ps -M` saw 93 threads and `sample` 97, so the index
  alignment can be off by a few and the names are **indicative, not measured**.
  The two `63R` rows are the trustworthy ones — realtime-band rows are rare and
  the names landing on them are the two subsystems that deserve that band. A
  clean correlation needs both captures on a quiescent process.

- **⚠️ Backends had no provenance, and DXMT was not rebuildable.** Fixed above by
  `bin/build-backend`; kept here because the *shape* of the gap is the lesson.
  Found
  while asking the reasonable question *"should we retarget DXMT at 11.0?"* — the
  retarget is the cheap half. Measured state:

  | | |
  | - | - |
  | upstream source | `src/dxmt`, tag **v0.80**, commit `589adb7` — pinned, but `src/` is gitignored |
  | our delta | `patches/dxmt-0001-include-iomanip.patch`, in `patches/series` (class `fix`), and the working tree matches it exactly ✅ |
  | the build command | **only in `build/dxmt-build2.log`** — gitignored, and prose in `backend.conf`, which is gitignored too (`/backends/*/`) |
  | `wine_build_path` it used | `/Users/<user>/games/build/wine-11.14-x86_64` — the **pre-rename** path, and that tree is **deleted** |

  A clean checkout could not produce a backend at all, and losing
  `backends/dxmt-0.80/` would have taken the D3D11 capability with it. Every
  runtime carried a `PROVENANCE`; backends carried nothing equivalent. **The fix
  was a recipe, not a rebuild** — and once there was one, its natural
  `wine_build_path` was `build/winecx-26.3.0-sse-x86_64`, so the Wine 11.0
  retarget fell out for free rather than being its own task. Worth remembering
  the shape of that: the question asked was "should we retarget?", and the
  retarget turned out to be five minutes of a job whose real content was that
  the thing could not be built at all.

- **Runtime:** `runtimes/winecx-26.3.0-sse-x86_64` — WineCX 26.3.0 (CrossOver 26.3.0 =
  Wine 11.0), built with `--pe-gcc`. It is `DEFAULT_RUNTIME` and is named in every
  profile. It now also carries DXMT's `winemetal.{dll,so}`, recorded in its
  `.backend-installs`. Built from `src/crossover/sources/wine`, which is its own git repo: pristine
  tarball on branch `vendor` at tag `winecx-26.3.0`, our work on `games`.
- **Prefix:** `prefixes/steam-winecx`, an APFS clone of `steam-base` with `wineboot -u`
  run on it — which preserved the Steam login and the `S:` library registration, the two
  steps only a human can redo. **Wine Mono was swapped 11.2.0 → 10.4.1 on 2026-08-01**:
  it had been inherited from the 11.14-era lineage, so the prefix was running a Mono its
  Wine never asked for (`addons.c` requests 10.4.1 on this 11.0 base). Verified
  functionally, not just in the registry — the game's .NET `Launcher.exe` came up and the
  game played. A broken .NET exits 255 silently with no window, so the launcher appearing
  *is* the test.
- **Also here:** `prefixes/control`, made by `bin/mkprefix` after its fix, as proof the
  fix reproduces in this tree (31 fonts, Mono 10.4.1, `S:` resolving by exit code). It has
  no Steam in it and is a clean starting point for a per-game prefix.
  `prefixes/steam-winecx.pre-mono` is the rollback clone from the Mono swap — deletable
  now that the swap is proven.
- **Patches:** `wine-0004` (`workaround`; this base needs it) and `wine-0005` (`debug`).
  The four ports of CodeWeavers' hacks are in `patches/superseded/`.
- **One runtime, one prefix.** The upstream `wine-11.14-x86_64` fallback, the
  `winecx-26.3.0-clang-x86_64` experiment, the degraded `wine-11.0-x86_64`, and the
  `steam-base` / `steam-cxbuild` / `steam-cxtest` prefixes were all deleted on
  2026-08-01 (~21 GB) when the checkout was renamed to `wine-workbench-macos`: each had
  the old absolute path baked in, and a stale one fails *silently* (no Vulkan, no fonts,
  no TLS). Rebuilding beats carrying them.
  - upstream 11.14 is reproducible from `src/wine` **plus** restoring
    `wine-0006/0007/0008` from `patches/superseded/` into a series — it is not a
    one-command rebuild, which is the main reason to think twice before wanting it back.
  - the clang runtime is one documented command (see the clang section below) plus
    ~25 minutes.

**Working:** Steam renders and logs in; Strategic Command WWII reaches its menu with
real-time menu response, clean audio and smooth loading, and plays a turn; Iron Nest
runs on DXMT at feature level 11.1 and plays a mission.

## Next: the four directions on the table

Item 3 is **done, and it answered a bigger question than it asked** (see Current
state). Everything here is a live option, not a backlog — pick by what you want to
learn.

**1. Play Strategic Command and polish.** The environment works; find the remaining minor
issues. Cheap, and it carries a free measurement with it: **the 2 s → 20 s menu fade is
still unmeasured on the current build**. Loading got smoother when the x87 float bug was
fixed, so the fade may have gone with it. Re-measure before theorising — that is the last
known symptom from the WineCX rebase.

**2. Controller support / the SDL2 `winebus` crash** (`RUNTIME.md` §1). Gamepads do not
work: `winebus` dlopens our bundled `libSDL2` from a worker thread, SDL2's dyld
initializer touches `NSWindow`, macOS 26 turns that into a hard exception, and the service
dies with `1053`. Well-scoped, and there is a **reference oracle** — check whether
gamepads work in the CrossOver bottle. If they do, their build avoids it and the
difference is findable. Unlocks a class of games (Sekiro and similar).

**3. ✅ DONE (2026-08-14) — the D3D11 class, via Iron Nest.** It was the highest
information per hour on the list, as expected, but not in the expected way: the
result was not "does this game work" but *"wined3d can never do D3D11 here, so DXMT
is mandatory"* (Current state, and `RUNTIME.md` §10). **Quasimorph and Battleship
Command no longer need to be tried to answer that question** — they are 64-bit
Unity/D3D11 too and would reproduce the same `0x9300` log line for line. Install them
when you want to *play* them, or as breadth-testing for DXMT; there is no open
question left that they are the cheapest way to answer.

**The D3D12 question is separate and unanswered.** DXMT is D3D10/11; nothing here
has established how far **D3D12** gets. What is measured so far, from the Iron
Nest session: vkd3d enumerates the real adapter and reports **feature level
11.0** — much healthier than wined3d's 9_3 — but `D3D11On12CreateDevice` is an
`E_NOTIMPL` stub in the CX tree, so a Unity D3D12 build cannot build its interop
device. Neither of those facts tells us whether a natively-D3D12 game renders.

The probe to use is **Clair Obscur: Expedition 33** (UE5, D3D12-only, 64-bit) —
Gil's suggestion, and the right one: it is D3D12 with no D3D11 fallback, so it
cannot silently take the path we already understand. Expect it to fail: UE5
generally wants feature level 12_0 / SM6 and vkd3d is reporting 11.0. That
failure would still be worth having, because *"D3D12 reaches feature level 11.0
and here is the specific thing that stops it"* is a measurement, and
*"D3D12 probably doesn't work"* is not. ⚠️ Check ownership first — unlike XCOM 2
and Chimera Squad, appid 1903340 does not appear in the prefix's
`userdata/*/config/localconfig.vdf`.

What is genuinely open in DXMT now, in cost order:
  - **Breadth.** One game at feature level 11.1 is one data point. Iron Nest is a
    modest Unity 6 title; something heavier would exercise more of DXMT.
  - **`ID3D11Fence` is not implemented** — `GpuFence::Create(): Failed to create
    ID3D11Fence, error 0x80004005` in Unity's log. Non-fatal here (Unity carried on
    and the mission played), but it is a real gap and a title that needs fences will
    notice.
  - **DXMT 0.80 is built against Wine 11.14, not the CX tree.** It works anyway, so
    this is hygiene rather than a bug — but the rebuild against
    `src/crossover/sources/wine` is the thing that makes the backend reproducible
    from this repo alone.
  - **Performance is unmeasured.** Nothing here has profiled a DXMT title; the
    `ps -M` / phase-matching method from the dsound work applies directly.

**4. Task Force Admiral + `wine_mesa_gl`.** The reason the workbench exists. The bridge
(GL → Mesa/zink → Vulkan → Metal) already runs with rough edges as its own project; what
stalled it was having no distributable frame, no CrossOver source to build against, and no
reproducibility — all three of which now exist and have been tested by a clean checkout.
Rebuilding it on this base is the thing that converts the workbench from plausible to
proven. Expect real work: the winemac.drv side has to be re-derived against CrossOver's
tree rather than upstream 11.2, and there is a known unsolved issue at the end (Metal
texture-view sampling, which forced post-processing off).

**Also open, smaller:**

- **clang as the PE compiler** — the better long-term toolchain (CrossOver's and
  upstream's choice; brings `-fms-hotpatch`), blocked on a Steam-login regression:
  `EConnect` retried every 50 ms with zero TLS errors. Rebuild that runtime to debug it.
- **Why CEF runs its GPU in-process under CrossOver and not for us** — reopened; see
  `RUNTIME.md` §2. Closing it would delete `wine-0004` and leave zero functional patches.
- **Installing Steam into a prefix is still manual**, as is registering `S:` in the
  client. `bin/mkprefix` itself now runs end to end (verified 2026-08-01).

**Not a target: upstreaming to Wine.** CodeWeavers is the audience; if CrossOver already
carries a hack for something, there is nothing to report. See `AGENT_START_HERE.md`,
"Contributing outward".

## ⚠️ THE PIVOT — read this before anything else (decided 2026-07-30)

### DECISION

**Our base is WineCX — CodeWeavers' published CrossOver Wine source — not upstream
Wine.** Do not start new work against `src/wine` (upstream 11.14).

`patches/series` keeps its role, but from now on it holds **only our own deltas on
top of WineCX**. If a patch there is a port of a `CW HACK`, that is a smell: it
means we are re-deriving, and the answer is to take it from the base instead.

**VERSION: WineCX 26.3.0.** Decided 2026-07-31, and **the versions now agree**:
CrossOver.app is `26.3.0.39832`, the source is `crossover-sources-26.3.0.tar.gz`,
and the `Steam` bottle migrated itself to `26.3.0.39832` when it was opened under
the new app (`cxbottle.conf` `"Version"`), so the A/B reference is exact.

Stronger than a version match, the source is demonstrably the tree that app was
built from: the bottle records build timestamp `20260715T103801Z`, and every file
in `src/crossover/sources/wine` carries mtime `Jul 15 12:37` local — the same
moment.

**That matching is a hard rule, not a nicety.** The A/B reference against a working
bottle was decisive **four separate times** in a single session — the `lretq` diff,
the `WINEMSYNC=1` discovery, the `winmm/time.c` null result, and the paint hacks.
Every real finding came from it. So:

> **Rule: the WineCX source version must equal the installed CrossOver.app version.**
> When one is upgraded, upgrade the other in the same change.

`bin/build-runtime --winecx <ver>` now enforces this: it refuses to build when the
installed app disagrees (`--allow-cx-mismatch` overrides, and the mismatch is then
recorded in `PROVENANCE`). Check by hand with:

```sh
/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' \
  /Applications/CrossOver.app/Contents/Info.plist        # 26.3.0.39832
cat runtimes/winecx-*/PROVENANCE                          # crossover_version: 26.3.0
grep '"Version"' ~/Library/Application\ Support/CrossOver/Bottles/Steam/cxbottle.conf
```

Use `CFBundleVersion`, not `CFBundleShortVersionString` — the short string is only
`26.3` and cannot distinguish point releases.

### Why this base, in one sentence

The objective is for our own macOS work — `wine_mesa_gl` and similar — to be
**adopted by the people who actually ship Wine on macOS, i.e. CodeWeavers**. You do
not get adopted by developing against a tree they do not ship. Their tree is both
the realistic integration target *and* the only one load-tested on this platform, so
it is the base. Upstream-Wine purity was never the goal and cost us two days of
re-deriving fixes CodeWeavers had already made.

### Terminology, because three version numbers get confused

- **CX = CrossOver, CW = CodeWeavers.** Both appear as source markers, inconsistently:
  `CW HACK` (91), `CW Hack` (76), `CrossOver Hack` (56), `CX HACK` (28), `CXHACK` (20).
  Their shipped binaries are `cx`-prefixed throughout (`cxbottle.conf`,
  `cxinstallfonts.exe`, `cxkeepalive.exe`, …) and the build tag is `cxoffice-26.0.0rc2`
  — CrossOver launched as *CrossOver Office*.
- **"WineCX-nn" is the community's name, not CodeWeavers'.** They only publish
  `crossover-sources-<ver>.tar.gz`. The label comes from the Wineskin → Kegworks →
  Sikarugir lineage, e.g. `WS12WineCX23.7.1` = Wineskin-12 + Wine-from-CrossOver-23.7.1.
  `PhoenicisOrg/winecx` tags them `winecx-25.1.0`.
- ⚠️ **The number in `WineCX-nn` is the CROSSOVER version, not the Wine version.**
  `WineCX-26.3.0` = CrossOver 26.3.0 = **Wine 11.0** base. `WineCX-23.7.1` = Wine ~8.x.
  We already tripped on this: `runtimes/wine-cx26.3-x86_64` reports `wine-11.0`, is
  named for CrossOver 26.3, and was built from source we never had.

**So adopt a naming + provenance convention with the rebase** — a directory name
cannot carry three facts:

```
runtimes/winecx-<cxver>-x86_64/PROVENANCE
  crossover_version: 26.3.0
  wine_version:      11.0
  source_tarball:    crossover-sources-26.3.0.tar.gz
  sha256:            <...>
  built:             <date>
```

`bin/build-runtime` should *write and assert* that, so provenance is checkable
instead of inferred from a folder name.

### Why

Every single problem solved in this repository's first two days had *already been
solved by CodeWeavers*:

| problem | already fixed in CrossOver as |
| ------- | ----------------------------- |
| WoW64 32→64 mode switch — a total blocker for **all** 32-bit games | `CW HACK 20760` |
| 2D/GDI menu latency (5–10 s per mouse hover) | `CW Hack 20969, 23427` |
| Steam black window / the whole `--in-process-gpu` saga | `CX HACK 23950` |
| Synchronisation overhead | msync (unmarked) |
| 2 s → 20 s menu fade (still open) | almost certainly one of theirs |

That is 100% re-derivation. It helps nobody, and it is not what this project is
for. The goal is a **niche macOS-specific library** (in the spirit of
`mesa_gl_to_metal`), not Wine upstream contributions — so "stay on latest upstream
so we can upstream things" was optimising for a virtue we do not need. **Latest
Wine is not the macOS standard; CrossOver's Wine is**, because it is the only tree
actually load-tested on this platform.

### Where to get the base

Researched 2026-07-30. **No maintained git repo tracks current CrossOver.**

| source | state |
| ------ | ----- |
| **CodeWeavers tarballs** — `https://media.codeweavers.com/pub/crossover/source/crossover-sources-<ver>.tar.gz` | ✅ **26.0.0, 26.1.0, 26.2.0, 26.3.0** all live (142 MB each), plus 25.1.0. Verified by HTTP HEAD. |
| `PhoenicisOrg/winecx` | active mirror, one git tag per release, but stops at `winecx-25.1.0` (last push Sep 2025). Useful only for diffing *across* CX releases. |
| `Gcenx/winecx` | **404, deleted.** Most search results still point here. |
| `tbodt/crossover-wine` | full history (102k commits), 5 stars, no version signal — abandoned. |
| MacPorts `wine-crossover` | Wine 4.0 / CrossOver 18. Dead end. |
| `benstonezhang/winecx_build` | *"build Wine on macOS with WoW64 from CrossOver source"* — `build.sh`, `brew_install.sh`. Pushed May 2025. **Worth reading before writing our own build.** |

Direct download works and needs no auth (substitute the version you settle on):

```sh
CXVER=26.3.0   # must equal the installed CrossOver.app version -- see the Rule above
curl -O "https://media.codeweavers.com/pub/crossover/source/crossover-sources-$CXVER.tar.gz"
```

**We already have it** — `src/crossover/crossover-sources-26.3.0.tar.gz`,
sha256 `ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872`,
extracted at `src/crossover/sources/wine` and now a git repository of its own:
branch `vendor` holds one pristine commit per CrossOver release (tagged
`winecx-26.3.0`), our work lives on branch `games`. The next CX upgrade is
therefore *extract onto `vendor`, tag, rebase `games`*.

The tree was verified pristine before committing — every file's size matches the
tarball's own listing. `src/crossover-sources-26.0.0.tar.gz` is kept only as the
previous release for cross-version diffing.

### Ecosystem context — who else does this, and how far behind they are

Worth knowing so we do not assume a community build already solves our problem:

| project | what it is | Wine it uses |
| ------- | ---------- | ------------ |
| **CrossOver** (CodeWeavers) | commercial; Wine part is LGPL and published | its own, **CrossOver 26 = Wine 11.0 + D3DMetal 3.0** |
| **Sikarugir** (ex-Kegworks, ex-Wineskin) | the maintained free macOS wrapper; toggles for D3DMetal/DXVK/DXMT | **`WineCX23.7.1`** — CrossOver 23.7.1, ~Wine 8.x. Nearly 3 years behind. Engines built by **Gcenx**. |
| **Whisky** | was the popular SwiftUI wrapper | **discontinued/archived April 2025** — author's stated reason was that it siphoned sales from CrossOver, which funds macOS Wine. Not a base. |
| **Apple GPTK** | Apple's Game Porting Toolkit (D3DMetal) | CodeWeavers-derived; a dev tool, not an end-user base |
| **Gcenx/macOS_Wine_builds** | official WineHQ macOS packages | **upstream** Wine, not CX |
| **MacPorts `wine-crossover`** | port | **Wine 4.0 / CrossOver 18.** Dead. |

Takeaway: **nobody tracks CrossOver 26.x.** The newest community CX build is ~3
years old, and the one modern free wrapper (Whisky) is dead. So maintaining a
current WineCX base is genuinely unoccupied ground — which is the point of doing it
here rather than re-deriving fixes.

**Plan: fetch the matching `crossover-sources-<ver>.tar.gz`, commit that tree as the
base, and keep `patches/series` for our own deltas only.**

### THE REBUILD — where it actually stands (2026-07-31)

Goal: get back to **Steam running and Strategic Command playable** on a WineCX
26.3.0 base, and find out how much of our workaround pile is still needed. Expect
most of it to be unnecessary.

**Step 0 — prerequisites: DONE.** CrossOver `26.3.0.39832` installed, `Steam`
bottle migrated to the same version in place (do **not** rebuild that bottle — it
carries the login and the installed game, and CrossOver upgrades bottles itself).

**Step 1 — get the source: WAS ALREADY DONE, and the handover said otherwise.**
The 26.3.0 tarball was fetched on Jul 29 and extracted; earlier notes claiming "we
only ever had 26.0.0 source" were wrong, and so was the claim that
`runtimes/wine-cx26.3-x86_64` was misnamed — `build/wine-cx26.3-x86_64/config.status`
records `srcdir='../../src/crossover/sources/wine'`, so it *was* built from 26.3.0.
Its real defect is the one that matters: bare sonames, no bundled libs.

`src/cx26/` is deleted. `src/crossover-sources-26.0.0.tar.gz` is kept for
cross-version diffing — which produced a useful number: across the whole wine tree
(12,439 files) **only ~35 files differ between CX 26.0.0 and 26.3.0**, all in
`msctf`/IME, `user32/static.c`, `win32u/{input,main,message,window}.c` + `ntuser.h`
+ `server/window.c`, `winhttp`, `joy.cpl`, `advapi32`, `kernelbase/file.c`,
`ntdll/unix/{env,signal_x86_64}.c`. **Nothing** in `winemac.drv`, `wow64cpu`,
`wined3d`, `dxgi` or `winevulkan`. So a CX point-release bump is cheap, and
graphics work does not need to fear one.

**Step 2 — teach `bin/build-runtime` about WineCX: DONE.**
`bin/build-runtime --winecx 26.3.0` builds `runtimes/winecx-26.3.0-x86_64` from
`src/crossover/sources/wine` (override with `WINECX_SRC`, or `--src`/`--name` for
anything else), keeps the soname-seeding + `bundle-libs` step, and writes and
asserts `PROVENANCE`. CrossOver's tree needed **no** special configure flags — our
existing line builds it as-is, which is why the earlier CX build succeeded; the only
thing it was missing was the seeding. Three guards were added, each for a failure
that had already happened here or nearly did:

  - **the version rule has teeth** — it refuses to build when CrossOver.app
    disagrees with `--winecx` (`--allow-cx-mismatch` to override, recorded in
    `PROVENANCE`).
  - **the source tree must be a git repo at its own root.** ⚠️ This one is a silent
    killer: the patch loop uses `git -C "$SRC" apply`, and an extracted tarball
    sitting in this repo's gitignored `src/` resolves *upwards* to the outer repo,
    prepends the path prefix, finds the patch's `dlls/...` paths do not match, and
    prints `Skipped patch 'dlls/wow64cpu/cpu.c'.` **while exiting 0**. Every patch
    then reports as applied and nothing is patched. It worked for `src/wine` only
    because that tree is its own clone. (It also produced a false measurement during
    the rebase: all five patches appeared to "apply clean" to the CX tree, including
    ones whose code is demonstrably already there.)
  - **after patching, the tree must differ from its tag only by the series.** This is
    the check that was missing when an earlier session applied `CW Hack 24834`
    straight into `src/wine` with no patch file, so clean rebuilds silently dropped
    it — and when `wine-0005` sat applied in the tree, making its class-`debug` skip
    meaningless.

**Step 3 — retire the patch pile: DONE.** `patches/series` is now `wine-0005`
(class `debug`) plus the DXMT patch. The other four are in
`patches/superseded/README.md`, which cites where each one lives in the base:
`wow64cpu/cpu.c:253` (20760), `win32u/dce.c:1801` (20969/23427),
`winemac.drv/opengl.c:2166` (24834), `win32u/dce.c:731-934` (23950).
`git apply` now *refuses* 0006/0007/0008 against the base, which is redundancy
proven by construction rather than assumed. **`wine-0004` is the exception: it is
retired on the strength of 23950 existing, and that substitution is still untested
— Step 5 is what settles it.**

**Step 4 — bring up the prefix and Steam.** Plan: clone `prefixes/steam-base` with
APFS `cp -Rc` and `wineboot -u` it on the WineCX runtime, because that keeps the two
things only a human can redo — the Steam login and the S: library-folder
registration. `steam-base` stays as the untouched 11.14 fallback. A fresh
`bin/mkprefix` is plan B (it runs end to end as of 2026-08-01, but it cannot do the
login or the `S:` registration, which is the whole reason cloning wins here). Note there are
also two forgotten prefixes from the Jul 29 CX experiments, `steam-cxbuild` and
`steam-cxtest`: Steam installed, never logged in.

⚠️ Before `mkprefix` on a WineCX runtime, run `bin/get-addons` — and note it had a
bug that would have bitten exactly here: it read the expected Wine Mono version from
`src/wine` regardless of runtime, so a WineCX (Wine 11.0, wants mono **10.4.1**)
runtime would have been given upstream 11.14's **11.2.0**, which Wine refuses. It now
reads the source tree from the runtime's `PROVENANCE`. The symptom would have been a
.NET game launcher exiting 255 with no window.

**Step 5 — the test that validates the whole pivot: RUN, AND IT FAILED.**
Steam is **black** on WineCX 26.3.0 with `WINE_APPEND_ARGS` inert. `CX HACK 23950`
does not replace `wine-0004`; it is unconditional (nothing to enable), it never fires
for CEF windows, and CrossOver never modified `winemac.drv/surface.c`. Full
measurements and the corrected reasoning are in `RUNTIME.md` §2. **`wine-0004` is
back in `patches/series` as our own `workaround`, and it is the one patch this base
requires.** With it, Steam renders and logs in — verified by seeing
`--no-sandbox --in-process-gpu --disable-gpu` on the live `steamwebhelper.exe`
command line, not just by the window looking right.

**Step 6 — regression-check the game: PASSED, with one new regression.**
Strategic Command launches through Steam, the .NET `Launcher.exe` runs (inherited
Wine Mono 11.2.0 works on this 11.0 base despite it wanting 10.4.1), the game
reaches its menu, menus respond in **real time**, and a turn plays.

- **WoW64 loop: gone.** `utime 2:16.53` vs `stime 0:01.60` at ~200% CPU over 16
  threads — 98.8% userspace. The bug produced the inverse (48 s system / 2 s user,
  ~50,000 faults/s), so this is the clean signature. Note the *absence* of
  `virtual_setup_exception` in the log proves nothing here, because
  `strategic-command.env` forces `WINEDEBUG=-all`; the time ratio is the evidence.
- **Menu latency: gone**, from the base's `CW Hack 20969/23427`.
- **⚠️ NEW: audio jitters badly** — noticeably worse than on `wine-11.14-x86_64`.
  This is the first genuine regression from the base change.
- Between-menu transitions are still slow, i.e. the 2 s → 20 s fade is **unchanged**.

**Step 7 — the open items, in the order they now deserve.**

1. ~~**`WINEMSYNC=1`**~~ — **TRIED 2026-07-31. It is NOT the audio fix.** Enabled and
   *verified active* (see below); the audio jitter is unchanged. Keep it on anyway,
   since it is what the reference bottle runs, but cross it off as the explanation.

   **This one is properly excluded, unlike last time.** The verification, for reuse:
   ```
   msync: bootstrapped mach port on wine-4d24cc4-msync.
   msync: up and running.
   0024:trace:sync:request_shm_from_server requesting shm entry 0 from server
   ```
   with `WINEMSYNC=1 WINEDEBUG=+sync` — 114 `shm entry` traces across 44 wine pids,
   **0** `Server is running with WINEMSYNC but this process is not`, and **0**
   `mach_msg instead of mach_msg2` (so the fast trap, not the fallback). The prefix
   must be fully down first: the wineserver reads the variable too.

   The mechanics, for whoever needs them next — msync is opt-in per process *and per
   server*:

   ```c
   do_msync_cached = getenv("WINEMSYNC") && atoi(getenv("WINEMSYNC"));
   ```
   `dlls/ntdll/unix/msync.c:422` and `server/msync.c:533` (plus `WINEMSYNC_QLIMIT`).

   So every wait in our runs goes through the wineserver round-trip while the
   reference bottle uses msync — which fits both new symptoms (audio jitter, slow
   transitions). Requirements and the reason this is worth doing carefully:
   - **the wineserver must be started with it too**, so the whole prefix has to come
     down first — a running server plus a msync client is a mismatch;
   - that mismatch is *detectable*, which is the point: `msync.c:600` has
     `ERR("Server is running with WINEMSYNC but this process is not, ...")`. This is
     the control the docs said was missing when msync was previously written off as
     "not demonstrated". **Verify the variable arrived; do not assume it.**
2. **⚠️ THE OPEN REGRESSION: audio jitter, and rougher animation.** Now the top item.
   On `winecx-26.3.0` the game's audio jitters badly and its start animation is
   visibly rougher than on `wine-11.14-x86_64`, where audio was fine. Everything else
   is held constant — same host, same prefix lineage (`steam-winecx` is an APFS clone
   of `steam-base`), same game, and the same SC3 virtual desktop (that registry entry
   dates from 2026-07-30 16:01, i.e. it was already in place during the *smooth* 11.14
   runs, so it is not the cause). **Only the Wine build changed.**

   Ruled out already: msync (item 1), and CrossOver's audio delta itself — their
   `winecoreaudio.drv` divergence from upstream 11.0 is *only* microphone-permission
   code (`authorization.m`, `CoreAudio_get_capture_authorization_status`), with no
   latency tunable and no env/registry knob. `dsound/dsound.c` also differs and is
   unexamined.

   **The splitting test was run, and hypothesis (b) won: the CrossOver 26.3 bottle
   plays the same game with clean audio.** Same Wine version (11.0), same host, their
   binaries. So the 11.0 base is *not* the problem — **our build of their source is**,
   which merges this with the repo's other unresolved item (our build of their tree
   reporting a dummy GPU card). Two symptoms, one likely cause.

   | | hypothesis | verdict |
   | - | ---------- | ------- |
   | a | upstream improved audio/timing between 11.0 and 11.14 | **refuted** — their 11.0 bottle is clean |
   | b | **our build** of their tree differs from their shipped binaries | **supported** |

   ### The leading candidate: deployment target

   Their whole runtime is built for an old macOS; ours targets the host:

   | | `minos` | `sdk` |
   | - | ------- | ----- |
   | CrossOver's shipped `ntdll.so`, `winemac.so`, `win32u.so`, `winecoreaudio.so`, `wineserver` | **10.15** | 15.1 |
   | ours, same modules | **26.0** | 26.5 |

   macOS switches framework behaviour on this "linked-on-or-after" value, so identical
   source can behave differently — and it is a candidate for the NSWindow main-thread
   exception too, since that check is deployment-target gated. We pass **no**
   `-mmacosx-version-min` and set no `MACOSX_DEPLOYMENT_TARGET` anywhere in `bin/`
   (verified), while their `configure.ac` passes `-mmacosx-version-min=10.7` for the
   loader and preloader. **Next action: set `MACOSX_DEPLOYMENT_TARGET=10.15` in
   `bin/env-build-x86_64.sh`, rebuild, and listen.** Caveat to state honestly: we would
   still compile against SDK 26.5 where they use 15.1, so this makes us *closer* to
   their build, not identical.

   ### CPU: a real difference, but measure the same phase

   **Localised 2026-07-31: our build runs one extra thread flat out.** All three
   measured at the *menu* phase, same game, same host, via
   `bin/gamerun strategic-command-direct` (no clicking needed) and
   `ps -M <pid>` — note `ps -M` must be parsed per row, since the header row has
   `PRI` in field 6 and thread rows in field 4:

   | build | audio | total CPU | busy threads | R-band / T-band |
   | ----- | ----- | --------- | ------------ | --------------- |
   | our `wine-11.14-x86_64` | fine | **102.5%** | **one** — `31T` @ 99.4% | 5 / 11 |
   | CrossOver 26.3 shipped | fine | **~101%** | **one** — `31T` @ 94.6% | present |
   | **our `winecx-26.3.0-x86_64`** | **jitters** | **199.4%** | **two** — `31T` @ 99.0% **plus `63T` @ 99.9%** | 5 / 10 |

   The `31T` thread at ~99% is the game's main loop and is identical in all three. The
   **`63T` thread spinning at 99.9% exists only in our WineCX build** — that is the
   whole 100% difference. `63T` is a *boosted* timeshare priority, i.e. a thread that
   keeps waking up rather than computing steadily.

   ### The extra thread is `wine_dsound_mixer`

   Identified by correlating `ps -M` (per-thread CPU, kernel order) with `sample`
   (thread names, same order) — `sample` alone cannot attribute CPU, as it reports an
   identical sample count for every thread:

   ```
   idx  PRI   %CPU   name
    2   31T   99.0   (unnamed -- the game's main loop)
    8   97R    0.0   com.apple.audio.IOThread.client
    9   63R    0.0   audio_client_timer
   10   63T  100.0   wine_dsound_mixer     <-- the extra core
   14   31T    0.0   mmdevapi_midi_notify
   ```

   The index alignment is self-corroborating: the two audio helpers land on exactly the
   realtime priorities they should (97R for the CoreAudio IO thread, 63R for the audio
   timer). **So the audio mixer itself is the thing burning the core**, which explains
   the jitter and the extra 100% together, in a DirectSound title.

   Mechanics, for whoever picks this up (`dlls/dsound/`):
   - `DSOUND_mixthread` (`mixer.c:760`) loops on
     `WaitForSingleObject(dev->sleepev, dev->sleeptime)`.
   - `sleeptime` cannot collapse to zero: `primary.c:345` sets it to `5` when
     `period_ms < 3`, else `period_ms * 5 / 2`. So a zero timeout is **not** the cause.
   - The render path is event-driven — `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` plus
     `IAudioClient_SetEventHandle(client, device->sleepev)` (`primary.c:288`, `:296`) —
     so the mixer wakes whenever `winecoreaudio`/`mmdevapi` signals a period. Either
     the driver signals far too often, or each wake does far too much work.
   - The CoreAudio IO thread sits at **0.0%**, so the driver is not itself spinning.
   - **CrossOver's dsound is not the difference**: their only change from upstream 11.0
     is `CX HACK 21897`, a `Sleep(100)` for a Fallout 4 race. Upstream, however,
     reworked dsound substantially between 11.0 and 11.14 (`mixer.c`, `dsound_main.c`,
     `primary.c`, `dsound_convert.c`, `fir.h`, and `make_fir` was removed), which is why
     our 11.14 build has a different — and healthy — mixer.

   That left the puzzle sharp: **the same 11.0-era mixer code is fine in CrossOver's
   shipped build and spins in ours.**

   ### ✅ ROOT CAUSE (2026-07-31): our 32-bit PE DLLs compile float math to x87

   Found by swapping **only** CrossOver's `dsound.dll` (both arches) into a clone of our
   runtime — same runtime, same prefix, same game, one file different:

   | | our `dsound.dll` | CrossOver's `dsound.dll` |
   | - | ---------------- | ------------------------ |
   | total CPU | **197.8%** | **101.0%** |
   | `wine_dsound_mixer` | **100%** at `63T` | **1.5%** at `63R` |

   ~60× the CPU for the same work, so the fault is in the compiled PE, not the
   environment. Disassembling both 32-bit DLLs says why:

   | `i386` dsound.dll | x87 instructions | SSE instructions |
   | ----------------- | ---------------- | ---------------- |
   | CrossOver | 52 | **741** |
   | **ours** | **1334** | **0** |

   The 64-bit builds are at parity (no x87, ~310 vs ~334 SSE) because x86_64 mandates
   SSE2. **Only our 32-bit PE code uses x87 — and x87 is the one thing Rosetta 2
   emulates badly**, while it handles SSE well. A float-heavy audio mixer built with
   x87, running translated, is exactly a 60× story.

   Corroborating detail: with CrossOver's DLL the mixer sits in the **realtime band**
   (`63R`); with ours it is `63T`. `server/thread.c` explains that — a thread that
   violates the RT time-constraint policy is *demoted* out of the realtime band. Ours
   spun, so the scheduler evicted it. The priority was a symptom, not a cause.

   **This is not an audio bug. It affects every 32-bit PE DLL we build**, so it is a
   candidate for the "rougher animation" too and for 32-bit performance generally.

   **The fix, untested as of this writing:** `configure.ac:441` sets per-arch PE flags
   from `${wine_arch}_CFLAGS`, defaulting to `${CROSSCFLAGS:-"-g -O2"}`. So pass

   ```
   i386_CFLAGS="-g -O2 -msse2 -mfpmath=sse"
   ```

   to configure (in `bin/build-runtime`), or use **clang** as the PE compiler, which is
   what CrossOver's configure prefers (`configure.ac:430` checks for `clang`) and which
   defaults to SSE2 for i686 where GCC defaults to x87. Our PE compiler is Homebrew
   `i686-w64-mingw32-gcc (GCC) 16.1.0`. Nothing in CrossOver's *tree* requests SSE, so
   they supply it from their build environment or get it from clang.

   Verify a rebuild with:
   ```sh
   objdump -d runtimes/<rt>/lib/wine/i386-windows/dsound.dll | grep -cE '\b(mulss|mulps|addss|addps)\b'   # want >0
   objdump -d runtimes/<rt>/lib/wine/i386-windows/dsound.dll | grep -cE '\b(fmul|fadd|fld)[a-z]*\b'        # want ~0
   ```
   and then the thread profile: healthy is ~100% total with `wine_dsound_mixer` near 0%.

   `runtimes/winecx-26.3.0-cxdsound-x86_64` is the experiment that proved this — an APFS
   clone with CrossOver's `dsound.dll` swapped in. Keep it as the control until the
   rebuild is verified, then delete it; it is **not** a shippable configuration.
   `runtimes/winecx-26.3.0-mt1015-x86_64` is the deployment-target experiment, which
   **refuted** that hypothesis (mixer still spun at 99.2%); it can be deleted too.

   ### ⚠️ Attempt 1 at the fix: clang for PE — right codegen, broken networking

   `runtimes/winecx-26.3.0-clang-x86_64`, built with
   `--with-mingw=/usr/local/opt/llvm/bin/clang`:

   - **codegen fixed**: `120 SSE / 6 x87` in i386 `dsound.dll` (was `0 / 1334`).
   - configure enabled clang-only features mingw-gcc never gave us:
     `-fasync-exceptions`, `-ffp-exception-behavior=maytrap`, **`-fms-hotpatch`**
     (proper MS hotpatchable entries — relevant, since Steam's overlay hot-patches
     DLL entry points and that is implicated in the old WoW64 crash).
   - **but Steam cannot log in.** It reaches the login screen and then retries
     `EConnect` every 50 ms forever (`logs/connection_log.txt`), with **zero** TLS or
     certificate errors, so it never gets as far as crypto — it looks like a
     networking-layer regression (`ws2_32`/socket path) in the clang-built PE DLLs.
     Unresolved; needs its own session.

   One build-environment trap found on the way, worth keeping: Homebrew's llvm is
   **keg-only**, so `llvm-dlltool` is not on `PATH` and every import library fails with
   `winebuild: cannot find the 'dlltool' tool` — which reads like a missing package
   rather than a `PATH` problem. `bin/env-build-x86_64.sh` now adds
   `$BREW/opt/llvm/bin`. (`lld-link` *is* linked into `$BREW/bin` by the separate `lld`
   formula, so linking is not the part that breaks.)

   ### ✅ FIXED: `runtimes/winecx-26.3.0-sse-x86_64`

   `bin/build-runtime --winecx 26.3.0 --pe-gcc` — mingw-gcc with
   `i386_CFLAGS='-g -O2 -msse2 -mfpmath=sse'`, i.e. one variable changed from the build
   that worked. Measured at the menu, same test as every other row:

   | | total CPU | `wine_dsound_mixer` | i386 dsound float math |
   | - | --------- | ------------------- | ---------------------- |
   | before (x87) | 197.8% | **100%** at `63T` | 0 SSE / 1334 x87 |
   | CrossOver's shipped build | ~101% | 1.5% at `63R` | 741 SSE / 52 x87 |
   | their DLL on our runtime | 101.0% | 1.5% at `63R` | — |
   | **ours, `--pe-gcc`** | **101.9%** | **1.4%** at **`63R`** | **138 SSE / 24 x87** |

   The mixer is back in the realtime band rather than demoted for spinning, and the only
   busy thread is the game's own loop at `31T` — the same signature as both builds that
   always sounded fine.

   Note the SSE/x87 counts do not have to match CrossOver's exactly (different compiler,
   different inlining); what matters is that float math is SSE and x87 is residual.

   **Confirmed by ear (Gil, 2026-08-01): "audio is clean now, no jitter" and "loading
   up was also smooth"** — so the x87 penalty was hitting more than the mixer, exactly
   as expected for something that slowed every float-heavy 32-bit path.

   Done as part of this: `winecx-26.3.0-sse-x86_64` is now `DEFAULT_RUNTIME` and every
   profile points at it plus `prefixes/steam-winecx`; the superseded runtimes
   (`winecx-26.3.0-x86_64` (x87), `-mt1015-`, `-cxdsound-`, `wine-cx26.3-x86_64`) and
   their build trees are deleted (~14 GB). `bin/gamerun` now sets `WINEMSYNC=1` by
   default to match the reference bottle — **not** because it fixed anything.

   Remaining for this thread:
   - **the 2 s → 20 s menu fade** — re-measure on this build before theorising; the
     loading-smoothness improvement suggests the fade may have been x87 as well.
   - **clang** remains the better long-term PE toolchain (it is what CrossOver and
     upstream prefer, and it brings `-fms-hotpatch`); its Steam-login regression is the
     blocker. That runtime has since been deleted (stale absolute paths after the
     rename); rebuild it with `--pe-cc`/`--with-mingw=<clang>` when picking this up.
   - **the runtime name** still carries a `-sse` suffix, which is historical now that
     `build-runtime` guarantees and asserts SSE for every build. A future rebuild can
     drop it; renaming the directory in place is not safe.

   For the record, the earlier recommendation, which is what produced the fix:
   **`bin/build-runtime --winecx 26.3.0 --pe-gcc`**,
   which keeps the toolchain that demonstrably works and changes only the float math
   (`i386_CFLAGS=-g -O2 -msse2 -mfpmath=sse`). Verified in a smoke test: the same float
   loop compiles to 7 SSE / 1 x87 with those flags versus 0 SSE / 5 x87 without. That
   isolates the one variable that matters instead of swapping an entire toolchain.
   Revisit clang separately — it is the better long-term answer (it is what CrossOver
   and upstream prefer) but it is not a one-line change.

   R-band counts match (5 either way), so audio threads *are* being placed in the
   realtime band correctly — the jitter is not misplaced audio threads, it is a core
   being eaten.

   Getting the process right matters here: the virtual-desktop launch goes through
   `start.exe`, and **`ps -M` and `ps -eo command` print different strings for that
   wrapper**, so `grep -v start.exe` does not exclude it and two of these measurements
   initially profiled a 0%-CPU wrapper. Select the busiest matching pid instead.

   Useful negatives from the same session: the wineserver did **not** fail to set
   thread QoS (`msync: error setting thread latency QoS` never appears; only
   `msync: bootstrapped mach port` / `up and running`), and the mach thread-policy code
   in `server/thread.c` is **identical in upstream 11.0 and CX**, gated only on
   `#ifdef __APPLE__` — so it is compiled into our build too.

   ⚠️ **Do not repeat the "we burn twice the CPU" claim without matching phases** — the
   200% reading was taken during gameplay and the 101% at the menu, and at the menu
   ours is 114%. The genuine, phase-matched finding is the **extra ~14% thread**
   (`PRI 40T`) that CrossOver does not appear to have. Identifying it is the concrete
   next measurement, and it needs both sides sampled in the *same* phase.

   `sample(1)` cannot symbolise the hot code: 4,500 of ~5,265 thread-samples sit at one
   unresolved address inside the Rosetta-translated region, i.e. in the game's own
   translated code rather than in Wine. Thread *names* do come through, and the audio
   plumbing is all present and correct (`com.apple.audio.IOThread.client`,
   `audio_client_timer`, `wine_dsound_mixer`, `mmdevapi_midi_notify`). `ps -E` on
   another process returns nothing (macOS blocks it), so CrossOver's runtime
   environment cannot be read that way.

   **Do (b)'s test first — run the game in the CrossOver 26.3 bottle and listen.** It
   costs no build time, and the bottle is now the same Wine version as our runtime.
   While listening, also grab `ps -o utime=,stime=,%cpu=` on their game process: ours
   runs at ~200% CPU with `utime 2:16 / stime 0:01`, and a materially lower number
   there would be independent evidence for (b).

   If (b) holds, this merges with the repo's other unresolved item — *"why did our
   build of CrossOver's own published source not behave like their shipped binaries?"*
   (GPU identification fell back to a dummy card). Two symptoms, one cause, and worth
   far more than the audio: it would mean our whole build recipe diverges from theirs
   in some way we have not found. Next step there is to compare their build
   configuration, not their source — start with the shipped
   `/Applications/CrossOver.app/.../wine` binaries (`otool -L`, linked frameworks,
   optimisation/codegen) against ours.

   To isolate (a) offline and without needing CrossOver at all: build **upstream 11.0**
   properly (we have `src/wine-11.0`; the existing `runtimes/wine-11.0-x86_64` is
   degraded — no bundled libs — so it cannot be used for this) and test audio on it.
   Three points then bracket the answer: upstream 11.0, CX 11.0, upstream 11.14.
3. **The 2 s → 20 s menu fade** — unchanged by the rebase. Candidate `CXHACK 19488`
   noted above; re-measure before theorising.
4. **The other two games** — Quasimorph, Battleship Command, both 64-bit Unity/D3D11,
   so DXMT territory and untouched by any of the 32-bit work.
5. **The SDL2/`winebus` crash** from `RUNTIME.md` §1 — decide whether to drop SDL2
   from the bundle or fix the dlopen. Gamepads do not work either way today.

### What carries over, and what does not

- **Carries over:** all of `bin/` (`build-runtime`, `bundle-libs`, `mkprefix`,
  `gamerun`, `killwine`, `get-addons`, `compare-bottle`), `profiles/`,
  `tools/probes/`, the bundled-libs/soname-seeding approach (`RUNTIME.md` §1), and
  the documented negative results below.
- **Became redundant, and are now in `patches/superseded/`:** `wine-0004` (23950
  replaces it — *pending Step 5*), `wine-0006` (= 20760), `wine-0007` (= 20969/23427),
  `wine-0008` (= 24834). They are still needed to reproduce the upstream fallback
  runtime `wine-11.14-x86_64`, whose source tree `src/wine` has all four applied —
  which is why `src/wine` is deliberately left dirty rather than reset.
- **Watch out:** the first CX-based runtime here (`wine-cx26.3-x86_64`, since deleted)
  had **zero bundled libs** and so was never usable — `RUNTIME.md` §1's work must be
  applied to any CX-based runtime or you silently lose freetype/vulkan/gnutls. (It was
  *not* misnamed; an earlier note claiming that was wrong.)

---

## Start here (handover)

Read this file, then `RUNTIME.md`. `PROJECT.md` is history — useful, but it
contains conclusions that were later disproved and are marked as such.

Everything below this line describes the **upstream-based** tree, which is now the
**fallback**, not the base. It is still accurate and the game runs on it — see the
rebase status above for what replaces it.

**The environment works.** Steam runs, renders and logs in with no environment
variables. Don't re-derive any of that.

**Reproducibility of the fallback runtime.** Every modification in `src/wine` has a
patch file, but those patches now live in `patches/superseded/` rather than
`patches/series`, so rebuilding `wine-11.14-x86_64` means naming them explicitly.
`src/wine` is deliberately left with all five applied — it is the source of the
runtime that currently works, and resetting it would strand that runtime. Note
`wine-0005` (class `debug`) being applied there is why the old "a clean rebuild
reproduces this runtime" claim was only approximately true; on the WineCX base
`build-runtime` now asserts that this cannot happen again.

**The task is Strategic Command WWII. It is NOT a graphics problem — root cause
found 2026-07-30.** The game never gets anywhere near drawing. It dies in
Wine's 32→64-bit WoW64 transition thunk. Full mechanism in §"The WoW64 mode
switch" below; the short version:

> Wine's `wow64cpu.dll!syscall_32to64` is 64-bit code reached from 32-bit code
> via a CS-changing far call. **In this game that mode switch does not take
> effect**, so the thunk is executed as 32-bit code. Its first instruction
> `xchg %r14,%rsp`
> (`4c 87 f4`) decodes in 32-bit mode as `dec %esp; xchg %esi,%esp`, which
> trashes ESP; a few instructions later the RIP-relative
> `mov 0x5ecd(%rip),%edx` decodes as the **absolute** load `mov 0x5ecd,%edx`
> and faults reading `0x5ECD`. Wine then cannot deliver that exception, because
> the wrecked ESP points into a read-only image page, so writing the exception
> frame faults too — and that re-enters the handler. The result is ~50,000
> page faults and ~375,000 mach traps per second, forever, at 100% CPU.

**Do not start from the screen.** The black fullscreen window with an hourglass
is *normal* for the first ~30 s — CrossOver shows exactly the same thing before
the menu appears. Ours simply never leaves that phase. Nothing about the window
is diagnostic.

**Why CrossOver survives it — answered.** They carry a Rosetta-specific
workaround, **`CW HACK 20760`**, in `dlls/wow64cpu/cpu.c`, gated on
`is_rosetta2()` (CPU brand string contains `VirtualApple`). It replaces the
mode-switching far **`ljmp`** with a far **`lcall`** on the way in and a far
**`lretq`** on the way out. Their own comment:

> *"When running under Rosetta 2, use lretq instead of ljmp to work around a
> SIGUSR1 race condition."*

The reference implementation is **already on disk** at
`src/crossover/sources/wine/dlls/wow64cpu/cpu.c`; `diff` it against
`src/wine/dlls/wow64cpu/cpu.c` (163 lines, and the hack is the only real
difference in that file). A port to 11.14 was built and **verified to make the
game reach its main menu**.

⚠️ **The patch is NOT currently applied.** It was deliberately reverted so the
problem could be used as a clean-context test for several models. To restore it,
re-port the hack from that diff, register it in `patches/series` as class
`workaround`, and rebuild only `dlls/wow64cpu`. Confirm with
`objdump -d runtimes/wine-11.14-x86_64/lib/wine/x86_64-windows/wow64cpu.dll |
grep -c lretq` — **2** means applied, **0** means not.

⚠️ **An earlier version of this file said "the fix is not in the thunk's code"
and sent the reader to the caller side. That was wrong**, and it was written
while the investigation was still open. The divergence is in the thunk, starting
at `+0x11b0` — only `0x77` bytes past where the comparison stopped. Two lessons
recorded in "Method note".

---

`PROJECT.md` holds design and decisions. `RUNTIME.md` holds what this
environment changes from stock Wine and why. This file is only "what to do
next".

## State

**Steam works.** `bin/gamerun steam` brings up the client, logs in, and renders
the store — text and all. It needs no environment variables: the runtime carries
its own libraries (`RUNTIME.md` §1).

**Strategic Command WWII is installed and launches, and then burns a core
forever in a Wine WoW64 exception loop.** It is the first non-Chromium target
and the first 32-bit one. "Renders black" was the old description and is
misleading — see the handover note above.

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

## The WoW64 mode switch (Strategic Command root cause)

Measured 2026-07-30. Evidence in `docs/evidence/sc3-*`.

**How to reproduce in one step.** `bin/gamerun strategic-command-direct` runs the
game exe directly, so the environment is yours and there is no launcher click.
It needs Steam already up in the same prefix, and it needs
`steam_appid.txt` — see "the steam_appid.txt file" below.

```sh
bin/gamerun steam &                                    # wait for login
WINEDEBUG=+virtual,+pid bin/gamerun strategic-command-direct
```

**The two log lines that matter**, always byte-identical across runs:

```
warn:virtual:virtual_setup_exception exception outside of stack limits
    addr 0x7b591139 stack 0x7b3f0f00 (0x100110000-0x100112000-0x10020fd20)
warn:virtual:virtual_setup_exception exception outside of stack limits
    addr 0x4101fd     stack 0x7b3efd50 (0x100110000-0x100112000-0x10020fd20)
```

`0x7b591139` is `wow64cpu.dll` (loaded at `0x7B590000`, confirm with
`+loaddll`) `+0x1139`. Disassembling our own runtime's copy:

```
000000007a401110 <syscall_32to64>:
    7a401110:  4c 87 f4              xchg %r14,%rsp
    ...
    7a401139:  8b 15 cd 5e 00 00     mov 0x5ecd(%rip),%edx   # cs32_sel
```

The `+seh` record for that fault says `info[0]=0` (read) and
**`info[1]=0x5ECD`** — the accessed address equals the instruction's
*displacement*. That only happens if the bytes were decoded in 32-bit mode,
where `mod=00, rm=101` means absolute disp32 rather than RIP-relative. The same
bytes both ways:

| bytes | 64-bit (intended) | 32-bit (what ran) |
| ----- | ----------------- | ----------------- |
| `4c 87 f4` | `xchg %r14,%rsp` | `dec %esp` + `xchg %esi,%esp` |
| `8b 15 cd 5e 00 00` | `mov 0x5ecd(%rip),%edx` | `mov 0x5ecd,%edx` |

Corroborating: the faulting context reports `cs=0107` — still the **32-bit**
code selector — and `ss=0000`. And `esp=0x7b3f0f0a` is misaligned and points
into a lone 4 KB `rwx` page at `7b3f0000-7b3f1000`, i.e. wreckage from
`dec %esp; xchg %esi,%esp`, not a stack.

The loop follows from the wrecked ESP: Wine tries to build the exception frame
there, `0x7b3efd50` and below is the **`r-x`** region under that page, the
`memmove` write faults (`lldb` shows thread 2 stopped at
`EXC_BAD_ACCESS code=2 address=0x7b3efc30` inside
`_platform_memmove$VARIANT$Rosetta`), and that fault re-enters the handler.
Measured cost: `top` shows FAULTS climbing ~50,000/s and SYSMACH ~375,000/s,
with 48 s of *system* time against 2 s of user time.

### This is situational, NOT "32-bit is broken"

Important, because it is the obvious wrong conclusion to draw from the above.
Every 32-bit process reaches `syscall_32to64` for *every* syscall, so a
universally broken thunk would mean no 32-bit exe could even load a DLL. Two
32-bit exes from this very game directory run fine on this runtime:

| exe | bitness | result |
| --- | ------- | ------ |
| `Launcher.exe` | PE32 (.NET) | runs, draws its window, PLAY works |
| `Editor-WWII War in Europe.exe` | PE32, same SDL stack | runs, 1.5% CPU, **zero** `virtual_setup_exception` |
| `SC3-WWII War in Europe.exe` | PE32 | faults as above |

So the mode switch normally succeeds and fails only at this one point. Our Wine
is not miscompiled and the prefix is not misconfigured — resist "rebuild Wine"
as the first move.

**Why it fails *here*.** The syscall that crosses the boundary at the fatal
moment is `eax=0x99` = **`NtFlushInstructionCache`**, issued by Steam's overlay
immediately after it hot-patches a function entry in `mmdevapi.dll` (the `+virtual`
trace shows `NtProtectVirtualMemory(0x74595a80, PAGE_EXECUTE_READWRITE)` → write →
`PAGE_EXECUTE_READ` right before it). Per CodeWeavers, the trigger is a **SIGUSR1
race**: Wine's server sends SIGUSR1 for thread suspension and system-APC delivery,
and a signal landing on the far `ljmp` leaves the thread with the new RIP but the
stale CS. That explains why this process dies and the Editor and .NET launcher do
not — only this one has `gameoverlayrenderer.dll` and `steamclient.dll` injected,
both of which call `SuspendThread`/`Get`/`SetThreadContext`.

*Competing hypothesis, unresolved:* the trigger may be **translation-cache
invalidation** from the immediately preceding code patch, whose re-translation
loses 64-bit mode — the fault is 4/4 byte-identical and always right after a
patch+flush, which fits invalidation better than a race. The two are
indistinguishable from outside the emulator and **the fix is the same either way**.
A minimal 32-bit program that patches its own code and flushes would settle it.

⚠️ **Retracted reasoning:** an earlier version argued the byte-identical fault
addresses were evidence *against* a race. That is invalid — `0x7b591139` is fixed
by construction, since it is always the same instruction at the far-jmp target, no
matter when the race lands. Determinism says nothing either way here.

### What this rules out

- Graphics. Nothing GL, ddraw, D3D or backend-related is in the path. The
  previous "SDL 1.2 / DirectDraw, make it draw" framing was wrong.
- Audio. `SDL_AUDIODRIVER=dummy` changes nothing; the fault is identical.
- The Steam overlay. It *is* injected and its handlers *are* walked during the
  unwind, which made it look guilty — but the fault is byte-identical with the
  overlay disabled in Steam, and an SEH handler being walked says nothing about
  what caused the fault. Gil disabled the in-game overlay while testing this;
  it can be turned back on.

### The fix, concretely

Port `CW HACK 20760` from `src/crossover/sources/wine/dlls/wow64cpu/cpu.c` into
`src/wine/dlls/wow64cpu/cpu.c`. It applies essentially verbatim to 11.14 and needs
nothing else — upstream 11.14 already implements `SystemProcessorBrandString`
(`dlls/ntdll/unix/system.c`), which `is_rosetta2()` depends on. Five parts:

1. `struct thunk_32to64_rosetta2_workaround` (lcall + far-return frame), and make
   the two members of `struct thunk_opcodes` unions of the old and new shapes.
2. `BOOL use_rosetta2_workaround;` — **non-static**, the asm references it by name
   — plus `is_rosetta2()`.
3. `syscall_32to64`: `cmpl`/`jne` around the existing `ljmp`, with an `lretq` path.
4. `unix_call_32to64`: the same, with a distinct label.
5. `BTCpuProcessInit`: set the flag and build the `lcall`-based entry thunk.

Then rebuild only `dlls/wow64cpu` and install `wow64cpu.dll`. Because the Wine
*version* is unchanged, `prefixes/steam-base` is not at risk.

Note the 32→64 half of the change is **invisible to any binary diff** — it is bytes
poked into `code_buffer` at runtime by `BTCpuProcessInit`, living in `.data`. Only
the 64→32 `lretq` half is visible in `.text`. That is part of why a static compare
of the two shipped DLLs looked like "no relevant difference".

(Historical note: that section suggested smoke-testing against
`runtimes/wine-cx26.3-x86_64`. That runtime is deleted, and the whole question is
moot — the WoW64 workaround comes from the base now.)

## Next actions, in order

**0. THE PIVOT — LANDED 2026-07-31.** The runtime is
`runtimes/winecx-26.3.0-sse-x86_64` (WineCX 26.3.0 = CrossOver 26.3.0 = Wine 11.0),
built by `bin/build-runtime --winecx 26.3.0 --pe-gcc`, prefix `prefixes/steam-winecx`.
Both are now the defaults, so no environment overrides are needed. Steam renders
and logs in; Strategic Command reaches its menu with real-time menus and plays a turn.
`patches/series` carries exactly **one** functional delta of ours (`wine-0004`) plus a
`debug` patch — down from four ports of CodeWeavers' hacks.

Launch it with:
```sh
bin/gamerun steam
bin/gamerun strategic-command
RUNTIME=wine-11.14-x86_64 PREFIX=steam-base bin/gamerun steam   # the old build, for A/B
```
(`RUNTIME`/`BACKEND`/`PREFIX` in the environment override a profile, which is how the
runtime comparisons in this document were run.)

Read "THE REBUILD" above for what each step actually produced, then Step 7 for what is
open. The next thing to try is **`WINEMSYNC=1`**.

1. ~~Re-apply the wow64cpu fix~~ — **done.** `patches/wine-0006` is applied, built
   and installed; the game reaches its menu and plays a turn of the 1939 campaign.

1b. **Menu latency — solved.** `patches/wine-0007` (CrossOver's
   `CW Hack 20969, 23427`) sends `WM_NCPAINT` asynchronously instead of
   synchronously. Menus went from 5–10 s per hover to real-time.

   Attribution is precise, not "we changed two things and it improved": the paired
   `WM_ERASEBKGND` 1-second bound (`CW Hack 23394`) is carried in the same patch and
   its `ERR("timeout")` fired **zero** times across a full launch and menu session,
   so that half never engaged. The `WM_NCPAINT` change is the whole fix.

   Method note worth keeping: every metric looked *normal* while this bug was
   active — CPU, page faults and mach syscalls were all within noise of CrossOver's.
   Only BSD syscalls differed (15,300/s vs 7,100/s), and even that was a red
   herring. The thread was not working, it was **waiting**. Measuring throughput
   for a latency bug wasted most of the investigation.

1c. **Still open: the 2 s → 20 s menu fade.** Same family as 1b, almost certainly
   another CrossOver paint/timer hack. Good first target once rebased — and the first
   thing to do is simply *re-measure it on the WineCX runtime*, because the base may
   already fix it.

   Candidate, found while rebasing and **not yet tested**: `CXHACK 19488` in
   `dlls/win32u/message.c:3218`. For a window with `WS_EX_COMPOSITED` it dispatches
   `WM_PAINT` synchronously and then clears `PM_QS_PAINT` from the peek flags, i.e. it
   collapses repeated paint churn on exactly the kind of window an animated fade uses.
   Enumerate the rest with:

   ```sh
   grep -rn -E 'CW ?HACK|CW Hack|CX ?HACK|CXHACK|CrossOver Hack' --include='*.c' \
     src/crossover/sources/wine/dlls/win32u src/crossover/sources/wine/dlls/winemac.drv
   ```

2. **Then the other two games** — Quasimorph and Battleship Command, both Unity
   D3D11, so DXMT is the intended backend. Note they are **64-bit**, so they do
   not go through the WoW64 thunk at all and are very likely unaffected by the
   bug above. Worth trying early: they may just work.

3. **Fold prefix provisioning into a fresh prefix.** `bin/mkprefix` installs
   corefonts, `d3dcompiler_47` and Wine Mono, and as of 2026-08-01 it is measured
   end to end (`wb-test`, WineCX 26.3.0) rather than assumed. `steam-base` predates
   it and was provisioned by hand. Run `bin/get-addons` first or there is no Mono
   MSI to install.

## Known-good launch recipes

```sh
bin/gamerun steam                     # client; renders, logs in
bin/gamerun strategic-command         # via steam.exe -applaunch 593030 (real path)
bin/gamerun strategic-command-direct  # runs the game exe directly (debug path)
```

### The `steam_appid.txt` file

`library/.../Strategic Command WWII War in Europe/steam_appid.txt` contains
`593030` and **was added by us**, not by Steam. It is what makes
`strategic-command-direct` possible: without it `steam_api.dll` calls
`SteamAPI_RestartAppIfNecessary`, hands the launch back to Steam via
`steam://run/593030`, and exits — so you get the .NET launcher again instead of
the game. With it present, `SteamAPI_Init` just connects to the running client.

It is an added file, not a modified one, so it is harmless; `rm` it to get stock
behaviour back, and note "verify integrity of game files" may remove it.

Correction to `RUNTIME.md` §9: that section reads as "running the game exe
directly never works, it exits 53". The real rule is narrower — direct launch
fails **only when Steam is not already running**. With Steam up it still hands
off unless `steam_appid.txt` is present.

(The blanket "Steam games must be launched through Steam, it exits 53 otherwise"
rule that used to be repeated here is superseded by the two paragraphs above.)

## Open questions

- ~~**Why does CEF add `--in-process-gpu` under CrossOver and not for us?**~~
  **This was the wrong question — closed 2026-07-30.** CEF's decision logic never
  mattered. CrossOver does not need the switch because `CX HACK 23950` gives
  foreign-parent windows a shared-memory surface flushed to the owning process, so
  Chromium's viz-process blits land somewhere real. The earlier session reported
  "nothing in their source forces this flag" — technically true and completely
  misleading, because it searched for *who passes the flag* instead of *what makes
  the flag unnecessary*. See `RUNTIME.md` §2. Remaining work is to port it, not to
  investigate it.

  Original text, for the record: *We force it via `WINE_APPEND_ARGS`; the trigger
  is still unknown. Ruled out:*
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

**Three additions from the wow64cpu hunt (2026-07-30), because that session
produced the right mechanism and then the wrong direction:**

1. **`src/` is reference material, not build output.** `.gitignore` lists `/src/`
   with the artifact directories, which is misleading: it holds
   `src/wine/`, `src/wine-11.0/` and `src/crossover/sources/wine/`. The answer to
   this bug was a `diff` of two files already sitting there. `src/` appeared in the
   first `ls` of the session and was never opened, because the ignore rule had
   framed it as output. **A `.gitignore` says what not to *commit*, not what is not
   worth *reading*.** For any "works in CrossOver, not for us" question:
   `grep -rn 'CW HACK' src/crossover/sources/wine/` enumerates every intentional
   divergence they carry, and `grep -rl Rosetta src/crossover/sources/wine/`
   returns six files.
2. **Match the tool to the question.** A *difference* question ("why does the
   working reference behave differently?") is answered by diffing the reference.
   Tracing answers a *behaviour* question ("what is happening?"). This session
   asked the first and instrumented for the second, and the trace — which was
   correct and detailed — then anchored attention on the fault site rather than the
   version delta.
3. **Do not write conclusions into this file while still investigating.** The
   "fix is not in the thunk's code" error was committed to the handover mid-hunt,
   which turned a guess into a stated finding and pointed the next reader away from
   the answer. Record observations here freely; defer conclusions until the
   question is actually closed. When stuck, spawn a fresh-context **read-only**
   investigator instead of writing up a dead end.

## Potential upstream contribution (candidate — NOT pursued)

**`virtual_setup_exception` writes the exception frame onto a stack it has just
rejected.** In `dlls/ntdll/unix/virtual.c` (~4674-4686 in the 11.14 tree) it
detects that the target stack is outside the thread's limits, logs

```
warn:virtual:virtual_setup_exception exception outside of stack limits ...
```

and then proceeds to return `stack - size` and write there anyway. When the stack
pointer is genuinely wrecked, that write faults, which re-enters exception
delivery, which faults again — an unkillable ~100% CPU loop that emits **no
further output at all**. That is the entire reason this bug presented as a
"rendering hang" instead of a crash, and it is why it cost days rather than
minutes.

Calling `abort_thread(1)` here — as the nested-signal-stack case already does —
or even just promoting the `WARN` to an `ERR`, would turn a silent infinite loop
into an immediate, obvious diagnosis. Arguably worth more to the next person than
the Rosetta workaround itself, since it converts a whole class of WoW64 stack
corruption from "mystery hang" into "named error".

**Status: deliberately not pursued.** Recorded so it is not lost. Nobody has
written a test case, checked current upstream, or confirmed `abort_thread` is the
right call there.

## Tooling fixed while doing this (2026-07-30)

- **`bin/killwine` missed most leftovers.** Wine rewrites a PE process's argv
  *and* its `comm` to the Windows image name and orphans reparent to PID 1, so
  a leftover shows up as bare `start.exe` with no path — matching nothing.
  Found **751** stale `winedbg --auto` crash handlers from a single five-minute
  crash loop on Jul 29, plus four `start.exe /exec explorer /desktop=SC3` from a
  virtual-desktop experiment, one over a day old. `killwine` reported "0
  processes" the whole time. Now matches `winedbg` and `start.exe`, and
  `bin/killwine --deep` sweeps by open files via `lsof` (exact, ~20 s, opt-in).
  The deep sweep keys on `runtimes/` only and skips shells on purpose — keying
  it on the whole tree would kill a terminal whose cwd is in `library/`.
- **`bin/gamerun` silently dropped `WINEDLLOVERRIDES`.** It built
  `"${BACKEND_OVERRIDES};${WINEDLLOVERRIDES}"` unconditionally, so with
  `BACKEND="none"` the string began with a stray `;` and Wine ignored the whole
  list. This actively produced a wrong conclusion: a run intended to disable
  `gameoverlayrenderer` loaded it anyway, and the override looked tested when it
  had not been. Now only joins when both sides are non-empty.
- **`bin/killwine --game-only`** kills only the game (`S:\SteamLibrary` plus
  orphaned `winedbg`) and leaves the Steam client up. The full sweep matches
  `C:\Program Files`, which is Steam too, so every iteration used to cost a ~60 s
  client restart. Measured: relaunch-to-spinning drops from ~60 s to **3 s**. Use
  it while iterating on one game; use plain `killwine` to tear everything down.
  Note `WINEDLLOVERRIDES='gameoverlayrenderer='` does **not** stop the overlay —
  Steam loads it by full path, so that route cannot A/B it.

## Do not re-chase these (measured and refuted)

- *"`BACKEND=` in the profile means the game is using that backend."* **No — not
  for a Steam game (measured 2026-08-15).** The client spawns the game and
  `WINEDLLOVERRIDES` does not cross that hand-off, so the game silently gets
  wined3d. Check what actually loaded rather than what the profile says:

  ```sh
  WINEDEBUG=+loaddll bin/gamerun <profile> 2>&1 | grep -iE 'd3d11\.dll|dxgi'
  # "native" = the backend is in the path. "builtin" = wined3d, and on macOS
  # that means the game will fail outright (RUNTIME.md section 10).
  ```

  A Steam game needs `bin/backend-install --app '<Game.exe>'`. And if you see
  Steam's **"Launch Game with custom arguments"** dialog, that is proof Steam
  launched the process, not `gamerun` — so nothing from the profile environment
  reached it.

- *"An intermittent graphics failure is a race / a flaky backend."* This one was
  neither. It reproduced perfectly once the launch path was the variable rather
  than the timing. Before theorising about races, establish **which process
  actually started the game** — Steam's `logs/gameprocess_log.txt` records every
  launch with its full command line and is the fastest way to tell.

- *"A D3D11 game failing on wined3d means something is wrong with our build / the
  prefix / that game."* **No — wined3d cannot do D3D11 on macOS at all
  (measured 2026-08-14).** The adapter caps at feature level **9_3** because
  MoltenVK lacks `geometryShader`, `pipelineStatisticsQuery` and
  `shaderCullDistance`, so `D3D11CreateDevice` returns `E_FAIL` (`0x80004005`)
  for every application. Do not debug this per-game and do not look for a
  wined3d fix — use DXMT. Full chain in `RUNTIME.md` §10. The one-line check:

  ```sh
  WINEDEBUG=+d3d bin/gamerun <profile> 2>&1 | grep 'maximum feature level'
  # 0x9300 = the cap. Nothing above 9_3 will ever be granted by wined3d here.
  ```

- *"Forcing D3D12 gets around it."* **No.** `D3D11On12CreateDevice` is an
  `E_NOTIMPL` stub in the CX tree (`dlls/d3d11/d3d11_main.c:381`), so a Unity
  D3D12 build fails at interop-device creation (`0x80004001`) and falls back to
  the D3D11 path above. Note vkd3d itself is healthy — it reports feature level
  11.0 — so this is a missing interop shim, not a Vulkan limitation.

- *"DXMT does not load because it was built against Wine 11.14 and the runtime is
  Wine 11.0."* **Refuted 2026-08-14 — that mismatch is fine.** `winemetal.so`
  loads into the 11.0 runtime without complaint. The actual fault was our
  packaging: `bin/backend-install` installed the PE half only into the prefix,
  and Wine derives a unixlib's path from the **builtin** PE in the runtime tree.
  Fixed; see `backends/README.md`. Recorded because the ABI story is the
  plausible-sounding explanation and it is wrong.

- *"The `NSWindow should only be instantiated on the main thread!` exception is a
  WineCX problem."* **No — it is ours, and the mechanism is measured (2026-07-31).**
  The call stack names it outright:

  ```
  30  winebus.so   sdl_bus_init + 76
  29  dyld         dlopen_from  ->  runInitializers / forEachInitializer
      libc++abi: terminating due to uncaught exception of type NSException
  ```

  `winebus` (gamepad support) `dlopen`s **our bundled `libSDL2`**, and SDL2's dyld
  initializer instantiates an `NSWindow` off the Cocoa main thread, which macOS 26
  turns into a hard exception. That kills the `winebus` service — hence
  `scmdatabase_autostart_services Auto-start service L"winebus" failed to start: 1053`
  in the same log — so **gamepads do not work**, which is exactly what bundling SDL2
  was for. See `RUNTIME.md` §1.

  It is pre-existing rather than a rebase regression: `wine-11.14-x86_64`, the runtime
  that runs Steam and plays Strategic Command, does the identical thing. Not a
  blocker for keyboard/mouse titles.

  Method note, worth more than the bug: the three-runtime A/B initially looked like a
  WineCX-only fault because `wine-11.0-x86_64` reported **zero** exceptions. But that
  runtime prints *"Wine cannot find the FreeType font library"* — it has **no bundled
  libs at all**, so its `dlopen` of SDL2 simply fails and the crash cannot happen.
  **A control that is itself broken is not a control.** Reading its clean result as
  exculpating our build would have produced a confident wrong conclusion; what
  actually settled it was the call stack, not the comparison.
- *"Strategic Command is a rendering/black-window problem."* No. It never
  reaches drawing. It is the WoW64 32→64 mode-switch fault above. The black
  fullscreen window with hourglass is *identical* to CrossOver's first ~30 s and
  carries no information.
- *"The Steam overlay is the **cause**."* No — the cause is the un-worked-around
  `ljmp`. Byte-identical fault with the in-game overlay disabled. Its SEH handlers
  appear in the trace only because they are *walked* during the unwind, and its
  `Unknown opcodes for X86` lines mean it *declined* to hook (and name
  `SETUPAPI.dll`, nowhere near the crash site). Its log stopping at
  `Initializing Audio...` is the hang, not the cause.
  **But do not over-correct to "the overlay is irrelevant" either** — it is the
  plausible *trigger*: it issues the `NtFlushInstructionCache` that crosses the
  boundary, and it (with `steamclient.dll`) is the only source of
  `SuspendThread`/`SetThreadContext` traffic, i.e. of SIGUSR1. Cause and trigger
  are different things here. (Trigger role: inferred, not measured.)
- *"It is an audio problem."* No. `SDL_AUDIODRIVER=dummy` changes nothing;
  `DSOUND`/`mmdevapi` still load and the fault is identical. They load just
  before the fault, which is timing, not causation.
- *"Our Wine is built wrong / 32-bit does not work here."* No. `Launcher.exe`
  and `Editor-WWII War in Europe.exe` are both 32-bit and both run on this
  runtime, and both use the same thunk for every syscall.

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
