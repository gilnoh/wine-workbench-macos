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

git clone --depth 1 --branch 1.28.7 \
    https://gitlab.freedesktop.org/gstreamer/gstreamer.git src/gstreamer
bin/build-gstreamer                        # ~10 min; build-runtime requires it

bin/build-runtime --winecx <VER> --pe-gcc  # ~25 min; --name <runtime-name> to pick the name
bin/get-addons <runtime-name>              # Wine Mono + Gecko; a source build has neither
```

GStreamer is **built, not installed**: x86_64 Homebrew has no bottle for current
macOS, and without GStreamer Media Foundation in this tree opens and decodes
nothing — games then fail in ways that look like their own bugs. The reasons and
the plugin list are in `bin/build-gstreamer`.

The `git init` is **required**, not tidiness: `build-runtime` refuses to build a source
tree that is not its own git repository, because `git apply` run inside another repo
silently skips every hunk and still exits 0.

Only CodeWeavers' published sources are needed. Nothing in this repository builds,
runs, configures itself against, or checks for an installed copy of CrossOver.app — the
runtime is self-contained, and `--winecx <ver>` names the *source* version and nothing
else.

### Make a prefix and install a game

```sh
bin/mkprefix <name>                      # wineboot + provisioning + mounts S:
bin/mkprefix <name> --cjk                # ... plus CJK fonts (~380 MB, slow)
```

Provisioning installs `corefonts`, `d3dcompiler_47` and `vcrun2022`, then Wine Mono if
the runtime carries it. Each is there for a reason recorded in the script; `vcrun2022` in
particular is asserted afterwards, because Wine ships its own builtin `vcruntime140.dll`
and a prefix that never got the native one looks identical until something loads it.

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
- **`bin/killwine` selects by argv pattern, not by prefix**, so it will also kill Wine
  processes belonging to *other* Wine installations on the machine. Know what else is
  running before you use it.
- **A launched game often shows a black window with an hourglass for ~30 s.** That is
  normal and carries no diagnostic information.
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

- **Compare against a working reference.** If you have some *other* environment that
  runs the same game correctly, diffing it against ours is the strongest tool available.
  That reference is a property of your machine, not of this repository — none is
  required, and none is assumed anywhere in the build or the runtime.
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
- **`$?` after a pipeline is the *last* command's status.** `cmd | tail` reports whether
  `tail` succeeded, which it essentially always does — so `some-script | tail -30; echo $?`
  prints 0 for a script that died. This produced a false pass here three times in one day:
  a `mkprefix` run that aborted midway and was read as clean, `ps -M` parsing, and a wine
  exit code. Decide which command's status you mean, then get it:

  ```sh
  some-script | tail -30; echo "${PIPESTATUS[0]}"   # the script's status, not tail's
  some-script > out.log 2>&1; echo $?               # or do not pipe at all
  tail -30 out.log
  ```

  The same mechanism bites in the other direction *inside* a script: with `set -o pipefail`
  a failing `ls` at the head of a pipeline propagates past a successful `head`, and under
  `set -e` that kills the script on what looks like a harmless assignment
  (`x=$(ls glob 2>/dev/null | head -1)`). Remedies, in order of preference:

  1. **Restructure so the thing you care about is not mid-pipeline.** A glob loop cannot
     fail, so the branch after it stays reachable — this is what `bin/mkprefix` now does.
  2. `${PIPESTATUS[@]}` when you genuinely need a pipeline's component statuses.
  3. `set +e` scoped to the one line, and back on immediately.

  Prefer (1). It is the only one that also makes the code say what it means.

  That bug is worth stating precisely, because the precise version is worse: `bin/mkprefix`
  never worked at all. The Mono step arrived with the script in `c8efc2e` (2026-07-30), the
  repo's first commit is 2026-07-29, and the fault was found on 2026-08-01 — so it was
  broken from the day it was written and nobody had run it end to end in between.
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
bin/compare-bottle <bottle> <prefix>     # dev aid: diff a separately-installed
                                         # CrossOver bottle against one of our prefixes
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

### Contributing outward — CodeWeavers, not upstream

Patches are LGPL-2.1-or-later (see `patches/LICENSE`) so they *can* go to either the Wine
project or CodeWeavers unchanged. But the default audience is **CodeWeavers**, not
wine-devel: this is a macOS workbench built on their tree, and they are one of Wine's
major contributors, so what they know upstream effectively knows.

Before proposing to report anything, check whether CrossOver already handles it:

```sh
grep -rn 'CW HACK\|CX HACK' src/crossover/sources/wine/
```

If they carry a hack for it, or deliberately took another path, **there is nothing to
report** — record it in `docs/` and stop. Do not reflexively suggest upstreaming; that is
optimising for a virtue this project does not need.

---

## The vendor source repository

`src/crossover` is a git repository of its own (`winecx-sources`), laid out exactly like
CodeWeavers' tarball:

```
src/crossover/
├── README.md  NOTICE  RELEASES.md  winecx.sh   ← ours
└── sources/wine/                               ← byte-identical to the tarball
```

**The invariant: nothing of ours ever goes inside `sources/`.** That is what keeps
"byte-identical" checkable rather than merely claimed — `./winecx.sh verify 26.3.0`
downloads the tarball and diffs it. Our patches live in this workbench's `patches/`, not
there.

Releases are commits on `main`, tagged `winecx-<cxver>`. Adding one is
`./winecx.sh add 26.4.0`, which branches, replaces `sources/wine` wholesale (so files
dropped upstream really disappear), records the sha256 in the commit and `RELEASES.md`,
and leaves a PR to open. After merging, tag it. Then:

```sh
git -C src/crossover diff --stat winecx-26.3.0 winecx-26.4.0 -- sources/
```

is the question this repository exists to answer quickly.

That repository is **local-only and not published**; publishing is a decision for when
there is something of ours worth pairing with it. Reproducibility never depends on it —
the tarball is a public download and every runtime's `PROVENANCE` records its sha256.

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

**`toolchains/` is affected too, and it is the one nobody looks at.** A Python venv's
scripts are shebanged with an absolute path to their own interpreter, so after a rename
`toolchains/venv-meson/bin/meson` reports

```
bad interpreter: /old/path/toolchains/venv-meson/bin/python3.14: no such file
```

Found 2026-08-14, months after the rename, when the first backend rebuild needed it.
`bin/build-backend` now recreates that venv itself rather than trusting it — the pin
matters (Homebrew's meson 1.10.1 crashes on DXMT's cross build with an internal
`KeyError`), so "meson is installed" is not the same as "meson works here".

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
