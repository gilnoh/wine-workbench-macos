# games — a Wine workbench for macOS on Apple Silicon

**Play your favourite Windows games on a Mac — and hack on the thing that runs them
while you do.**

This is not a launcher. There are good launchers already. This is a *workbench*: a
reproducible Wine build, a patch series, and a set of measurement tools, arranged so that
when a game misbehaves you can go all the way down — trace it, diff it against a
known-good reference, disassemble it, patch it, rebuild, and measure whether you were
right.

The split of intent is roughly:

- **50% selfish.** Get specific games running well on this machine, and tune for them.
- **50% useful to others.** macOS is where Wine is least explored. Everything learned
  here is written down in a form someone else can use — and the pieces worth
  contributing go outward, to Wine or to CodeWeavers.

The longer-term target is a macOS-specific graphics module — a `wine_mesa_gl`: something
that gives Wine a modern OpenGL path on macOS instead of the deprecated CGL one, which
caps out at GL 4.1. There are games that need exactly that; **Task Force Admiral**, a
GL-only title, is the example driving it here.

## Scope

**Apple Silicon only.** Every Wine binary here is x86_64 running under **Rosetta 2**, on
**macOS**. That is not incidental — a substantial part of what this project has learned
is specifically about how *translated* x86 code behaves, and none of it transfers
unchanged to Intel Macs or to Linux.

The base is **WineCX**: CodeWeavers' published CrossOver Wine source, not upstream Wine.
They are the people who actually ship Wine on macOS, so their tree is both the realistic
integration target and the only one load-tested on this platform. CrossOver itself is not
required to build or run anything here — only their freely published LGPL sources are.

> **What "WineCX 26.3.0" means:** the number is the **CrossOver** version, not the Wine
> version. WineCX 26.3.0 = CrossOver 26.3.0 = Wine 11.0. Three version numbers get
> confused constantly; every runtime therefore carries a `PROVENANCE` file recording all
> of them, plus the source tarball, its sha256, the compiler and the configure line.

---

## Layout

Only the first group is in the repository (~1.6 MB). Everything else is fetched or built
on your machine, and is gitignored.

```
README.md            this file
AGENT_START_HERE.md  the same, for an AI agent operating a copy of this repo

bin/                 the tooling: build-runtime, mkprefix, gamerun, killwine,
                     bundle-libs, get-addons, compare-bottle, provision-host
patches/             our deltas on the base, as a quilt-style series (patches/series)
                       superseded/  were right, but the base now provides them
                       retracted/   were wrong — kept so the mistake stays visible
docs/                the real content; see below
profiles/            one file per game: runtime, prefix, backend, exe, environment
tools/probes/        small Windows binaries that answer one question each, from
                     inside a prefix

src/                 vendor source: CodeWeavers' Wine tree, upstream Wine — NOT
                     distributed here, and reference material rather than build output
runtimes/            built Wine installations, each with a PROVENANCE file
prefixes/            Wine prefixes (C: drives)
library/             shared game library, mounted as S: in every prefix
backends/            graphics backends (DXMT, DXVK)
build/               build trees and logs
```

## What to read

| | |
| --- | --- |
| **`AGENT_START_HERE.md`** | how to operate this repo, in two modes: set it up so games run, and set it up so you can fix things. Written for an AI agent; equally usable by a human. |
| **`docs/RUNTIME.md`** | what this environment changes from stock Wine, and *why*. The most reusable document here — the platform findings live in it, and they apply to anyone building Wine on macOS, whether or not they use this tooling. |
| **`docs/NEXT.md`** | the only file that tracks *current* state: what works, what is open, and a long list of measured dead ends under "Do not re-chase these". |
| **`docs/PROJECT.md`** | history. Contains conclusions later disproved, marked as such. |

Some of what is in `RUNTIME.md`, as a taste: why libraries Wine `dlopen`s go missing on
macOS with no error at all; why 32-bit floating-point code can be an order of magnitude
slower than it should be under Rosetta; and how a patch series can be applied with `git
apply` reporting success while patching nothing.

---

## Building and running

Requires macOS on Apple Silicon, Xcode command line tools, and **both** Homebrews — the
x86_64 one in `/usr/local` and the arm64 one in `/opt/homebrew`.

```sh
# 1. host dependencies
bin/provision-host                     # --check verifies without installing

# 2. the base source, from CodeWeavers (LGPL, no account needed, ~142 MB)
mkdir -p src/crossover && cd src/crossover
curl -O https://media.codeweavers.com/pub/crossover/source/crossover-sources-26.3.0.tar.gz
tar xzf crossover-sources-26.3.0.tar.gz        # -> sources/wine
cd sources/wine && git init -b vendor . && git add -A && git commit -m 'pristine'

# 3. build (~25 min)
bin/build-runtime --winecx 26.3.0 --pe-gcc

# 4. addons a source build does not include
bin/get-addons winecx-26.3.0-sse-x86_64

# 5. a prefix; then install the game into it
bin/mkprefix mygames
```

The `git init` in step 2 is not optional — `build-runtime` refuses to build without it,
for reasons `AGENT_START_HERE.md` explains.

```sh
bin/gamerun <profile>                  # launch a game or the Steam client
bin/gamerun <profile> --shell          # a shell with that profile's environment
bin/killwine                           # stop everything
bin/killwine --game-only               # stop the game, leave the client up

RUNTIME=<other> PREFIX=<other> bin/gamerun <profile>   # A/B against another build
```

---

## Licence and attribution

- **`bin/`, `tools/`, `docs/`, `profiles/` — MIT** (`LICENSE`). Take anything useful.
- **`patches/` — LGPL-2.1-or-later** (`patches/LICENSE`), matching Wine, because each
  patch is a derivative work of Wine. That is deliberate: it is what lets these changes
  go to the Wine project or to CodeWeavers without a licence conflict.
- `patches/crossover26-*.diff` are **CodeWeavers' code**, kept as reference and
  redistributed under the LGPL — not ours.

No Wine source, CrossOver source or binaries, Steam, or game content is distributed in
this repository. All of it is fetched from its original source on your machine.

**Not affiliated with or endorsed by CodeWeavers.** CrossOver is their trademark. This
project builds their *published* Wine sources, which they release under the LGPL, and it
is better off for their work — several problems solved here were solved by simply using
fixes they had already written.
