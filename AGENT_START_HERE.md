# Agent guide

You are working in a Wine workbench for macOS on Apple Silicon. Read `README.md` for
what the project is; this file is how to *operate* it.

Nothing here is time-varying. Current status, open bugs and what to do next live in
`docs/NEXT.md` — read that too, but do not copy its contents into this file.

There are two things a user asks for here, and they overlap heavily:

- **"I want to play a game."** Set the environment up, get the game running, keep it
  working.
- **"I want to fix or build something."** Same environment, plus the ability to change
  Wine, measure the result, and record it so it is worth something later.

Treat the second as the first plus a method. The environment is identical.

---

## Ground rules

- **The host is Apple Silicon, macOS, and every Wine binary is x86_64 under Rosetta 2.**
  This matters constantly: translated x86 code has different performance characteristics
  from native, and one whole class of bug here comes from that (see `RUNTIME.md` §4).
- **The base is CodeWeavers' published CrossOver Wine ("WineCX"), not upstream Wine.**
  Decided deliberately; the reasoning is in `docs/NEXT.md`. Do not propose switching to
  upstream for purity — the point is to be adoptable by the people who ship Wine on
  macOS.
- **Before deriving a fix, check whether the base already has it.**
  `grep -rn 'CW HACK\|CX HACK' src/crossover/sources/wine/` lists CodeWeavers'
  intentional divergences. Re-deriving one of their fixes is wasted work and has
  happened here more than once.
- **`src/` is reference material, not build output.** It is gitignored, which makes it
  look disposable. It holds the vendor Wine trees, and several answers came from diffing
  them.
- **Never claim a result you have not measured.** This repository's documentation style
  is to record the correction, not only the conclusion. If something is inferred, say
  inferred.

---

## Mode 1 — set it up so games run

### Build the runtime

```sh
bin/provision-host                       # Homebrew deps; --check to verify only
                                         # x86_64 brew in /usr/local AND arm64 in /opt/homebrew

mkdir -p src/crossover && cd src/crossover
curl -O https://media.codeweavers.com/pub/crossover/source/crossover-sources-<VER>.tar.gz
tar xzf crossover-sources-<VER>.tar.gz    # -> sources/wine
cd sources/wine && git init -b vendor . && git add -A && git commit -m 'pristine'

bin/build-runtime --winecx <VER> --pe-gcc  # ~25 min
bin/get-addons <runtime-name>              # Wine Mono + Gecko; a source build has neither
```

The `git init` is **required**, not tidiness: `build-runtime` refuses to build a source
tree that is not its own git repository, because `git apply` run inside another repo
silently skips every hunk and still exits 0.

CrossOver.app itself is optional — its *sources* are all the build needs. If it is
installed, its version must match the source version, or the build stops; that reference
is the project's main investigative tool and a mismatched one lies silently.

### Make a prefix and install a game

```sh
bin/mkprefix <name>                      # wineboot + provisioning + mounts S:
```

Then install the game by hand. Steam specifically:

1. Run the Steam installer inside the prefix (`wine SteamSetup.exe`).
2. Log in — only a human can do this.
3. **Register the shared library**: Steam > Settings > Storage > `+` > `S:\`. Mounting
   `S:` is not enough; Steam ignores a drive it has not been told about, and games will
   install into the prefix instead, costing a re-download later.

Cloning an existing prefix is often better than making a new one: APFS `cp -Rc` is
instant and costs no disk, and it preserves the login and the library registration —
the two steps a human cannot skip. Follow it with `wine wineboot -u` if the runtime
changed.

### Add a game

Copy `profiles/EXAMPLE.env` to `profiles/<game>.env` and set the runtime, prefix, exe and
any environment. Then:

```sh
bin/gamerun <game>                       # launch
bin/gamerun <game> --shell               # a shell with that environment
bin/killwine                             # stop everything
bin/killwine --game-only                 # stop the game, leave the Steam client up
```

`RUNTIME=`, `PREFIX=` and `BACKEND=` in the environment override the profile. That is how
every A/B comparison in `docs/` was run.

### Structural facts that will confuse you otherwise

- **Never run two prefixes' Steam clients at once** — they share `S:\steamapps` and will
  fight over install state.
- **`bin/killwine` also kills a running CrossOver bottle** (same argv patterns). Never
  run it while comparing against CrossOver.
- **A launched game often shows a black window with an hourglass for ~30 s.** That is
  normal — CrossOver does the same. It carries no diagnostic information.
- **Wine rewrites a PE process's argv *and* its `comm`** to the Windows image name, and
  orphans reparent to PID 1. A process you are looking for may appear as bare
  `start.exe` with no path. `bin/killwine --deep` sweeps by open files instead.
- **`ps -M` and `ps -eo command` print different strings for the same process.** Select a
  target by highest CPU rather than by grepping for a name; a wrapper process whose argv
  merely *contains* the game path will otherwise be measured instead of the game.
- **A fullscreen game takes its own macOS Space**, so `screencapture` photographs the
  wrong one. Ask the user what they see.

---

## Mode 2 — set it up so you can fix things

Everything above, plus:

### The patch series

`patches/series` is the whole contract. Each line is a class and a file:

| class | meaning |
| ----- | ------- |
| `fix` | upstreamable; the goal is to send it away and delete it here |
| `workaround` | ours to carry until the base changes |
| `debug` | diagnostic only; **not** applied unless `--with-debug` |

`build-runtime` applies them in order and then asserts that the source tree differs from
its vendor tag **only** by those patches. Editing the source tree directly is therefore a
build failure, not a shortcut — that assertion exists because a change once sat in the
tree with no patch file and was silently dropped from every clean rebuild.

If a patch you are writing turns out to be a port of a `CW HACK`, that is a smell: take
it from the base instead.

### Rebuilding after a change

`build-runtime --force` reuses the existing build tree, so a one-DLL change is minutes,
not a full rebuild. It re-applies the series, re-runs its assertions and rewrites
`PROVENANCE`. Build into a **new `--name`** when you are testing a build variable — that
keeps the working runtime intact and makes the two directly comparable.

Every runtime carries a `PROVENANCE` file: CrossOver version, Wine version, source
tarball + sha256, source commit, PE compiler, deployment target, patches applied, and the
configure line. Read it before drawing conclusions about a runtime; a directory name
cannot carry that, and guessing it went wrong here once already.

### How to investigate

The method that has produced every real finding here:

- **Compare against a working reference.** A version-matched CrossOver bottle running the
  same game is the strongest tool available. `bin/compare-bottle` diffs a bottle against
  one of our prefixes.
- **A difference question wants a diff, not a trace.** "Why does the reference behave
  differently?" is answered by diffing the reference. When the *source* is identical,
  diff the **binaries** — `objdump -d` on the PE DLLs has settled at least one question
  that source reading could not.
- **Measure the symptom, not a proxy.** A latency bug here had entirely normal CPU, page
  fault and syscall counts. The thread was not working, it was waiting.
- **A control that cannot fail is not a control.** Two near-misses came from a "clean"
  result produced by a build too broken to reach the code path, and from a command-line
  flag that silently did nothing. Before believing a negative, prove the test could have
  detected a positive.
- **Isolate one variable.** When a build behaves differently, change one thing and
  rebuild under a new name. Swapping a single file between two runtimes is often the
  fastest way to localise a fault to code versus environment.
- **When stuck, get a fresh read-only view.** `codex exec -s read-only -C <dir> "<prompt>"
  < /dev/null` is fast and cheap. Note the `< /dev/null` — without it, it blocks forever
  reading stdin.

### The measurement toolkit

```sh
WINEDEBUG=+seh,+pid,+virtual,+loaddll,+timestamp,+bitblt   # useful channels
bin/gamerun <profile>                    # profiles default WINEDEBUG through ${WINEDEBUG:-...}
tools/probes/                            # small Windows binaries, one question each
bin/compare-bottle <bottle> <prefix>     # DLL overrides, native DLLs, bottle config
ps -M <pid>                              # per-thread CPU and priority (parse per row:
                                         #   header row has PRI in field 6, thread rows in 4)
sample <pid> 5 10                        # thread names; it cannot attribute CPU
objdump -d <pe.dll>                      # PE disassembly works on Wine's built DLLs
```

`lldb` attaches but follow-up commands tend to hang on these processes; use `--batch`
with `-o` flags and kill it after ~45 s. The attach message alone carries the fault
address.

### Recording what you find

- **`docs/RUNTIME.md`** — durable: what this environment changes from stock Wine and why.
  Findings that will still be true next year go here.
- **`docs/NEXT.md`** — current: state, open problems, and the "do not re-chase these"
  list of measured dead ends. Add refuted hypotheses there *with their evidence*; that
  list is worth more than it looks.
- **`patches/*/README.md`** — why a patch exists, or why it was retracted or superseded.
- **Do not write conclusions while still investigating.** Doing that once turned a guess
  into a stated finding and misled the next reader for an entire session. Record
  observations freely; defer conclusions until the question is closed.

### Contributing outward

Patches are LGPL-2.1-or-later (see `patches/LICENSE`) precisely so they can go to the
Wine project or to CodeWeavers unchanged. When a `fix`-class patch is genuinely
upstreamable, that is the goal — deleting it from `patches/series` because upstream took
it is the best possible outcome.

---

## The vendor source repository

`src/crossover/sources/wine` is its own git repository (required — see above). Its layout
is deliberate:

| ref | contents |
| --- | --- |
| `vendor` | one commit per CrossOver release, **byte-identical** to `sources/wine` from that release's tarball |
| `winecx-<cxver>` tags | the release points on `vendor` |
| `main` | an orphan branch holding `README.md` + `add-release.sh` — kept off `vendor` so the source stays pristine |
| others | work branches, based on a tag |

Keeping `vendor` pristine is what makes a diff against it mean anything, and what stops
our changes from silently becoming "the base". Upgrading is scripted — `add-release.sh` on
the `main` branch downloads a release, records its sha256, replaces the tree wholesale,
commits and tags, then reports how many files changed since the previous tag:

```sh
git -C src/crossover/sources/wine show main:add-release.sh > /tmp/add-release.sh
chmod +x /tmp/add-release.sh && /tmp/add-release.sh 26.4.0 src/crossover/sources/wine
```

Then rebase work branches onto the new tag. Releases share almost every file, so each one
costs very little history.

That repository is **local-only and not published**. It is a convenience: the tarball is a
public download and every runtime's `PROVENANCE` records its sha256, so reproducibility
never depends on it. Its `README.md` is already written on the `main` branch should it
ever be pushed (as `winecx-sources`) — it is explicit that the repository is a companion
rather than a mirror, makes no promise to track releases, and points at CodeWeavers'
original as the thing to prefer.

## A built tree is not relocatable

Building bakes absolute paths in. `configure` is seeded with absolute soname paths so
that Wine's `dlopen`s resolve without any environment (`RUNTIME.md` §1), which means the
runtime contains lines like:

```
#define SONAME_LIBVULKAN "/abs/path/runtimes/<name>/lib/bundled/libvulkan.1.dylib"
```

Move or rename the checkout and those paths stop resolving — **silently**, in exactly the
way the bundling design exists to prevent: no error, just no Vulkan, no fonts, no TLS.
Prefixes have the same issue: `dosdevices/s:` is an absolute symlink to `library/`, and
`build/` trees hold absolute `srcdir`/`prefix`.

Cloning to a new machine or path is fine — you build there, so the paths are correct.
It is *moving an existing built tree* that breaks. If you must:

```sh
ln -s /new/path /old/path        # 1. compatibility symlink: everything keeps resolving
ln -sfn /new/path/library /new/path/prefixes/<pfx>/dosdevices/s:   # 2. repoint prefixes
bin/build-runtime --winecx <VER> --pe-gcc --force                  # 3. rebuild at leisure
```

After the rebuild the old paths are gone from the runtime and the compatibility symlink
can be removed. Verify with:

```sh
grep -E '^#define SONAME_LIB' build/<name>/include/config.h    # must point at the NEW path
```

## Things not to do

- **Do not set `DYLD_FALLBACK_LIBRARY_PATH` to fix a missing library.** It masks a
  bundling regression, and that class of failure is invisible when masked: no error, just
  no Vulkan, or no fonts, or no TLS. Fix the bundle instead (`RUNTIME.md` §1).
- **Do not ship CrossOver binaries in a runtime.** Transplanting one is a legitimate
  *diagnostic* — it localises a fault to code versus environment — but it is not a
  configuration to keep.
- **Do not point a runtime at a prefix built by a very different Wine version** without
  expecting a `wineboot -u`, and never at a prefix that took a human hours to provision.
- **Do not delete a runtime or prefix without checking for live processes** (`lsof` on
  its `ntdll.so`).
