# Superseded patches — the base now provides the fix

> **⚠️ `wine-0004` is NOT superseded and is no longer in this directory.** It was moved
> here on the expectation that `CX HACK 23950` replaced it; that was **tested and
> disproved** the same day (2026-07-31). Steam's window is black without it on
> WineCX 26.3.0, the 23950 path never fires for CEF windows, and CrossOver never
> modified `winemac.drv/surface.c`. It is back in `patches/series` as a `workaround`
> — see `docs/RUNTIME.md` §2 for the measurements. The table below is kept as written
> so the mistake is visible rather than tidied away.

These four patches worked, and three of them were the difference between a game
that ran and a game that did not. They are here because the project's base
changed from upstream Wine 11.14 to **WineCX 26.3.0** (CodeWeavers' published
CrossOver Wine source), which already carries every one of them.

This is a different category from `patches/retracted/`: those were *wrong*.
These were right, and are simply no longer ours to carry.

Evidence, in `src/crossover/sources/wine` at tag `winecx-26.3.0`:

| patch | what it was | where it lives in the base |
| ----- | ----------- | -------------------------- |
| `wine-0006-wow64cpu-rosetta2-mode-switch` | port of `CW HACK 20760` — lcall/lretq instead of ljmp for the WoW64 32↔64 mode switch under Rosetta 2. Without it **no** 32-bit game survives. | `dlls/wow64cpu/cpu.c:253`, `:327`, `:405` |
| `wine-0007-win32u-async-ncpaint` | port of `CW Hack 20969, 23427` — async `WM_NCPAINT`; took 2D/GDI menus from 5–10 s per hover to real-time. Carried `CW Hack 23394` too, which measured zero hits. | `dlls/win32u/dce.c:1801`, `:1840` |
| `wine-0008-winemac-forward-compat-gl-context` | port of `CW Hack 24834`. Inert unless `WINEMAC_FWD_COMPAT_GL_CTX=1`. | `dlls/winemac.drv/opengl.c:2166` |
| `wine-0004-kernelbase-append-args-env` | *not* a port — our `WINE_APPEND_ARGS` mechanism, which forced `--in-process-gpu` into `steamwebhelper.exe` so Steam's window was not black. CrossOver solves the underlying problem properly with `CX HACK 23950`: a shared-memory surface for windows whose parent lives in another process. | `dlls/win32u/dce.c:731`–`934`, `dlls/win32u/message.c` (3 sites) |

The first three are confirmed redundant twice over — `git apply` refuses them against
the base because the code is already there, **and** the behaviour was verified on
2026-07-31: the game no longer dies in the WoW64 thunk (user time 2:16 against system
time 0:01, where the bug produced the inverse ratio) and menus respond in real time.

**`wine-0004` was the exception, and it failed its test** — it is back in
`patches/series`. See the warning at the top of this file.

Do not port anything from here onto the WineCX base. If a patch in
`patches/series` turns out to be a re-derivation of a `CW HACK`, that is the
smell the pivot was meant to remove — take it from the base instead.

Nothing here is needed to rebuild the *current* WineCX runtime. They are needed
only to reproduce `runtimes/wine-11.14-x86_64`, the upstream-based fallback,
whose source tree `src/wine` still has all four applied (`wine-0004` included,
which is why that tree shows five modified files).
