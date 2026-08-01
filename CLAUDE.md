# Instructions for AI agents in this repository

**Read `AGENT_START_HERE.md` first.** It is the operating guide: how to build the
runtime, make prefixes, run games, patch Wine, measure results, and where to record
findings. This file is only the short version.

`docs/NEXT.md` is the **only** file that tracks current state — what works, what is open,
and the "do not re-chase these" list of measured dead ends. `README.md`,
`AGENT_START_HERE.md` and `docs/RUNTIME.md` are durable and should not accumulate status.

## Non-negotiables

- **Measure; do not infer.** Say "measured" or "inferred" explicitly. A grep that finds a
  plausible cause is a candidate, not a finding — one such guess was written up as nearly
  settled here and misled the next reader for a full session.
- **Verify that a negative test could have detected a positive.** A control that cannot
  fail is not a control. Two near-misses here came from a "clean" result produced by a
  build too broken to reach the code path, and a command-line flag that silently did
  nothing.
- **Check the base before writing a patch.**
  `grep -rn 'CW HACK\|CX HACK' src/crossover/sources/wine/` — re-deriving a fix
  CodeWeavers already wrote is the specific waste this project was restructured to avoid.
- **Never edit the vendor source tree without a patch file in `patches/series`.**
  `bin/build-runtime` asserts the tree differs from its tag only by the series, and will
  fail. That assertion exists because an unpatched change once vanished from every clean
  rebuild.
- **Do not mask a missing library with `DYLD_FALLBACK_LIBRARY_PATH`.** Fix the bundle.
- **Do not write conclusions into the docs while still investigating.** Record
  observations; defer conclusions until the question is closed.

## Practical

- `bin/killwine` also kills a running CrossOver bottle — never run it while comparing
  against CrossOver.
- Never run two prefixes' Steam clients at once; they share `S:\steamapps`.
- A black window with an hourglass for ~30 s after launch is normal and carries no
  information. Ask the user what they see rather than guessing from a screenshot.
- Long builds (~25 min) and game launches are fine to run in the background, but say when
  they will saturate the machine.
