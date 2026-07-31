# Start instruction — next session

Paste this file (or just point at it) at the start of a new Claude session.

---

## What this repo is

`~/games` is a Wine-on-macOS gaming playground on an **Apple M1 Max (arm64), macOS
26.x**. All Wine code is x86_64, therefore running under **Rosetta 2**. The aim is not
to be a launcher — it is a development environment for macOS-specific Wine work
(`wine_mesa_gl` and similar), with real games as the test load.

## Your goal this session

**Rebase the runtime onto WineCX 26.3.0 (CrossOver's published Wine source), then get
Steam and Strategic Command WWII running on it.**

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
- Strategic Command's WoW64 crash, its menu latency, and the `--in-process-gpu`
  question are all **solved or closed**. See NEXT.md.

## ⚠️ Three traps that will bite you

**1. The bundled-libs work does NOT come free from CrossOver.**
`runtimes/wine-cx26.3-x86_64` has **zero** bundled dylibs. Wine `dlopen`s libraries by
bare soname and macOS has no `/etc/ld.so.conf.d/`, so they silently go missing — and
the failure modes all look like unrelated application bugs: no Vulkan (Steam decides
there is no GPU), no freetype (no fonts, or no text at all in DirectWrite apps), no
gnutls (no TLS, so Steam cannot log in). `bin/build-runtime` seeds configure with
absolute paths into a bundle built by `bin/bundle-libs`, and fails the build if any
soname comes out bare. **This is the one genuinely original thing here and the easiest
to lose in a base change.** See `RUNTIME.md` §1. Do not "fix" it by setting
`DYLD_FALLBACK_LIBRARY_PATH` — that masks the regression instead of detecting it.

**2. CrossOver's tree layout differs.** Their source is `sources/wine/`, not `wine/`,
and their configure flags are their own — do not blindly reuse ours.
`bin/build-runtime` currently assumes an upstream tree and needs teaching. Read
[`benstonezhang/winecx_build`](https://github.com/benstonezhang/winecx_build)
(`build.sh`, `brew_install.sh`) before writing your own recipe.

**3. `bin/mkprefix` has never been run end to end.** `prefixes/steam-base` predates it
and was provisioned by hand. A base change is the wrong moment to debug prefix
creation for the first time, so budget for it — or clone `steam-base` (APFS `cp -c` is
near-instant), though a fresh prefix is cleaner across a Wine-version change. Note
`mkprefix` does **not** install Steam; that is still manual.

## How to work here (hard-won, please follow)

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
- **Verify a control before believing a negative result.** "It wasn't msync" is
  recorded as *not demonstrated* rather than *excluded*, because nobody confirmed the
  env var reached the process.
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
bin/gamerun steam            # Steam client
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

## Current state (as of 2026-07-31)

- Working tree **clean**, 5 commits today, nothing running.
- The **existing upstream-based runtime works** and is a fallback: Strategic Command
  launches, reaches its menu with real-time menu response, and plays a turn.
  `patches/series` has `wine-0004`…`wine-0008`; every `src/wine` modification has a
  patch file.
- Known blemish: `wine-0005` is class `debug` (so `build-runtime` skips it) but is
  applied in `src/wine`, making the skip meaningless. Resolve during the rebase.
- `runtimes/wine-cx26.3-x86_64` is **misnamed** (built from 26.0.0 source) and has no
  bundled libs. `src/cx26/` is a 9.1 MB partial extraction — delete it.

## What "done" looks like

Steam renders and logs in, Strategic Command reaches its menu with responsive menus
and plays a turn — all on a `winecx-26.3.0` runtime with a `PROVENANCE` file, and with
`patches/series` containing only deltas that are *ours* rather than ports of
CodeWeavers' fixes. Bonus, and the test that validates the whole pivot: Steam renders
with `WINE_APPEND_ARGS` unset, letting `wine-0004` be deleted.
