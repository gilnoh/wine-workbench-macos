# Start instruction — next session

Paste this file (or just point at it) at the start of a new Claude session.

---

## What this repo is

`~/games` is a Wine-on-macOS gaming playground on an **Apple M1 Max (arm64), macOS
26.x**. All Wine code is x86_64, therefore running under **Rosetta 2**. The aim is not
to be a launcher — it is a development environment for macOS-specific Wine work
(`wine_mesa_gl` and similar), with real games as the test load.

## Your goal this session

**The WineCX 26.3.0 rebase is DONE, and so is the audio regression it exposed
(2026-07-31 → 08-01).** Steam renders and logs in, Strategic Command plays with clean
audio and smooth loading, and `patches/series` is down to one functional delta of ours.

The finding worth carrying forward: **our 32-bit PE DLLs were compiling float math to
x87, which Rosetta 2 emulates badly.** `wine_dsound_mixer` burned a whole core (100% vs
1.5%) and the audio jittered. `bin/build-runtime` now guarantees SSE and **fails the
build** if it is missing. See `RUNTIME.md` §4 and the root-cause section in `NEXT.md`;
msync was excluded along the way (enabled and *verified* active, jitter unchanged).

Open, in rough order:

1. **The 2 s → 20 s menu fade** — the last known symptom. Re-measure on the current
   build before theorising: loading got smoother with the x87 fix, so this may have
   gone with it.
2. **clang as the PE compiler** — the better long-term toolchain (CrossOver's and
   upstream's choice; brings `-fms-hotpatch`), blocked on a Steam-login regression
   (`EConnect` retried every 50 ms, zero TLS errors).
   `runtimes/winecx-26.3.0-clang-x86_64` and its build tree are kept so this needs no
   rebuild to debug.
3. **The other two games** — Quasimorph, Battleship Command. Both 64-bit, so untouched
   by the x87 problem and by all the 32-bit work. Never tried; they may just work.
4. **The SDL2/`winebus` crash** (`RUNTIME.md` §1) — gamepads do not work, and every
   prefix bring-up logs an alarming, unrelated-looking exception.

Read, in this order, and do not re-derive what they already record:

1. `docs/NEXT.md` — start with **⚠️ THE PIVOT** and follow **THE REBUILD** (Steps 0–7)
2. `docs/RUNTIME.md` — what this environment changes from stock Wine and *why*
3. `docs/BRIEF-wow64-mode-switch.md` — a solved case study; read only if you need the
   worked example of how findings get evidenced here

`docs/PROJECT.md` is history and contains conclusions later disproved (marked as such).

## Settled — do not re-open these

- **The base is CrossOver's Wine, not upstream Wine.** This was argued and decided.
  The reason: our work should be adoptable by the people who actually ship Wine on
  macOS (CodeWeavers), and every problem in this repo's first two days turned out to
  be already fixed in their tree. Do not propose "stay on upstream so we can
  upstream things" — that is not the goal.
- **Version is 26.3.0, matching the installed CrossOver 26.3.** The source version
  must always equal the installed app version, upgraded together, because A/B-ing
  against a working CrossOver bottle produced *every* real finding so far.
- Strategic Command's WoW64 crash and its menu latency are **solved, and now by the
  base rather than by patches of ours** — both re-verified on WineCX 26.3.0.
- ⚠️ **The `--in-process-gpu` question is NOT closed — it was reopened on 2026-07-31.**
  The previous session recorded that `CX HACK 23950` made `wine-0004` unnecessary.
  Tested: false. Steam is black without `wine-0004` on WineCX, the 23950 path never
  fires for CEF windows, and CrossOver never modified `winemac.drv/surface.c`. 23950
  keys on a foreign *parent*; our bug is a foreign *painter*. The open question is
  again **why CEF runs its GPU in-process under CrossOver** — their bottle spawns no
  `--type=gpu-process`. See `RUNTIME.md` §2.
  Worth generalising from: that claim came from grepping their source for a plausible
  fix and reasoning forward. It read as settled for a day. Grep finds candidates;
  only running the thing decides.

## ⚠️ Three traps that will bite you

**1. The bundled-libs work does NOT come free from CrossOver.**
A CX tree built with plain flags produces **zero** bundled dylibs — that is what the
first CX-based runtime here did, and it was unusable for it. Wine `dlopen`s libraries by
bare soname and macOS has no `/etc/ld.so.conf.d/`, so they silently go missing — and
the failure modes all look like unrelated application bugs: no Vulkan (Steam decides
there is no GPU), no freetype (no fonts, or no text at all in DirectWrite apps), no
gnutls (no TLS, so Steam cannot log in). `bin/build-runtime` seeds configure with
absolute paths into a bundle built by `bin/bundle-libs`, and fails the build if any
soname comes out bare. **This is the one genuinely original thing here and the easiest
to lose in a base change.** See `RUNTIME.md` §1. Do not "fix" it by setting
`DYLD_FALLBACK_LIBRARY_PATH` — that masks the regression instead of detecting it.

**2. CrossOver's tree needs no special configure flags — but its `bin/` is nearly
empty, and patching it silently no-ops.** Measured: our existing configure line builds
their tree as-is (`build-runtime --winecx` handles the rest). Two real differences:
their `TOP_INSTALL_LIB` installs **no app loaders**, so `$BIN/wineboot`, `$BIN/winecfg`
and `$BIN/msiexec` do not exist — call `wine <tool>` instead; and because an extracted
tarball is not its own git repo, `git -C "$SRC" apply` resolves up to the outer repo
and prints `Skipped patch '...'` **while exiting 0**, patching nothing. The source tree
is a git repo now and `build-runtime` refuses to build otherwise.

**3. `bin/mkprefix` has never been run end to end.** `prefixes/steam-base` predates it
and was provisioned by hand. A base change is the wrong moment to debug prefix
creation for the first time, so budget for it — or clone `steam-base` (APFS `cp -c` is
near-instant), though a fresh prefix is cleaner across a Wine-version change. Note
`mkprefix` does **not** install Steam; that is still manual.

## How to work here (hard-won, please follow)

- **When two builds of the same source differ, compare the binaries.** The audio bug
  was found by disassembling both `dsound.dll`s (`0 SSE / 1334 x87` vs `741 / 52`) after
  a one-file swap localised it. Source diffing had already exonerated the source.
- **Measure the thing, not a proxy.** The menu-latency bug had *normal* CPU, page
  faults and mach-syscall numbers — identical to CrossOver's. The thread was not
  working, it was waiting. Reading `ps`/`top` counters instead of the actual symptom
  wasted most of that investigation.
- **A difference question wants a diff, not a trace.** "Why does CrossOver behave
  differently?" is answered by diffing their tree. Tracing answers "what is
  happening?". `src/` holds vendor source (upstream Wine *and* CrossOver's) — it is
  gitignored, which makes it look like build output. It is not.
  `grep -rn 'CW HACK' src/crossover/sources/wine/` enumerates their intentional
  divergences.
- **Verify a control before believing a negative result.** msync is now properly
  *excluded* (enabled, confirmed active via `msync: up and running` + shm traces, jitter
  unchanged) rather than merely "not demonstrated". The same session produced two
  near-misses from bad controls: a three-runtime comparison whose clean result came from
  a runtime too degraded to reach the code path at all, and a `strings -el` check that
  silently proved nothing because macOS `strings` has no such flag. **A control that
  cannot fail is not a control.**
- **Do not write conclusions into `NEXT.md` while still investigating.** Doing that
  turned a guess into a stated finding and pointed the next reader away from the
  answer for a whole session. Record observations freely; defer conclusions.
- **When stuck, get a read-only external view.** `codex exec -s read-only -C <dir>
  "<prompt>" < /dev/null` (GPT is ~20× faster and cheap). It cracked two problems here
  that were not yielding. Note the `< /dev/null` — without it, `codex exec` blocks
  forever reading stdin.

## Practical notes

```sh
bin/killwine                 # kill everything, Steam included
bin/killwine --game-only     # kill only the game, leave Steam up (60 s -> 3 s iteration)
bin/killwine --deep          # exact lsof sweep, ~20 s, for orphans argv matching misses
bin/gamerun steam            # Steam client (winecx-26.3.0-sse + steam-winecx are now the defaults)
bin/gamerun strategic-command        # via steam.exe -applaunch (real path, needs a PLAY click)
bin/gamerun strategic-command-direct # runs the exe directly; needs Steam up + steam_appid.txt
bin/compare-bottle Steam <prefix>    # diff the CrossOver bottle against one of ours
```

- **Never run two prefixes' Steam clients at once** — they share `S:\steamapps`.
- **`bin/killwine` will also kill a running CrossOver bottle** (same argv patterns).
  Do not run it while A/B-ing against CrossOver.
- A fullscreen game takes its own macOS Space, so `screencapture` photographs the
  wrong one. Ask Gil to look, or send a ⌘⇧3 screenshot. Better: do not rely on the
  screen — the black-window-with-hourglass phase is *normal* for ~30 s.
- `WINEDEBUG` is overridable per run on the `-direct` profile, e.g.
  `WINEDEBUG=+seh,+pid bin/gamerun strategic-command-direct`.
- Useful channels here: `+seh` (exception records), `+virtual` (VM + the
  `virtual_setup_exception` warning), `+loaddll` (module map), `+timestamp`, `+bitblt`.
- `lldb` can attach but follow-up commands hang on this process; use `--batch` with
  `-o` flags in the background and kill it after ~45 s. The attach message alone
  carries the fault address and faulting instruction. `timeout` is not installed.

## Current state (end of 2026-07-31)

- **Base:** `runtimes/winecx-26.3.0-sse-x86_64` (the SSE fix; now `DEFAULT_RUNTIME`
  and named in every profile), built from `src/crossover/sources/wine`
  (now its own git repo: pristine tarball on branch `vendor`, tag `winecx-26.3.0`, our
  work on `games`). Every runtime carries a `PROVENANCE` file that `build-runtime`
  writes and asserts.
- **Prefix:** `prefixes/steam-winecx`, an APFS clone of `steam-base` (4.8 s, zero extra
  disk) with `wineboot -u` run on it — which keeps the Steam login and the `S:` library
  registration, the two steps only a human can redo.
- **`patches/series`:** `wine-0004` (`workaround`, and this base *needs* it) plus
  `wine-0005` (`debug`). The four CodeWeavers ports are in `patches/superseded/`.
- **Fallback:** `wine-11.14-x86_64` + `steam-base` still work, untouched, one env var
  away (`RUNTIME=… PREFIX=… bin/gamerun …`).
- Fixed on the way through: `get-addons` chose Wine Mono by guessing the source tree
  (would have handed this base an MSI Wine 11.0 rejects); `mkprefix`/`gamerun` called
  `$BIN/wineboot`/`$BIN/winecfg`, which **do not exist on a WineCX runtime** — their
  `TOP_INSTALL_LIB` installs no app loaders. Use `wine <tool>`.
- `src/cx26/` deleted, along with the superseded runtimes (`wine-cx26.3-x86_64`, the
  x87 `winecx-26.3.0-x86_64`, and the `-mt1015-`/`-cxdsound-` experiments) — ~14 GB.
  `winecx-26.3.0-clang-x86_64` is deliberately kept: its Steam-login regression is
  unresolved and debugging it should not require a 25-minute rebuild.

## What "done" looks like

Steam renders and logs in, Strategic Command reaches its menu with responsive menus
and plays a turn — all on a `winecx-26.3.0` runtime with a `PROVENANCE` file, and with
`patches/series` containing only deltas that are *ours* rather than ports of
CodeWeavers' fixes. Bonus, and the test that validates the whole pivot: Steam renders
with `WINE_APPEND_ARGS` unset, letting `wine-0004` be deleted.
