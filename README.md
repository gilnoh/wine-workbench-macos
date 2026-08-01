# games — a Wine workbench for macOS on Apple Silicon

**Play your favourite Windows games on a Mac — and hack on the thing that runs them
while you do.**

This is not a launcher. There are good launchers already. This is a *workbench*: a
reproducible Wine build, a patch series, and a set of measurement tools, arranged so
that when a game misbehaves you can go all the way down — trace it, diff it against a
known-good reference, disassemble it, patch it, rebuild, and measure whether you were
right.

The split of intent is roughly:

- **50% selfish.** Get specific games running well on this machine, and tune for them.
- **50% useful to others.** macOS is where Wine is least explored. Everything learned
  here is written down in a form someone else can use — and the pieces worth
  contributing go upstream, to Wine or to CodeWeavers.

The eventual target is a macOS-specific graphics module — a `wine_mesa_gl`: something
that gives Wine a modern OpenGL path on macOS instead of the deprecated CGL one, which
caps out at GL 4.1. There are games that need exactly that; **Task Force Admiral**, a
GL-only title, is the example driving it here. That module does not exist yet — the
work so far has been building the foundation it would sit on.

> **Scope, honestly:** Apple Silicon (tested on an M1 Max), macOS 26.x, all Wine code
> x86_64 running under Rosetta 2. Nothing here is portable to Intel Macs or Linux
> without work, and much of what was learned is specifically about *translated* x86
> code.

---

## Current status

**Base:** [WineCX](#what-winecx-means) 26.3.0 — CodeWeavers' published CrossOver Wine
source (CrossOver 26.3.0 = Wine 11.0), not upstream Wine. The reasoning is in
`docs/NEXT.md`: every hard problem this project hit in its first days turned out to be
already solved in CodeWeavers' tree, and they are the people who actually ship Wine on
macOS.

| | |
| --- | --- |
| runtime | `runtimes/winecx-26.3.0-sse-x86_64` (built here; not in the repo) |
| our patches on top | **one** functional (`wine-0004`) + one diagnostic |
| Strategic Command WWII | runs — real-time menus, clean audio, plays a turn |
| Steam client | renders and logs in |
| gamepads | **broken** (SDL2/`winebus` crash, see `docs/RUNTIME.md` §1) |
| other games | Quasimorph, Battleship Command — 64-bit, untried |

**Open:** a 2 s → 20 s menu fade; clang as the PE compiler (correct codegen, but Steam
cannot log in on it); why CEF runs its GPU in-process under CrossOver and not for us.

---

## Findings worth stealing

You do not have to run any of this to get value from it. These are the things that cost
real time to find, and they apply to anyone building Wine on macOS:

- **32-bit PE DLLs default to x87 float math, and Rosetta 2 emulates x87 badly.**
  Built with mingw-gcc, our `dsound.dll` had **0 SSE / 1334 x87** instructions where
  CrossOver's shipped one has **741 / 52**. The audio mixer thread burned an entire CPU
  core (100% vs 1.5%) and the game's sound jittered. Fix: build PE with clang, or pass
  `-msse2 -mfpmath=sse` for the i386 target. 64-bit is unaffected — x86_64 mandates
  SSE2 — so this only ever bites 32-bit titles. `docs/RUNTIME.md` §4.
- **macOS has no `ld.so.conf`, so every library Wine `dlopen`s by soname goes silently
  missing.** No error — just no Vulkan, or no fonts, or no TLS, each of which looks
  like an unrelated application bug. Fix: bundle the libraries and seed configure's
  soname cache with absolute paths. `docs/RUNTIME.md` §1.
- **`git apply` can silently patch nothing.** Run against a source tree that is not its
  own git repo, it resolves to the *enclosing* repo, prints `Skipped patch '...'`, and
  **exits 0**. Every patch reports as applied; none is.
- **Build-time assertions beat documentation.** Each of the above produced a runtime
  that built fine, ran fine, and was quietly wrong. `bin/build-runtime` now fails the
  build on all of them.

---

## Directory structure

Only the top group is in the repository (~1.6 MB). Everything else is fetched or built
on your machine and is gitignored.

```
bin/          the tooling. build-runtime, mkprefix, gamerun, killwine, bundle-libs,
              get-addons, compare-bottle, provision-host
patches/      our deltas on the base, as a quilt-style series (patches/series)
                superseded/   were right, but the base now provides them — kept as history
                retracted/    were wrong — kept so the mistake stays visible
docs/         the real content. See "What to read" below
profiles/     one file per game: which runtime, prefix, backend, exe, environment
tools/probes/ small Windows binaries that each answer one question from inside a prefix

src/          vendor source: CodeWeavers' Wine tree, upstream Wine. NOT distributed
runtimes/     built Wine installations, each with a PROVENANCE file
prefixes/     Wine prefixes (C: drives)
library/      shared Steam library, mounted as S: in every prefix
backends/     graphics backends (DXMT, DXVK)
build/        build trees and logs
```

Two conventions worth knowing:

- **`src/` is reference material, not build output.** It is gitignored, which makes it
  look disposable. It is not — it holds the CrossOver and upstream Wine trees, and
  several answers in `docs/` came from diffing them.
- **Every runtime carries a `PROVENANCE` file** recording CrossOver version, Wine
  version, source tarball + sha256, PE compiler, deployment target, patches applied and
  the configure line. A directory name cannot carry that, and guessing it went wrong
  here once already.

---

## What to read

**If you are a human**, in this order:

1. This file.
2. `docs/RUNTIME.md` — what this environment changes from stock Wine, and *why*. The
   most reusable document here.
3. `docs/NEXT.md` — current state, open problems, and a long list of measured dead ends
   under "Do not re-chase these".
4. `docs/PROJECT.md` — history. Contains conclusions later disproved, marked as such.

**If you are an AI agent**, read `docs/START-HERE-NEXT-SESSION.md` first — it is written
for you: the goal, what is already settled, the traps that will bite you, and the
working rules. Then `docs/NEXT.md`. The rules that matter most, learned the hard way:

- **Measure the thing, not a proxy.** A latency bug had entirely normal CPU and syscall
  counts; the thread was not working, it was waiting.
- **A difference question wants a diff.** "Why does the working reference behave
  differently?" is answered by diffing the reference — or, when the source is identical,
  by diffing the *binaries*.
- **A control that cannot fail is not a control.** Two near-misses here came from
  "clean" results produced by a build too broken to reach the code path, and by a
  command-line flag that silently did nothing.
- **Do not write conclusions into the docs while still investigating.** Doing that once
  turned a guess into a stated finding and misled the next reader for a whole session.

---

## Building it

Requires macOS on Apple Silicon, Xcode command line tools, and Homebrew — both the
**x86_64** Homebrew in `/usr/local` and the arm64 one in `/opt/homebrew`.

```sh
# 1. host dependencies (x86_64 brew: mingw-w64, freetype, gnutls, molten-vk,
#    vulkan-loader, sdl2, bison, llvm/lld; arm64 brew: cabextract)
bin/provision-host                     # --check to verify without installing

# 2. the base source, from CodeWeavers (LGPL, no account needed, ~142 MB)
mkdir -p src/crossover && cd src/crossover
curl -O https://media.codeweavers.com/pub/crossover/source/crossover-sources-26.3.0.tar.gz
tar xzf crossover-sources-26.3.0.tar.gz        # -> sources/wine
cd sources/wine && git init -b vendor . && git add -A && git commit -m 'pristine' && cd -

# 3. build (~25 min). --pe-gcc forces SSE float math; see "Findings" above
bin/build-runtime --winecx 26.3.0 --pe-gcc

# 4. addons a source build does not include (Wine Mono, Gecko)
bin/get-addons winecx-26.3.0-sse-x86_64

# 5. a prefix, then install Steam into it by hand
bin/mkprefix mygames
```

Step 2's `git init` is not optional — `build-runtime` refuses to build without it, for
the `git apply` reason above.

**CrossOver itself is not required.** Its *sources* are LGPL and free to download, which
is all the build needs. If CrossOver.app is absent the build warns and proceeds; you
simply lose the A/B reference that produced most of the findings here. A CrossOver
install whose version *disagrees* with the source does stop the build — a reference that
silently lies is worse than none.

Two things that are genuinely rough: `bin/mkprefix` has never been run end to end, and
installing Steam into a prefix is still manual.

## Running things

```sh
bin/gamerun steam                     # the Steam client
bin/gamerun strategic-command         # a game, via steam.exe -applaunch
bin/gamerun <profile> --shell         # a shell with that profile's environment
bin/killwine                          # stop everything (also kills a running CrossOver bottle)
bin/killwine --game-only              # stop the game, leave Steam up

RUNTIME=wine-11.14-x86_64 PREFIX=steam-base bin/gamerun steam   # A/B another runtime
```

`RUNTIME`, `PREFIX` and `BACKEND` in the environment override the profile, which is how
every comparison in `docs/` was run.

---

## What WineCX means

Three version numbers get confused constantly, so:

- **CX = CrossOver, CW = CodeWeavers.** Both appear in their source as `CW HACK`,
  `CX HACK`, `CrossOver Hack`, inconsistently.
- **"WineCX-nn" is the community's name, not CodeWeavers'.** They publish only
  `crossover-sources-<ver>.tar.gz`.
- ⚠️ **The number is the CrossOver version, not the Wine version.** WineCX 26.3.0 =
  CrossOver 26.3.0 = **Wine 11.0**.

---

## Licence and attribution

- **`bin/`, `tools/`, `docs/`, `profiles/` — MIT.** See `LICENSE`. Take anything you
  find useful.
- **`patches/` — LGPL-2.1-or-later**, matching Wine, because each patch is a derivative
  work of Wine. See `patches/LICENSE`. This is deliberate: it is what lets these changes
  go to the Wine project or to CodeWeavers without a licence conflict.
- `patches/crossover26-*.diff` are **CodeWeavers' code**, not ours — excerpts of their
  published sources kept as reference, redistributed under the LGPL.

No Wine source, CrossOver source, CrossOver binaries, Steam, or game content is
distributed in this repository. All of it is fetched from its original source on your
machine.

**Not affiliated with or endorsed by CodeWeavers.** CrossOver is their trademark. This
project builds their *published* Wine sources, which they release under the LGPL, and it
is better off for their work — several problems solved here were solved by simply using
the fixes they had already written.
