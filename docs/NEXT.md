# Continue here

> **New session?** Start with **`docs/START-HERE-NEXT-SESSION.md`** — it is the
> orientation brief: the goal, what is already settled, the three traps that will
> bite you, and how to work in this repo. Then come back here for the detail.

## ⚠️ THE PIVOT — read this before anything else (decided 2026-07-30)

### DECISION

**Our base is WineCX — CodeWeavers' published CrossOver Wine source — not upstream
Wine.** Do not start new work against `src/wine` (upstream 11.14).

`patches/series` keeps its role, but from now on it holds **only our own deltas on
top of WineCX**. If a patch there is a port of a `CW HACK`, that is a smell: it
means we are re-deriving, and the answer is to take it from the base instead.

**VERSION: WineCX 26.3.0.** Decided 2026-07-31. Gil is renewing the CrossOver
licence to run **CrossOver 26.3**, so the installed app and our source tree match.

**That matching is a hard rule, not a nicety.** The A/B reference against a working
bottle was decisive **four separate times** in a single session — the `lretq` diff,
the `WINEMSYNC=1` discovery, the `winmm/time.c` null result, and the paint hacks.
Every real finding came from it. So:

> **Rule: the WineCX source version must equal the installed CrossOver.app version.**
> When one is upgraded, upgrade the other in the same change.

Verify they agree with:

```sh
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
  /Applications/CrossOver.app/Contents/Info.plist        # expect 26.3
cat runtimes/winecx-*/PROVENANCE                          # expect crossover_version: 26.3.0
```

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

### THE REBUILD — start the next session here

Goal: get back to **Steam running and Strategic Command playable** on a WineCX
26.3.0 base, and find out how much of our workaround pile is still needed. Expect
most of it to be unnecessary.

**Step 0 — prerequisites.** CrossOver 26.3 installed (licence renewed), and its
version confirmed to match (command above). Keep the working
`~/Library/Application Support/CrossOver/Bottles/Steam` bottle: it is the reference.

**Step 1 — get the source.**
```sh
cd src
curl -O https://media.codeweavers.com/pub/crossover/source/crossover-sources-26.3.0.tar.gz
shasum -a 256 crossover-sources-26.3.0.tar.gz     # record this in PROVENANCE
tar xzf crossover-sources-26.3.0.tar.gz           # -> sources/wine
```
Then **delete the junk**: `src/cx26/` (9.1 MB, only `dlls/`, provenance unknown) and
optionally the old `src/crossover-sources-26.0.0.tar.gz` once 26.3.0 is in place.

**Step 2 — teach `bin/build-runtime` about WineCX.** It currently hardcodes
assumptions about an upstream tree. Needs:
  - a source-tree argument or `WINECX_SRC` (their layout is `sources/wine/`, not `wine/`)
  - the output naming convention `runtimes/winecx-<cxver>-x86_64/`
  - **write and assert a `PROVENANCE` file** (crossover_version, wine_version,
    source_tarball, sha256, built-date) so a directory name never has to carry three
    facts again — we already got that wrong once with `wine-cx26.3-x86_64`
  - keep the soname-seeding + `bin/bundle-libs` step. **This is the part that does not
    come free from CrossOver:** their shipped app bundles libraries its own way, and
    our `runtimes/wine-cx26.3-x86_64` has **zero** bundled dylibs. Skip this and you
    silently lose freetype, vulkan and gnutls (see `RUNTIME.md` §1 for the symptoms —
    they look like unrelated application bugs).
  - CrossOver's tree wants its own configure flags; check theirs rather than reusing
    ours verbatim. `benstonezhang/winecx_build` (`build.sh`, `brew_install.sh`) is
    worth reading first.

**Step 3 — retire the patch pile.** Set `patches/series` to a clean slate for the new
base. Expected outcome for each:

| patch | expectation on WineCX |
| ----- | --------------------- |
| `wine-0006` (WoW64 mode switch) | **delete** — is `CW HACK 20760`, already in base |
| `wine-0007` (async WM_NCPAINT) | **delete** — is `CW Hack 20969/23427`, already in base |
| `wine-0008` (GL forward-compat) | **delete** — is `CW Hack 24834`, already in base |
| `wine-0004` (`WINE_APPEND_ARGS`) | **probably delete** — see Step 5 |
| `wine-0005` (surface-flush trace) | keep as class `debug` if still wanted, but this time do **not** leave it applied in the source tree |

**Step 4 — bring up the prefix and Steam.** `bin/mkprefix` + install Steam. Note
`mkprefix` has still never run end to end, so budget time for it. Alternatively clone
`prefixes/steam-base` (APFS `cp -c` is near-instant) — but a prefix created by
upstream 11.14 being used by CrossOver's 11.0 may want a `wineboot`; a fresh prefix is
cleaner for a base change.

**Step 5 — the test that validates the whole pivot.**
**Does Steam render with `WINE_APPEND_ARGS` unset?** Remove the injection from
`bin/gamerun` (or override it empty) and launch. If `CX HACK 23950` does its job the
window renders, and `wine-0004` plus the entire mechanism can go — closing a problem
that consumed most of this repo's first two days. See `RUNTIME.md` §2.

**Step 6 — regression-check the game.** Strategic Command should launch, reach its
menu with **real-time** menu response, and play a turn. If menus are slow again,
something in the base differs from our expectation — start from
`patches/wine-0007`'s notes.

**Step 7 — then the open items:** the 2 s → 20 s menu fade, and the other two games
(Quasimorph, Battleship Command — both 64-bit Unity/D3D11, so DXMT territory and
untouched by any of the 32-bit work).

### What carries over, and what does not

- **Carries over:** all of `bin/` (`build-runtime`, `bundle-libs`, `mkprefix`,
  `gamerun`, `killwine`, `get-addons`, `compare-bottle`), `profiles/`,
  `tools/probes/`, the bundled-libs/soname-seeding approach (`RUNTIME.md` §1), and
  the documented negative results below.
- **Becomes redundant:** `wine-0004` (23950 replaces it), `wine-0006` (= 20760),
  `wine-0007` (= 20969/23427), `wine-0008` (= 24834). All four are CrossOver ports
  or workarounds for things CrossOver fixes. Keep them until the rebase lands so
  the game keeps working; delete them after.
- **Watch out:** `runtimes/wine-cx26.3-x86_64` **is misnamed** — it is CX-built
  (contains the `lretq` thunk) but we only ever had 26.0.0 source, so "26.3" is
  unverified. And it has **zero bundled libs**, so `RUNTIME.md` §1's work must be
  re-applied to any CX-based runtime or you silently lose freetype/vulkan/gnutls.
  `src/cx26/` is a 9.1 MB partial extraction (only `dlls/`) — junk, delete it.

---

## Start here (handover)

Read this file, then `RUNTIME.md`. `PROJECT.md` is history — useful, but it
contains conclusions that were later disproved and are marked as such.

Everything below this line describes the **current, upstream-based** tree. It is
still accurate and the game runs, but see the pivot above before extending it.

**The environment works.** Steam runs, renders and logs in with no environment
variables. Don't re-derive any of that.

**Reproducibility, precisely** (this claim used to be overstated): every
modification in `src/wine` now has a patch file in `patches/series`, so
`bin/build-runtime` from a clean checkout reproduces the runtime — with one known
difference. `wine-0005` is class `debug`, so a clean build **omits** it, whereas the
currently-installed runtime **has** it applied (it was applied directly in
`src/wine`, which made the class-`debug` skip meaningless). A clean rebuild is
therefore correct-but-not-byte-identical. Resolve by reverting `wine-0005` in
`src/wine`, or reclassifying it.

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

`runtimes/wine-cx26.3-x86_64` (already built from their source) contains the
workaround, so it can smoke-test the diagnosis without a rebuild — but **do not
point it at `prefixes/steam-base`**; an 11.14 → 11.0 downgrade risks a prefix that
took hours to provision. Use a throwaway `bin/mkprefix` prefix.

## Next actions, in order

**0. THE PIVOT (see top of file).** Rebase onto WineCX — CodeWeavers' published
CrossOver Wine source, **version 26.3.0**, matching the installed CrossOver 26.3 so
the A/B reference survives. Follow "THE REBUILD" at the top of this file. Everything
below is either superseded by it or unaffected.

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
   another CrossOver paint/timer hack. Good first target once rebased.

2. **Then the other two games** — Quasimorph and Battleship Command, both Unity
   D3D11, so DXMT is the intended backend. Note they are **64-bit**, so they do
   not go through the WoW64 thunk at all and are very likely unaffected by the
   bug above. Worth trying early: they may just work.

3. **Fold prefix provisioning into a fresh prefix.** `bin/mkprefix` now installs
   corefonts, `d3dcompiler_47` and Wine Mono, but it has never been run
   end to end — `steam-base` predates it and was provisioned by hand. Making a
   per-game prefix is the natural time to exercise it.

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
