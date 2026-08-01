# Brief: a 32-bit Wine game dies in the WoW64 32→64 mode switch

> ## ✅ SOLVED — 2026-07-30. This file is kept as a case study, not an open task.
>
> **Answer:** CrossOver carries `CW HACK 20760` in `dlls/wow64cpu/cpu.c` — under
> Rosetta 2 (detected via the `VirtualApple` CPU brand string) it replaces the
> mode-switching far `ljmp` with a far `lcall` inbound and a far `lretq` outbound,
> to dodge a SIGUSR1 race. Their comment: *"When running under Rosetta 2, use
> lretq instead of ljmp to work around a SIGUSR1 race condition."*
>
> The reference implementation was **already on disk** the whole time at
> `src/crossover/sources/wine/dlls/wow64cpu/cpu.c`. A one-line
> `grep -rl Rosetta src/crossover/sources/wine/` returns six files and lands on
> it. A port to 11.14 was built and verified to make the game reach its menu; it
> is **currently reverted** (see `docs/NEXT.md`).
>
> **§7 below is WRONG.** It says "the fix is not in the thunk's code" and sends
> the reader to the caller side. The divergence is in the thunk, `0x77` bytes past
> where §7's comparison stopped. §7 is left unedited on purpose — this brief was
> handed to several models as a clean-context test, and the wrong signpost is part
> of the record. Do not act on it.
>
> Worth noting exactly how it went wrong: §7's **candidate #3 is the right answer**
> ("a Rosetta-specific workaround CrossOver carries… CrossOver publishes full
> source, so the relevant patch may be findable directly"). The answer was written
> down as a candidate and then negated by the headline sentence above it. The
> failure was not missing information — it was asserting a conclusion over it.
>
> Also superseded: the claim that determinism argues against a race (invalid —
> the address is fixed by construction), and the flat "the overlay is irrelevant"
> (it is not the cause, but is the plausible *trigger*: it issues the
> `NtFlushInstructionCache` that crosses the boundary, and is the only source of
> `SuspendThread`/`SetThreadContext`, i.e. SIGUSR1 traffic).

**Status of the original text:** root cause identified and evidenced; the "open
question" it poses is now answered as above. Everything marked *measured* was
observed directly; everything marked *suspected* was not.

**Read this whole file before running anything.** There are four traps in this
environment that have each already produced a confidently wrong conclusion, and
they are listed in §8.

---

## 1. The ask, in one paragraph

A 32-bit Windows game runs correctly under **CrossOver 26.0** on this machine
and hangs under our **upstream Wine 11.14** build on the same machine, same
prefix layout, same game files (identical MD5s). We know exactly which
instruction faults and why. What we do not know is **why the same instruction,
byte-identical in CrossOver's own `wow64cpu.dll`, does not fault for them.**
Find that difference. A fix, or a precise explanation of the difference, is the
deliverable.

## 2. Host and build

| | |
| --- | --- |
| Hardware | Apple M1 Max, MacBookPro18,4 (**arm64**) |
| OS | macOS 26.5.2 (25F84) |
| Translation | Rosetta 2 (`/Library/Apple/usr/libexec/oah` present). All Wine code is x86_64 and therefore **translated**. |
| Our Wine | `wine-11.14`, upstream source, at `runtimes/wine-11.14-x86_64` |
| Reference | CrossOver 26.0.0 (`cxoffice-26.0.0rc2`), whose bundled Wine reports `wine-11.0` |
| Repo root | the checkout (paths below are relative to it) |

Our configure line (from `bin/build-runtime`, which reproduces the runtime from
a clean checkout):

```sh
CC="gcc -arch x86_64" CXX="g++ -arch x86_64" \
  ./configure $SEED \
    --build=x86_64-apple-darwin \
    --enable-archs=i386,x86_64 \
    --prefix=<runtimes/wine-11.14-x86_64> \
    --disable-tests --with-vulkan --with-mingw --with-freetype --with-gnutls --without-x
```

`$SEED` is a list of `ac_cv_lib_soname_*=<abs path>` assignments that point
Wine's `dlopen` sonames at bundled dylibs. That mechanism is unrelated to this
bug but do not remove it — see `docs/RUNTIME.md` §1.

Note `--enable-archs=i386,x86_64`: this is **new WoW64**, i.e. 32-bit PE code
running inside a 64-bit unix process. macOS has no 32-bit host support, so this
is the only option and CrossOver is in the same position.

## 3. The target

Strategic Command WWII: War in Europe, Steam appid **593030**.

- `SC3-WWII War in Europe.exe` — `PE32 executable (GUI) Intel 80386` — **fails**
- SDL 1.2 (`SDL.dll`, `SDL_ttf`, `SDL_image`, `SDL_mixer`, `SDL_gfx`), statically
  imports `steam_api.dll`
- Installed at
  `library/SteamLibrary/steamapps/common/Strategic Command WWII War in Europe/`
- The CrossOver bottle has its **own separate copy** of the game, but the
  binaries are **byte-identical** (verified by MD5 on the exe and `SDL.dll`), so
  "same game" is literal.

## 4. Symptom (measured)

Launched, the game takes a fullscreen window and burns ~100% of one core
indefinitely. It never loads campaign data or bitmaps.

**Do not treat the black screen as the bug.** CrossOver shows an identical black
fullscreen window with an hourglass cursor for ~30 s before its menu appears.
That phase is normal. Ours simply never leaves it. The window carries no
diagnostic information, and the previous investigation lost significant time
treating this as a rendering problem.

The CPU is **not** application work:

```
$ top -pid <game> -l 3 -s 2 -stats pid,cpu,faults,cow,sysbsd,sysmach,threads
PID    %CPU  FAULTS   COW  SYSBSD  SYSMACH   #TH
46796  101.6 4477296+ 1307 344638+ 35164559+ 8/2
```

≈**50,000 page faults/second** and ≈**375,000 mach traps/second**, sustained.
Per-thread (`ps -M`) the hot thread shows 48 s system time against 2 s user
time. It is a fault storm, not a stall or a spin loop in game code.

## 5. Root cause (measured)

### 5.1 Where it faults

`WINEDEBUG=+virtual` ends with exactly two lines and then goes permanently
silent while the process keeps spinning:

```
warn:virtual:virtual_setup_exception exception outside of stack limits
    addr 0x7b591139 stack 0x7b3f0f00 (0x100110000-0x100112000-0x10020fd20)
warn:virtual:virtual_setup_exception exception outside of stack limits
    addr 0x4101fd     stack 0x7b3efd50 (0x100110000-0x100112000-0x10020fd20)
```

These addresses are **byte-identical on every run**, across every configuration
tried. `WINEDEBUG=+loaddll` identifies the module:

```
Loaded L"C:\windows\system32\wow64cpu.dll" at 000000007B590000: builtin
```

So `0x7b591139` is `wow64cpu.dll + 0x1139`. Disassembling our own runtime's copy
(`lib/wine/x86_64-windows/wow64cpu.dll`, ImageBase `0x7a400000`):

```
000000007a401110 <syscall_32to64>:
    7a401110:  4c 87 f4              xchg %r14,%rsp
    7a401113:  41 89 bd 9c 00 00 00  mov  %edi,0x9c(%r13)
    ...
    7a401139:  8b 15 cd 5e 00 00     mov  0x5ecd(%rip),%edx   # cs32_sel
```

### 5.2 Why it faults

The `+seh` record for that exception:

```
dispatch_exception code=c0000005 (EXCEPTION_ACCESS_VIOLATION) addr=7B591139
  info[0]=00000000        <- access type: read
  info[1]=00005ECD        <- address accessed
  eip=7b591139 esp=7b3f0f0a ebp=0299f3a4
  cs=0107 ds=0023 es=0023 fs=010f gs=0023 ss=0023
```

**`info[1] = 0x5ECD` is the instruction's own displacement.** That can only
happen if the bytes were decoded in 32-bit mode, where `mod=00, rm=101` means
absolute `disp32` rather than RIP-relative. The same bytes both ways:

| bytes | 64-bit (intended) | 32-bit (what actually executed) |
| --- | --- | --- |
| `4c 87 f4` | `xchg %r14,%rsp` | `dec %esp` ; `xchg %esi,%esp` |
| `8b 15 cd 5e 00 00` | `mov 0x5ecd(%rip),%edx` | `mov 0x5ecd,%edx` |

Three independent corroborations:

1. **`cs=0107`** at fault time — still the *32-bit* code selector. The mode
   switch never happened.
2. **`esp=0x7b3f0f0a`** is misaligned and points into a lone 4 KB `rwx` page at
   `7b3f0000-7b3f1000` — not a stack. That is precisely the wreckage
   `dec %esp; xchg %esi,%esp` would leave.
3. **The two decodings re-synchronise at exactly `+0x1139`.** Summing 32-bit
   instruction lengths from `0x1110` gives 41 bytes = `0x29`, and
   `0x1110 + 0x29 = 0x1139`. This is why both the 32-bit and 64-bit views report
   the same fault address, and it is a strong consistency check on the whole
   reading.

**Conclusion:** the CS-changing far call that should switch to 64-bit mode before
entering `syscall_32to64` does not take effect, so Wine's 64-bit WoW64 thunk is
executed as 32-bit code.

### 5.3 Why it loops forever rather than crashing

ESP has been trashed to `0x7b3f0f0a`. Wine tries to build the Windows exception
frame there and walks downward past `0x7b3f0000` into the **`r-x`** region
underneath (`vmmap`: `7af26000-7b3f0000 r-x/rwx`). Writing the frame therefore
faults too. `lldb` attach confirms the steady state:

```
thread #2, stop reason = EXC_BAD_ACCESS (code=2, address=0x7b3efc30)
  frame #0: libsystem_platform.dylib`_platform_memmove$VARIANT$Rosetta + 340
  ->  movq %r12, (%rdi)
```

`code=2` is `KERN_PROTECTION_FAILURE` — a write to a mapped read-only page. That
fault is itself an exception, which re-enters the same delivery path, which
faults again. Hence 50,000/s forever, all in system time, with no further Wine
logging.

## 6. This is situational, NOT "32-bit is broken"

This is the obvious wrong conclusion and it is already refuted. Every 32-bit
process reaches `syscall_32to64` for *every syscall*, so a universally broken
thunk would prevent any 32-bit exe from loading a DLL. Two 32-bit exes **from
the same game directory** run fine on this runtime:

| exe | bitness | result |
| --- | --- | --- |
| `Launcher.exe` | PE32, .NET | runs, draws its window, its PLAY button works |
| `Editor-WWII War in Europe.exe` | PE32, same SDL stack | runs, 1.5% CPU, **zero** `virtual_setup_exception` |
| `SC3-WWII War in Europe.exe` | PE32 | faults as above |

So the mode switch normally succeeds and fails only at this one point. **Do not
start by rebuilding Wine.**

*Suspected, not measured* — the one lead on "why here": immediately before the
fault, `+seh` shows ~120 `code=80000026` (`STATUS_LONGJUMP`) unwinds, i.e. heavy
C++ exception traffic, then CRT `signal(11, ...)` / `signal(8, ...)` installing
SIGSEGV/SIGFPE handlers, then `GetModuleBaseNameA` module enumeration. The
working guess is that the thunk fails when entered from that unusual context.
This has not been confirmed and may be a red herring.

> **Superseded.** The crossing syscall is `eax=0x99` = `NtFlushInstructionCache`,
> issued by the overlay right after hot-patching `mmdevapi`. CodeWeavers attribute
> the failure to a SIGUSR1 race (Wine sends SIGUSR1 for thread suspension and
> system-APC delivery); a competing, unresolved reading is translation-cache
> invalidation from that very code patch. See `docs/NEXT.md`.

## 7. The open question — where to look

CrossOver's `lib/wine/x86_64-windows/wow64cpu.dll` has the **identical
instruction at the identical address**:

```
ours:       7a401139:  8b 15 cd 5e 00 00   mov 0x5ecd(%rip),%edx
CrossOver:  7a401139:  8b 15 d5 4e 00 00   mov 0x4ed5(%rip),%edx
```

Only the displacement differs (their `.bss` sits elsewhere). Both files contain
four `xchg %r14,%rsp` thunk prologues, the first two at identical addresses
(`0x7a401110`, `0x7a4011ad`; the later two are shifted). The binaries differ
overall (MD5 `c3dd4269…` vs `e29d0fcc…`) but the faulting construct is the same.

**Therefore the fix is not in the thunk's own code.** Candidate places for the
real difference, in rough order of promise:

1. **The caller side.** How 32-bit code transfers control into the thunk — the
   far call/jmp, and how `cs32_sel` / `cs64_sel` are established (LDT setup).
   Our fault context shows `cs=0107`, an LDT selector. Compare how CrossOver
   sets these up.
2. **The i386 `ntdll` syscall dispatcher** in `lib/wine/i386-windows/ntdll.dll`,
   which is what reaches the thunk.
3. **A Rosetta-specific workaround CrossOver carries.** They have shipped
   Apple-Silicon-specific work for years. `patches/crossover26-wined3d.diff` and
   `patches/crossover26-winemac.drv.diff` are already in this repo; CrossOver
   publishes full source, so the relevant patch may be findable directly.
4. **Whether Rosetta supports a CS-changing far call at all**, and if not, what
   alternative path Wine or CrossOver uses. If Rosetta silently ignores the mode
   switch, the question becomes why it usually works.

Available assets for the comparison:

- `runtimes/wine-cx26.3-x86_64` — **already built** from CrossOver's published
  source. Reports `wine-11.0`.
- `runtimes/wine-11.0-x86_64` — already built, upstream 11.0. Useful for
  separating "CrossOver's patches" from "11.0 vs 11.14".
- The working CrossOver bottle:
  `~/Library/Application Support/CrossOver/Bottles/Steam/`
- `bin/compare-bottle <bottle> <prefix>` diffs bottle vs prefix.

**Constraint:** do **not** point a different runtime at `prefixes/steam-base`. It
holds a provisioned, logged-in Steam plus winetricks packages that took hours to
build, and an 11.14 → 11.0 downgrade risks it. Make a throwaway prefix
(`bin/mkprefix`) instead. Note `bin/mkprefix` has never been run end to end.

## 8. Traps in this environment

Each of these has already caused a wrong conclusion. Please do not rediscover
them.

1. **`bin/killwine` used to miss most leftovers.** Wine rewrites a PE process's
   argv *and* its `comm` to the Windows image name, and orphans reparent to
   PID 1, so a stale process appears as bare `start.exe` with no path and
   matches nothing. 751 stale `winedbg --auto` handlers and four orphaned
   `start.exe` had accumulated while `killwine` reported "0 processes". Now
   fixed; use `bin/killwine --deep` for an exact `lsof`-based sweep (~20 s).
2. **`bin/gamerun` used to drop `WINEDLLOVERRIDES` silently.** With
   `BACKEND="none"` it produced a leading `;` and Wine ignored the entire list.
   A run intended to disable the Steam overlay loaded it anyway and looked
   tested. Fixed, but **always verify an override took effect** (e.g. with
   `+loaddll`) rather than assuming.
3. **Launching the game.** Steam games must normally start via
   `steam.exe -applaunch <appid>`; running the exe directly makes
   `steam_api.dll` hand the launch back through `steam://run/<appid>`, which
   does not inherit our environment. For debugging, a `steam_appid.txt`
   containing `593030` has been added next to the game exe — that makes
   `SteamAPI_Init` connect to the running client instead of re-launching, which
   is what enables `bin/gamerun strategic-command-direct`. It is an *added*
   file; `rm` it for stock behaviour. Steam still must be running.
4. **Fullscreen takes its own macOS Space**, so `screencapture` photographs the
   wrong one. Ask Gil to look at the screen, or send a ⌘⇧3 screenshot. Better:
   do not rely on the screen at all (§4).

## 9. Reproduction

```sh
cd <repo root>
bin/killwine --deep

bin/gamerun steam &                 # wait for login; ~20 s, watch:
#   prefixes/steam-base/drive_c/Program Files (x86)/Steam/logs/console_log.txt
#   for "System startup time:"

WINEDEBUG=+virtual,+pid bin/gamerun strategic-command-direct 2>&1 | tee /tmp/virt.log
# the two virtual_setup_exception lines are the last output; the process then
# spins silently at ~100% CPU
```

Useful follow-ups:

```sh
WINEDEBUG=+seh,+pid,+timestamp bin/gamerun strategic-command-direct   # exception records
WINEDEBUG=+loaddll,+pid        bin/gamerun strategic-command-direct   # module load map
sample <pid> 5 10 -file /tmp/s.txt                                   # native stacks
lldb -p <pid> --batch -o "thread list" -o "detach" -o "quit"          # see §note
vmmap -w <pid> > /tmp/vmmap.txt                                      # region protections
top -pid <pid> -l 3 -s 2 -stats pid,cpu,faults,cow,sysbsd,sysmach,threads
```

`lldb` note: attach works and prints the stop reason, but any *subsequent*
command hangs — `vmmap` output for this process is ~7200 lines, and it is
Rosetta-translated, so lldb warns it is reading `libobjc` from process memory.
Use `--batch` with a short command list, run it in the background, and kill it
after ~45 s. `-o` flags work; `-s <scriptfile>` did not. The attach message alone
contains the fault address and the faulting instruction, which is the useful
part.

Disassembly (both builds, for the comparison in §7):

```sh
OD=/usr/local/bin/x86_64-w64-mingw32-objdump
$OD -d --start-address=0x7a401100 --stop-address=0x7a401175 \
   runtimes/wine-11.14-x86_64/lib/wine/x86_64-windows/wow64cpu.dll
$OD -d "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/wow64cpu.dll"
```

To reproduce the 32-bit-vs-64-bit decode yourself:

```sh
printf '\x4c\x87\xf4\x41\x89\xbd\x9c\x00\x00\x00\x8b\x15\xcd\x5e\x00\x00' > /tmp/t.bin
$OD -D -b binary -m i386:x86-64 /tmp/t.bin   # xchg %r14,%rsp ... mov 0x5ecd(%rip),%edx
$OD -D -b binary -m i386        /tmp/t.bin   # dec %esp; xchg %esi,%esp ... mov 0x5ecd,%edx
```

## 10. Already ruled out — please do not re-test

| Theory | How it was refuted |
| --- | --- |
| Graphics / rendering / black window | Nothing GL, ddraw, D3D or backend-related is in the path; the game dies before drawing. `wined3d` and DXVK behaved *identically* because neither was ever loaded. |
| It is a D3D9 title | It is SDL 1.2 / DirectDraw. And the graphics API is irrelevant anyway. |
| Steam in-game overlay | Fault is **byte-identical** with the overlay disabled in Steam. Its SEH handlers appear in traces only because they are *walked* during the unwind. Its `Unknown opcodes for X86` lines mean it *declined* to hook, and name `SETUPAPI.dll`, nowhere near the crash site. Its log stopping at `Initializing Audio...` is the hang, not the cause. |
| Audio | `SDL_AUDIODRIVER=dummy` changes nothing; `DSOUND`/`mmdevapi` still load and the fault is identical. They load just before the fault — timing, not causation. |
| Our Wine is miscompiled / 32-bit is broken | §6. Two other 32-bit exes run fine on this runtime. |
| Missing VC++ 2022 runtime | Was a real difference from CrossOver's bottle, now installed, hang unchanged. |
| `gldriverquery.exe` failing | Fails identically inside CrossOver's *working* bottle. |

`docs/NEXT.md` carries a longer refuted list from the earlier Steam-client work.

## 11. Evidence on disk

- `docs/evidence/sc3-spin-sample.txt` — `sample` output of the spinning process
- `docs/evidence/sc3-wow64-fault-seh.txt` — `+seh` records around the fault
- `docs/evidence/sc3-wow64-fault-modules.txt` — module map + the two warnings
- `docs/NEXT.md` — current handover, §"The WoW64 mode switch"
- `docs/RUNTIME.md` — what this runtime changes from stock Wine and why
- `docs/PROJECT.md` — history; contains conclusions later disproved, marked as such

## 12. What a good answer looks like

Any of these, in descending order of value:

1. A change that makes the game reach its menu, with an explanation of *why* the
   mode switch was failing.
2. A precise identification of the CrossOver-vs-upstream difference responsible,
   even without a working fix.
3. A demonstration that the mode switch is not the real story, with evidence at
   least as specific as §5 — i.e. showing what `info[1]=0x5ECD`, `cs=0107` and
   the trashed ESP actually mean if not this.

**Method note from this project, which has held up repeatedly:** every genuine
finding here came from comparing against a working reference or measuring
directly. Inferring causes from an application's error messages produced a
string of confident, wrong theories. There is a working reference on this
machine — use it.
